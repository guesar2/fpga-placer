// main.cpp — Command-line driver.
//
//   placer <file.sitemap> <file.nets> <file.pl> <output.pl> [options]
//
// Flow: load -> (calibrate effort from the time budget) -> K independent runs
// of [simulated annealing -> median-move refinement], executed in parallel
// threads with seeds seed, seed+1, ... -> keep the best -> verify -> write.
#include "analytic.hpp"
#include "cluster.hpp"
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
#include <memory>
#include <thread>
#include <vector>

/// Average number of temperature steps of the VPR schedule from a random
/// start (T0 = 20 sigma) on the course benchmarks (measured; see README).
/// Used to turn a time budget into an effort value.
static const double kExpectedTemperatures = 165.0;
/// T0 scale of the "cold" start after the analytical placement: T0 = 1 sigma
/// (tuned; 2 sigma was worse, see README).
static double kQpT0Scale = 1.0;
/// Initial move window of the cold start, as a fraction of the grid (tuned).
static double kQpR0 = 0.05;
/// Temperature steps of a cold start; measured 112-120 on FPGA01-04 (t0.csv).
static double kQpExpectedTemperatures = 117.0;
/// T0 scale of the "warm" start for optional portfolio runs (--warm-runs,
/// default 0). It beat cold only on FPGA04 at effort 1 (-1.6%); at efforts
/// 2-4 it ties there and is equal or worse elsewhere (sweep.csv).
static const double kWarmT0Scale = 4.0;
/// Initial move window of the warm start, as a fraction of the grid.
static const double kWarmR0 = 0.15;
/// Temperature steps of the flat anneal after unclustering; measured 70-86
/// (K=8, FPGA01-02). Used only by the adaptive schedule in time-budget mode.
static const double kFineExpectedTemperatures = 90.0;
/// Temperature steps of a warm start; measured 155-157 on FPGA01-04.
static const double kWarmExpectedTemperatures = 156.0;
/// Upper bound on the calibrated effort. Quality saturates well before this
/// on small designs (FPGA00: about 10).
static const double kMaxEffort = 30.0;
/// Share of the time budget given to annealing; the rest covers the probe,
/// refinement, verification and margin.
static const double kAnnealShare = 0.85;
/// Safety factor on the measured move rate. Annealing moves (with commits,
/// several threads sharing memory bandwidth) are slower than the
/// evaluate-only probe; this margin keeps runs inside the budget so they
/// finish their schedule and remain reproducible.
static const double kRateSafety = 0.75;

/// Default time budget in seconds, a single power law in the number of
/// movable cells (scripts/budget_fit.py): fitted so that it is 70% of the
/// time limit on the benchmarks whose limit binds (FPGA01-03), capped at 3 h.
/// The adaptive schedule keeps a run within ~94% of its budget on any
/// machine, plus a few seconds of loading / analytical placement.
/// @param N Number of movable cells.
/// @return min(10800, 60 + 3.800348e-7 * N^2.1949).
static double defaultBudget(int N) {
    return std::min(10800.0, 60.0 + 3.800348e-7 * std::pow((double)std::max(N, 1), 2.1949));
}

/// Prints the command-line help to stderr.
static void usage() {
    std::fprintf(stderr,
                 "usage: placer <sitemap> <nets> <pl> <out.pl> [options]\n"
                 "  --seed S         base random seed (default 1); run i uses S+i\n"
                 "  --runs K         independent annealing runs, best one is kept (default: #cores, max 4)\n"
                 "  --max-runs M     with a time budget, if the effort hits its cap (small designs), extra\n"
                 "                   batches of runs fill the unused budget, up to M runs in total (default 64;\n"
                 "                   M <= K disables)\n"
                 "  --threads T      worker threads (default: min(runs, #cores))\n"
                 "  --time-budget S  target wall-clock seconds (default: min(3 h, 60 + 3.8e-7 * N^2.195), N = movable cells)\n"
                 "  --effort E       fixed annealing effort (VPR inner_num); disables calibration.\n"
                 "                   Same seed + effort + runs => identical result.\n"
                 "  --init MODE      initial placement: qp (analytical, default) or given (input .pl)\n"
                 "  --t0-scale F     T0 = F * std-dev of random move deltas (default: 20 given, see README for qp)\n"
                 "  --r0 F           initial move window as fraction of grid (default: 1 given, see README for qp)\n"
                 "  --warm-runs W    with --init qp, the last W runs use a warmer start (t0-scale %g, r0 %g)\n"
                 "                   (default 0; ignored if --t0-scale or --r0 is given)\n"
                 "  --window-passes P  exact window optimization after refinement: P passes (default 3; 0 = off)\n"
                 "  --window R,C     window = R sites x C columns of one type (default 3,2; at most 8 sites)\n"
                 "  --cluster K      clustering-driven flow: anneal clusters of up to K cells first,\n"
                 "                   then uncluster and finish with a cold flat anneal (default 8; 0 = off)\n"
                 "  --coarse-effort E  effort of the clustered anneal (default: same as --effort)\n"
                 "  --coarse-share F with a time budget, share of the annealing time for the clustered phase (default 0.5)\n"
                 "  --coarse-starts C  clustered anneal attempts per run, each with 1/C of its time; best continues (default 1)\n"
                 "  --fine-t0 F / --fine-r0 F  start of the flat anneal after unclustering (default 0.3 / 0.02)\n"
                 "  --fixed-schedule with a time budget, keep the calibrated moves per temperature\n"
                 "                   instead of re-planning them at each step from the measured rate\n"
                 "  --keep-best      restore best annealing state before refinement (experimental)\n"
                 "  --no-keep-best   disable best-state restoration (default)\n"
                 "  --verbose        print annealing progress\n", kWarmT0Scale, kWarmR0);
}

