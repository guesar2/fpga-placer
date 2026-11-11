// optimize.hpp — Simulated annealing and greedy median-move refinement.
#pragma once
#include "placement.hpp"

#include <chrono>
#include <cstdint>

// xoshiro256** (Blackman & Vigna, public domain), seeded with splitmix64.
struct Rng {
    uint64_t s[4];
    explicit Rng(uint64_t seed) {
        for (auto& v : s) {
            seed += 0x9E3779B97F4A7C15ULL;
            uint64_t z = seed;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
            v = z ^ (z >> 31);
        }
    }
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t next() {
        uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    uint32_t below(uint32_t n) { return (uint32_t)(((next() >> 32) * (uint64_t)n) >> 32); }
    double uniform() { return (next() >> 11) * 0x1.0p-53; }
};

using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point t0);

struct AnnealOptions {
    double effort = 1.0;         // moves per temperature = effort * Nmovable^(4/3)  (VPR's inner_num)
    Clock::time_point start;     // program start
    double deadline = 1e18;      // seconds after start; annealing stops early if reached
    double t0Scale = 20.0;       // T0 = t0Scale * std-dev(delta) of random moves (VPR: 20)
    double r0 = 1.0;             // initial window as a fraction of the grid size (VPR: 1)
    bool verbose = false;
    int runId = 0;
    // Deadline-driven schedule: when true, the number of moves per temperature
    // is re-planned at every step from the measured move rate and the
    // estimated number of remaining temperatures, so the schedule finishes
    // close to the deadline instead of relying on an up-front rate probe.
    // Off for fixed-effort runs (they must stay reproducible).
    bool adaptive = false;
    double expectedTemps = 112;  // typical total temperature count (prior for the estimate)
    double maxEffort = 30;       // upper bound on moves per temperature (in effort units)
};

struct AnnealStats {
    int temperatures = 0;
    long long moves = 0;
    bool hitDeadline = false;
    double effectiveEffort = 0;  // moves / (temperatures * N^(4/3)), i.e. the effort actually used
};

// Picks a random legal target site for cell a within a window of radius R
// (widened per resource type so sparse RAM/DSP columns stay reachable).
bool proposeTarget(const Placement& P, int a, int R, Rng& rng, int& tx, int& ty);

AnnealStats anneal(Placement& P, Rng& rng, const AnnealOptions& opt);

// Moves of evaluated (not committed) random proposals per second; used to turn
// a time budget into an annealing effort.
double measureMoveRate(Placement& P, Rng& rng, double seconds);

// Greedy refinement: move each cell toward the median of its nets' boxes
// ("optimal region"), accepting only improvements. Returns HPWL gained.
long long refine(Placement& P, Rng& rng, const AnnealOptions& opt, int maxPasses);

// Exact optimization of small windows (rows x cols sites of one type): every
// arrangement of the window's cells over its sites is tried, the best is kept.
// Deterministic; stops at opt.deadline. Returns HPWL gained.
struct WindowStats { long long windows = 0; int passes = 0; };
long long windowOpt(Placement& P, const AnnealOptions& opt, int rows, int cols, int maxPasses,
                    WindowStats* stats = nullptr);
