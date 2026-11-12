// optimize.hpp — Simulated annealing and greedy median-move refinement.
#pragma once
#include "placement.hpp"

#include <chrono>
#include <cstdint>

/// Pseudo-random generator xoshiro256** (Blackman & Vigna, public domain).
///
/// Used instead of <random> so that a seed gives the same sequence with any
/// compiler and standard library (the std distributions are
/// implementation-defined), and because it is small and fast.
struct Rng {
    uint64_t s[4];  ///< Generator state (256 bits).

    /// Seeds the state with splitmix64 (S. Vigna), so nearby seeds such as
    /// S and S+1 still give unrelated streams.
    explicit Rng(uint64_t seed) {
        for (auto& v : s) {
            seed += 0x9E3779B97F4A7C15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            v = z ^ (z >> 31);
        }
    }
    /// Rotates x left by k bits (0 < k < 64).
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    /// Returns the next 64-bit random value and advances the state.
    uint64_t next() {
        uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    /// Returns an integer in [0, n), using a multiply-shift instead of a
    /// division (slightly biased; negligible for the n used here).
    uint32_t below(uint32_t n) { return (uint32_t)(((next() >> 32) * (uint64_t)n) >> 32); }
    /// Returns a double uniformly distributed in [0, 1).
    double uniform() { return (next() >> 11) * 0x1.0p-53; }
};

/// Monotonic clock used for all budgets and deadlines.
using Clock = std::chrono::steady_clock;

/// Seconds elapsed since t0.
double secondsSince(Clock::time_point t0);

/// Settings of one annealing (refine() and windowOpt() also read start,
/// deadline and verbose from it).
struct AnnealOptions {
    bool keepBest = false;   ///< Experimental: restore the best state seen before the greedy pass.
    double effort = 1.0;     ///< Moves per temperature = effort * Nmovable^(4/3) (VPR's inner_num).
    Clock::time_point start; ///< Program start; deadline is measured from here.
    double deadline = 1e18;  ///< Seconds after start; annealing stops early if reached.
    double t0Scale = 20.0;   ///< T0 = t0Scale * std-dev of random move deltas (VPR: 20).
    double r0 = 1.0;         ///< Initial move window as a fraction of the grid size (VPR: 1).
    bool verbose = false;    ///< Print annealing progress to stderr.
    int runId = 0;           ///< Run number, used in log lines.
    /// Deadline-driven schedule: when true, the number of moves per temperature
    /// is re-planned at every step from the measured move rate and the
    /// estimated number of remaining temperatures, so the schedule finishes
    /// close to the deadline instead of relying on an up-front rate probe.
    /// Off for fixed-effort runs (they must stay reproducible).
    bool adaptive = false;
    double expectedTemps = 112;  ///< Typical total temperature count (prior for the adaptive estimate).
    double maxEffort = 30;       ///< Upper bound on moves per temperature, in effort units.
};

/// What an annealing run did.
struct AnnealStats {
    int temperatures = 0;        ///< Temperature steps executed.
    long long moves = 0;         ///< Moves evaluated.
    bool hitDeadline = false;    ///< true if it stopped at the deadline (result not reproducible).
    double effectiveEffort = 0;  ///< moves / (temperatures * N^(4/3)), the effort actually used.
};

/// Picks a random target site for cell a: a site of a's type inside a window
/// of radius R around it, other than a's own site and not holding a fixed
/// cell. The window is widened to the type's colGap/rowGap so sparse RAM and
/// DSP columns stay reachable.
/// @param tx,ty Receive the target coordinates.
/// @return false if no target was found within a few tries.
bool proposeTarget(const Placement& P, int a, int R, Rng& rng, int& tx, int& ty);

/// VPR-style simulated annealing of P's movable cells with single-cell moves
/// and swaps, followed by a greedy pass at T = 0. The schedule (T0, cooling
/// by acceptance rate, window size, stop rule) is described in the README.
/// @param P   Placement to optimize in place.
/// @param rng Random source; with a fixed effort and no deadline hit, the
///            result depends only on its seed.
/// @param opt Schedule settings.
/// @return Statistics of the run.
AnnealStats anneal(Placement& P, Rng& rng, const AnnealOptions& opt);

/// Measures how many random full-window moves per second can be evaluated
/// (not committed) on P. Used to turn a time budget into an annealing effort.
/// @param seconds Duration of the measurement.
/// @return Moves per second.
double measureMoveRate(Placement& P, Rng& rng, double seconds);

/// Greedy refinement: moves each cell toward the median region of its nets'
/// boxes (the "optimal region"), accepting only improvements. Nets with more
/// than 256 pins are ignored when computing the region.
/// @param maxPasses Maximum passes over all movable cells.
/// @return HPWL gained (positive means the cost went down).
long long refine(Placement& P, Rng& rng, const AnnealOptions& opt, int maxPasses);

/// Statistics of windowOpt().
struct WindowStats {
    long long windows = 0;  ///< Windows optimized.
    int passes = 0;         ///< Full passes completed.
};

/// Exact optimization of small windows (rows x cols sites of one type): every
/// arrangement of the window's cells over its sites is tried and the best is
/// kept. Deterministic; stops at opt.deadline.
/// @param rows,cols  Window size (the CLI allows at most 8 sites).
/// @param maxPasses  Maximum passes over the grid.
/// @param stats      If non-null, receives the statistics.
/// @return HPWL gained (positive means the cost went down).
long long windowOpt(Placement& P, const AnnealOptions& opt, int rows, int cols, int maxPasses,
                    WindowStats* stats = nullptr);