/// Outcome of one independent run (anneal -> refine -> windowOpt, 3 passes by default).
struct RunResult {
    long long cost = -1;       ///< Final HPWL (-1 until the run sets it).
    std::vector<int> x, y;     ///< Final coordinates of every cell.
    AnnealStats st;            ///< Statistics of the (flat) anneal.
    long long refineGain = 0;  ///< HPWL gained by refine().
    long long windowGain = 0;  ///< HPWL gained by windowOpt().
    unsigned seed = 0;         ///< Seed of this run.
};

int main(int argc, char** argv) {
    const Clock::time_point start = Clock::now();
    if (argc < 5) { usage(); return 1; }
    const std::string fSitemap = argv[1], fNets = argv[2], fPl = argv[3], fOut = argv[4];
    unsigned seed = 1;
    int runs = -1, threads = -1, maxRuns = 64;
    double budget = -1, effort = -1;
    bool verbose = false, fixedSchedule = false, keepBest = false;
    std::string init = "qp";
    double t0Scale = -1, r0 = -1;
    int warmRuns = 0;
    int clusterK = 8;
    int windowPasses = 3, windowRows = 3, windowCols = 2;
    double coarseEffort = -1, coarseShare = 0.5, fineT0 = 0.3, fineR0 = 0.02;
    int coarseStarts = 1;
    AnalyticOptions ao;
    for (int i = 5; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&]() -> const char* {
            if (i + 1 >= argc) { usage(); std::exit(1); }
            return argv[++i];
        };
        if (a == "--seed") seed = (unsigned)std::strtoul(val(), nullptr, 10);
        else if (a == "--runs") runs = std::atoi(val());
        else if (a == "--max-runs") maxRuns = std::atoi(val());
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
        else if (a == "--cluster") clusterK = std::atoi(val());
        else if (a == "--coarse-effort") coarseEffort = std::atof(val());
        else if (a == "--coarse-share") coarseShare = std::atof(val());
        else if (a == "--coarse-starts") coarseStarts = std::max(1, std::atoi(val()));
        else if (a == "--fine-t0") fineT0 = std::atof(val());
        else if (a == "--fine-r0") fineR0 = std::atof(val());
        else if (a == "--window-passes") windowPasses = std::atoi(val());
        else if (a == "--window") { const char* v = val(); if (std::sscanf(v, "%d,%d", &windowRows, &windowCols) != 2) { usage(); return 2; } }
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
    if (budget <= 0) budget = effort > 0 ? 1e9 : defaultBudget(N);
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

    // ---- optional clustering (shared by all runs) -------------------------------
    Clustering cl;
    if (clusterK >= 2) {
        // Dense designs may not have enough k-site blocks: halve k until it fits.
        for (int k = clusterK; k >= 2 && !cl.ok; k /= 2) {
            Rng crng(seed ^ 0xC1C1C1ULL);
            ClusterOptions co;
            co.k = k;
            cl = buildClustering(D, base.x, base.y, co, crng, true);
        }
        std::fprintf(stderr, "clustering %s (%.2fs)\n", cl.ok ? "built" : "not applicable", secondsSince(start));
    }

    // ---- independent runs --------------------------------------------------
    // The planned runs go in `batches` batches of `threads`. If the effort was
    // capped at kMaxEffort (a small design that cannot use its budget), more
    // batches follow while the next one is predicted to fit (1.5x the last
    // batch's duration, within 90% of the budget), up to maxRuns runs. Runs on
    // a small design settle in one of a few basins early in the schedule, so
    // extra restarts help where extra effort does not. Each extra run has the
    // same fixed (capped) effort; only how many batches fit depends on speed.
    std::vector<RunResult> results(runs);
    const double t0 = secondsSince(start);
    const double reserve = 0.05 * (budget - t0) / batches;  // room for refinement in each batch
    auto runBatch = [&](int first, int last, double batchEnd) {
        std::vector<std::thread> pool;
        for (int r = first; r < last; r++) {
            pool.emplace_back([&, r, batchEnd]() {
                RunResult& res = results[r];
                res.seed = seed + (unsigned)r;
                Rng rng(res.seed);
                Placement P(base);
                AnnealOptions opt;
                opt.keepBest = keepBest;
                opt.effort = effort;
                opt.start = start;
                opt.deadline = batchEnd - reserve;
                opt.verbose = verbose;
                opt.runId = r;
                const bool warm = r < runs && r >= runs - warmRuns;
                opt.t0Scale = warm ? kWarmT0Scale : t0Scale;
                opt.r0 = warm ? kWarmR0 : r0;
                opt.adaptive = adaptive;
                opt.expectedTemps = expectedTempsFor(opt.t0Scale);
                opt.maxEffort = kMaxEffort;
                if (cl.ok) {
                    // Clustered phase, then uncluster and continue cold on the flat netlist.
                    // With --coarse-starts C > 1 the clustered anneal is repeated C times,
                    // each with 1/C of the coarse time (or effort), and the best coarse
                    // state continues. The coarse phase decides the global arrangement
                    // (on FPGA01 the final basin is predictable from the coarse HPWL),
                    // so samples are cheapest there. Attempt 0 uses the run's own RNG,
                    // so C = 1 is exactly the single-attempt flow.
                    AnnealOptions co = opt;
                    if (coarseEffort > 0) co.effort = coarseEffort;
                    co.effort /= coarseStarts;
                    const double now = secondsSince(start);
                    const double coarseEnd = opt.deadline < 1e17 ? now + coarseShare * (opt.deadline - now) : 1e18;
                    std::unique_ptr<Placement> bestC;
                    AnnealStats cs;
                    for (int i = 0; i < coarseStarts; i++) {
                        auto Pc = std::make_unique<Placement>(cl.coarse);
                        if (coarseEnd < 1e17) {
                            const double t = secondsSince(start);
                            co.deadline = t + (coarseEnd - t) / (coarseStarts - i);
                        }
                        Rng alt(res.seed * 0x9E3779B97F4A7C15ULL + (unsigned)i);
                        AnnealStats s = anneal(*Pc, i == 0 ? rng : alt, co);
                        if (coarseStarts > 1)
                            std::fprintf(stderr, "run %d: coarse attempt %d HPWL %lld (temps %d)  t=%.1fs\n", r, i,
                                         Pc->cost, s.temperatures, secondsSince(start));
                        if (!bestC || Pc->cost < bestC->cost) bestC = std::move(Pc), cs = s;
                    }
                    const Placement& Pc = *bestC;
                    std::vector<int> fx, fy;
                    cl.uncluster(D, Pc.x, Pc.y, fx, fy);
                    P.setPositions(fx, fy);
                    std::fprintf(stderr, "run %d: coarse HPWL %lld (temps %d) -> unclustered HPWL %lld  t=%.1fs\n", r,
                                 Pc.cost, cs.temperatures, P.cost, secondsSince(start));
                    opt.t0Scale = fineT0;
                    opt.r0 = fineR0;
                    opt.expectedTemps = kFineExpectedTemperatures;
                }
                res.st = anneal(P, rng, opt);
                opt.deadline = batchEnd;
                res.refineGain = refine(P, rng, opt, 20);
                if (windowPasses > 0) res.windowGain = windowOpt(P, opt, windowRows, windowCols, windowPasses);
                res.cost = P.cost;
                res.x = P.x;
                res.y = P.y;
                std::fprintf(stderr, "run %d%s (seed %u): HPWL %lld  temps %d  moves %lld  eff.effort %.3f  refine -%lld  window -%lld%s  t=%.1fs\n",
                             r, warm ? " warm" : "", res.seed, res.cost, res.st.temperatures, res.st.moves, res.st.effectiveEffort, res.refineGain, res.windowGain,
                             res.st.hitDeadline ? "  [deadline hit: not reproducible]" : "",
                             secondsSince(start));
            });
        }
        for (auto& t : pool) t.join();
    };
    double lastBatch = 0;
    for (int b = 0; b < batches; b++) {
        const double tb = secondsSince(start);
        runBatch(b * threads, std::min(runs, (b + 1) * threads), t0 + (budget - t0) * (b + 1) / batches);
        lastBatch = secondsSince(start) - tb;
    }
    if (adaptive && effort >= kMaxEffort) {
        const double limit = t0 + 0.9 * (budget - t0);
        int extra = 0;
        while ((int)results.size() + threads <= maxRuns && secondsSince(start) + 1.5 * lastBatch <= limit) {
            const int first = (int)results.size();
            results.resize(first + threads);
            const double tb = secondsSince(start);
            runBatch(first, first + threads, t0 + (budget - t0));
            lastBatch = secondsSince(start) - tb;
            extra += threads;
        }
        if (extra > 0)
            std::fprintf(stderr, "effort capped at %.0f: %d extra runs in the unused budget (%d in total)\n", kMaxEffort,
                         extra, (int)results.size());
    }
    runs = (int)results.size();

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
