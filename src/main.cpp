// main.cpp — Command-line driver.
//
//   placer <file.sitemap> <file.nets> <file.pl> <output.pl> [options]
//
// Flow: load -> (calibrate effort from the time budget) -> K independent runs
// of [simulated annealing -> median-move refinement], executed in parallel
// threads with seeds seed, seed+1, ... -> keep the best -> verify -> write.
#include "analytic.hpp"
#include "design.hpp"
#include "optimize.hpp"
#include "placement.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// Empirical average number of temperature steps of the VPR schedule on the
// course benchmarks (measured; see README). Used only to turn a time budget
// into an effort value.
static const double kExpectedTemperatures = 165.0;
// Low-temperature start used after the analytical placement (tuned, see README).
static double kQpT0Scale = 1.0;
static double kQpR0 = 0.05;
static double kQpExpectedTemperatures = 117.0;  // cold start; 112-120 on FPGA01-04 (t0.csv)
// "Warm" analytical start for optional portfolio runs (--warm-runs, default 0).
// It beat cold only on FPGA04 at effort 1 (-1.6%); at efforts 2-4 it ties there
// and is equal or worse elsewhere (sweep.csv), so it is off by default.
static const double kWarmT0Scale = 4.0;
static const double kWarmR0 = 0.15;
static const double kWarmExpectedTemperatures = 156.0;  // measured: 155-157 on FPGA01-04
// Quality saturates well before this on small designs (FPGA00: ~10).
static const double kMaxEffort = 30.0;
static const double kAnnealShare = 0.85;  // share of the budget given to annealing
// Annealing moves (with commits, several threads sharing memory bandwidth) are
// slower than the evaluate-only probe; this margin keeps runs inside the budget
// so they finish their schedule and remain reproducible.
static const double kRateSafety = 0.75;

static void usage() {
    std::fprintf(stderr,
                 "usage: placer <sitemap> <nets> <pl> <out.pl> [options]\n"
                 "  --seed S         base random seed (default 1); run i uses S+i\n"
                 "  --runs K         independent annealing runs, best one is kept (default: #cores, max 4)\n"
                 "  --threads T      worker threads (default: min(runs, #cores))\n"
                 "  --time-budget S  target wall-clock seconds (default: 20 + 0.012 * #movable cells)\n"
                 "  --effort E       fixed annealing effort (VPR inner_num); disables calibration.\n"
                 "                   Same seed + effort + runs => identical result.\n"
                 "  --init MODE      initial placement: qp (analytical, default) or given (input .pl)\n"
                 "  --t0-scale F     T0 = F * std-dev of random move deltas (default: 20 given, see README for qp)\n"
                 "  --r0 F           initial move window as fraction of grid (default: 1 given, see README for qp)\n"
                 "  --warm-runs W    with --init qp, the last W runs use a warmer start (t0-scale %g, r0 %g)\n"
                 "                   (default 0; ignored if --t0-scale or --r0 is given)\n"
                 "  --fixed-schedule with a time budget, keep the calibrated moves per temperature\n"
                 "                   instead of re-planning them at each step from the measured rate\n"
                 "  --keep-best      restore best annealing state before refinement (experimental)\n"
                 "  --no-keep-best   disable best-state restoration (default)\n"
                 "  --verbose        print annealing progress\n", kWarmT0Scale, kWarmR0);
}

struct RunResult {
    long long cost = -1;
    std::vector<int> x, y;
    AnnealStats st;
    long long refineGain = 0;
    unsigned seed = 0;
};

int main(int argc, char** argv) {
    const Clock::time_point start = Clock::now();
    if (argc < 5) { usage(); return 1; }
    const std::string fSitemap = argv[1], fNets = argv[2], fPl = argv[3], fOut = argv[4];
    unsigned seed = 1;
    int runs = -1, threads = -1;
    double budget = -1, effort = -1;
    bool verbose = false, fixedSchedule = false, keepBest = false;
    std::string init = "qp";
    double t0Scale = -1, r0 = -1;
    int warmRuns = 0;
    AnalyticOptions ao;
    for (int i = 5; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(1); }
            return argv[++i];
        };
        if (a == "--seed") seed = (unsigned)std::strtoul(val(), nullptr, 10);
        else if (a == "--runs") runs = std::atoi(val());
        else if (a == "--threads") threads = std::atoi(val());
        else if (a == "--time-budget") budget = std::atof(val());
        else if (a == "--effort") effort = std::atof(val());
        else if (a == "--verbose") verbose = true;
        else if (a == "--keep-best") keepBest = true;
        else if (a == "--no-keep-best") keepBest = false;
        else if (a == "--fixed-schedule") fixedSchedule = true;
        else if (a == "--init") init = val();
        else if (a == "--t0-scale") t0Scale = std::atof(val());
        else if (a == "--r0") r0 = std::atof(val());
        else if (a == "--warm-runs") warmRuns = std::atoi(val());
        else if (a == "--qp-iters") ao.iterations = std::atoi(val());
        else if (a == "--qp-growth") ao.anchorGrowth = std::atof(val());
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }

    Design D;
    try {
        D.load(fSitemap, fNets, fPl);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    const int N = (int)D.movable.size();
    const int cores = std::max(1u, std::thread::hardware_concurrency());
    if (runs <= 0) runs = std::min(cores, 4);
    if (threads <= 0) threads = std::min(runs, cores);
    threads = std::min(threads, runs);
    // With an explicit --effort and no --time-budget there is no deadline: the
    // schedule always runs to completion (needed for reproducible sweeps).
    if (budget <= 0) budget = effort > 0 ? 1e9 : 20.0 + 0.012 * N;
    const int batches = (runs + threads - 1) / threads;

    Placement base(D);
    std::fprintf(stderr, "grid %dx%d  cells %d (movable %d)  nets %d  pins %zu  initial HPWL %lld (%.2fs)\n", D.W,
                 D.H, D.numCells(), N, D.numNets(), D.netPins.size(), base.cost, secondsSince(start));

    // ---- initial placement ------------------------------------------------------
    if (init != "qp" || t0Scale > 0 || r0 > 0) warmRuns = 0;
    warmRuns = std::max(0, std::min(warmRuns, runs));
    if (init == "qp") {
        ao.verbose = verbose;
        long long c = analyticInit(base, ao);
        std::fprintf(stderr, "analytical initial placement: HPWL %lld (%.2fs)\n", c, secondsSince(start));
        if (t0Scale <= 0) t0Scale = kQpT0Scale;
        if (r0 <= 0) r0 = kQpR0;
    } else if (init == "given") {
        if (t0Scale <= 0) t0Scale = 20.0;
        if (r0 <= 0) r0 = 1.0;
    } else {
        std::fprintf(stderr, "unknown --init %s\n", init.c_str());
        return 1;
    }
    // Measured counts for the standard settings; other T0 values are scaled
    // from the nearest one below by ln(ratio)/ln(1/0.95) (mid-schedule cooling).
    auto expectedTempsFor = [&](double t0s) {
        double base = kExpectedTemperatures, t0b = 20.0;
        if (init == "qp") {
            base = t0s >= kWarmT0Scale ? kWarmExpectedTemperatures : kQpExpectedTemperatures;
            t0b = t0s >= kWarmT0Scale ? kWarmT0Scale : kQpT0Scale;
        }
        return std::max(10.0, base + std::log(t0s / t0b) / std::log(1.0 / 0.95));
    };
    // Calibration plans for the longest schedule among the runs.
    const double expectedTemps = expectedTempsFor(warmRuns > 0 ? std::max(t0Scale, kWarmT0Scale) : t0Scale);

    // ---- effort calibration ---------------------------------------------------
    // With a calibrated effort the schedule is also re-planned at run time
    // (AnnealOptions::adaptive); the calibrated value is only the starting point.
    const bool adaptive = effort <= 0 && !fixedSchedule;
    if (effort <= 0) {
        Rng r(seed ^ 0xABCDEFULL);
        Placement probe(base);
        double rate = measureMoveRate(probe, r, 0.3);
        // Threads beyond the core count share CPUs.
        double perRunRate = kRateSafety * rate * std::min(1.0, (double)cores / threads);
        double perBatch = (budget - secondsSince(start)) * kAnnealShare / batches;
        double movesAvail = perRunRate * std::max(1.0, perBatch);
        effort = movesAvail / (std::pow((double)std::max(N, 1), 4.0 / 3.0) * expectedTemps);
        effort = std::min(kMaxEffort, std::max(0.05, effort));
        std::fprintf(stderr, "calibration: %.3g moves/s -> effort %.3f (budget %.0fs, %d runs on %d threads)\n",
                     rate, effort, budget, runs, threads);
    }
    std::fprintf(stderr, "effort %.4f%s  runs %d  threads %d  base seed %u  init %s  t0-scale %.3g  r0 %.3g", effort,
                 adaptive ? " (adaptive)" : "", runs, threads, seed, init.c_str(), t0Scale, r0);
    if (warmRuns > 0)
        std::fprintf(stderr, "  (last %d runs: t0-scale %.3g  r0 %.3g)", warmRuns, kWarmT0Scale, kWarmR0);
    std::fprintf(stderr, "\n");

    // ---- independent runs --------------------------------------------------
    std::vector<RunResult> results(runs);
    const double t0 = secondsSince(start);
    for (int b = 0; b < batches; b++) {
        const double batchEnd = t0 + (budget - t0) * (b + 1) / batches;
        std::vector<std::thread> pool;
        for (int r = b * threads; r < std::min(runs, (b + 1) * threads); r++) {
            pool.emplace_back([&, r, batchEnd]() {
                RunResult& res = results[r];
                res.seed = seed + (unsigned)r;
                Rng rng(res.seed);
                Placement P(base);
                AnnealOptions opt;
                opt.keepBest = keepBest;
                opt.effort = effort;
                opt.start = start;
                opt.deadline = batchEnd - 0.05 * (batchEnd - t0) / (b + 1);  // leave room for refinement
                opt.verbose = verbose;
                opt.runId = r;
                const bool warm = r >= runs - warmRuns;
                opt.t0Scale = warm ? kWarmT0Scale : t0Scale;
                opt.r0 = warm ? kWarmR0 : r0;
                opt.adaptive = adaptive;
                opt.expectedTemps = expectedTempsFor(opt.t0Scale);
                opt.maxEffort = kMaxEffort;
                res.st = anneal(P, rng, opt);
                opt.deadline = batchEnd;
                res.refineGain = refine(P, rng, opt, 20);
                res.cost = P.cost;
                res.x = P.x;
                res.y = P.y;
                std::fprintf(stderr, "run %d%s (seed %u): HPWL %lld  temps %d  moves %lld  eff.effort %.3f  refine -%lld%s  t=%.1fs\n",
                             r, warm ? " warm" : "", res.seed, res.cost, res.st.temperatures, res.st.moves, res.st.effectiveEffort, res.refineGain,
                             res.st.hitDeadline ? "  [deadline hit: not reproducible]" : "",
                             secondsSince(start));
            });
        }
        for (auto& t : pool) t.join();
    }

    // ---- pick best, verify, write ------------------------------------------------
    int best = 0;
    for (int r = 1; r < runs; r++)
        if (results[r].cost < results[best].cost) best = r;
    Placement fin(D);
    fin.setPositions(results[best].x, results[best].y);
    std::string why;
    if (!fin.isLegal(&why)) { std::fprintf(stderr, "INTERNAL ERROR: illegal placement: %s\n", why.c_str()); return 2; }
    long long check = fin.fullHpwl();
    if (check != results[best].cost) {
        std::fprintf(stderr, "INTERNAL ERROR: incremental HPWL %lld != full %lld\n", results[best].cost, check);
        return 2;
    }
    {
        std::ofstream o(fOut);
        if (!o) { std::fprintf(stderr, "cannot write %s\n", fOut.c_str()); return 1; }
        for (int c = 0; c < D.numCells(); c++)
            o << D.cellName[c] << ' ' << D.typeNames[D.cellType[c]] << ' ' << fin.x[c] << ' ' << fin.y[c] << ' '
              << (D.cellFixed[c] ? 'F' : 'M') << '\n';
    }
    double mean = 0;
    for (auto& r : results) mean += (double)r.cost;
    mean /= runs;
    std::printf("RESULT hpwl=%lld best_seed=%u mean_hpwl=%.0f runs=%d effort=%.4f time=%.2fs\n", check,
                results[best].seed, mean, runs, effort, secondsSince(start));
    return 0;
}
