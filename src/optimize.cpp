// optimize.cpp — Simulated annealing (VPR-style adaptive schedule) and refinement.
//
// Schedule follows Betz & Rose, "VPR: A New Packing, Placement and Routing Tool
// for FPGA Research" (FPL 1997):
//   * T0 = 20 * std-dev of the cost change of random moves
//   * moves per temperature = inner_num * N^(4/3)
//   * cooling factor chosen from the acceptance rate
//   * range limit adapted to keep the acceptance rate near 0.44
//   * stop when T < 0.005 * cost / #nets
#include "optimize.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

double secondsSince(Clock::time_point t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

bool proposeTarget(const Placement& P, int a, int R, Rng& rng, int& tx, int& ty) {
    const Design& D = P.D;
    const TypeSites& ts = D.sites[D.cellType[a]];
    const int ax = P.x[a], ay = P.y[a];
    const int Rx = std::max(R, ts.colGap), Ry = std::max(R, ts.rowGap);
    auto lo = std::lower_bound(ts.xs.begin(), ts.xs.end(), ax - Rx);
    auto hi = std::upper_bound(ts.xs.begin(), ts.xs.end(), ax + Rx);
    const int ncol = (int)(hi - lo);
    if (ncol <= 0) return false;
    for (int tries = 0; tries < 8; tries++) {
        int ci = (int)(lo - ts.xs.begin()) + (int)rng.below(ncol);
        const std::vector<int>& col = ts.ys[ci];
        auto ylo = std::lower_bound(col.begin(), col.end(), ay - Ry);
        auto yhi = std::upper_bound(col.begin(), col.end(), ay + Ry);
        int ny = (int)(yhi - ylo);
        if (ny <= 0) continue;
        tx = ts.xs[ci];
        ty = *(ylo + rng.below(ny));
        if (tx == ax && ty == ay) continue;
        int o = P.occ[P.site(tx, ty)];
        if (o >= 0 && D.cellFixed[o]) continue;
        return true;
    }
    return false;
}

double measureMoveRate(Placement& P, Rng& rng, double seconds) {
    const auto& mov = P.D.movable;
    if (mov.empty()) return 1e9;
    const int R = std::max(P.D.W, P.D.H);
    auto t0 = Clock::now();
    long long n = 0;
    volatile long long sink = 0;
    while (true) {
        for (int k = 0; k < 1024; k++) {
            int a = mov[rng.below((uint32_t)mov.size())], tx, ty;
            if (proposeTarget(P, a, R, rng, tx, ty)) sink = sink + P.evaluate(a, tx, ty);
        }
        n += 1024;
        if (secondsSince(t0) >= seconds) break;
    }
    return n / secondsSince(t0);
}

AnnealStats anneal(Placement& P, Rng& rng, const AnnealOptions& opt) {
    AnnealStats st;
    const Design& D = P.D;
    const auto& mov = P.D.movable;
    const int N = (int)mov.size();
    if (N == 0 || D.numNets() == 0) return st;
    const int Rmax = std::max(D.W, D.H);
    double rlim = std::max(1.0, opt.r0 * Rmax);

    // Initial temperature from the spread of random-move cost changes.
    double T;
    {
        int samples = std::min(std::max(N, 1000), 50000);
        double sum = 0, sum2 = 0;
        int k = 0;
        for (int i = 0; i < samples; i++) {
            int a = mov[rng.below(N)], tx, ty;
            if (!proposeTarget(P, a, (int)rlim, rng, tx, ty)) continue;
            double d = (double)P.evaluate(a, tx, ty);
            sum += d; sum2 += d * d; k++;
        }
        double mean = k ? sum / k : 0, var = k ? sum2 / k - mean * mean : 0;
        T = opt.t0Scale * std::sqrt(std::max(var, 1.0));
    }
    const double nPow = std::pow((double)N, 4.0 / 3.0);
    long long movesPerT = std::max(100LL, (long long)(opt.effort * nPow));
    const long long maxMovesPerT = std::max(movesPerT, (long long)(opt.maxEffort * nPow));

    auto tryMove = [&](double temp, int R) -> bool {
        int a = mov[rng.below(N)], tx, ty;
        if (!proposeTarget(P, a, R, rng, tx, ty)) return false;
        long long d = P.evaluate(a, tx, ty);
        if (d <= 0 || (temp > 0 && rng.uniform() < std::exp(-(double)d / temp))) {
            P.commit();
            return true;
        }
        return false;
    };

    // Adaptive mode: (time, moves) at the start of recent temperature steps,
    // used to measure the current move rate (late, local moves are faster).
    const int kRateWindow = 8;
    std::vector<double> histT;
    std::vector<long long> histM;

    while (true) {
        if (opt.adaptive) {
            const double now = secondsSince(opt.start);
            histT.push_back(now);
            histM.push_back(st.moves);
            const int h = (int)histT.size();
            if (h >= 3) {
                const int from = std::max(0, h - 1 - kRateWindow);
                const double dt = now - histT[from];
                const double rate = dt > 0 ? (double)(st.moves - histM[from]) / dt : 0;
                // Remaining steps from the measured typical count. (A geometric
                // estimate ln(T/Tstop)/ln(1/0.95) was tried and overestimates by
                // ~2x late in the schedule, where the cooling factor drops to 0.8,
                // which starved the middle of the schedule.) Floor at 5% of the
                // prior if the run is longer than usual; +1 for the greedy pass.
                const double rem = std::max(opt.expectedTemps - st.temperatures, 0.05 * opt.expectedTemps) + 1.0;
                const double left = 0.97 * (opt.deadline - now);
                if (rate > 0 && left > 0)
                    movesPerT = std::min(maxMovesPerT, std::max(100LL, (long long)(rate * left / rem)));
            }
        }
        long long acc = 0, m = 0;
        const int R = (int)rlim;
        for (; m < movesPerT; m++) {
            if (tryMove(T, R)) acc++;
            if ((m & 4095) == 0 && secondsSince(opt.start) > opt.deadline) { st.hitDeadline = true; break; }
        }
        st.moves += m;
        st.temperatures++;
        if (st.hitDeadline) break;
        const double ar = (double)acc / (double)movesPerT;
        const double alpha = ar > 0.96 ? 0.5 : ar > 0.8 ? 0.9 : ar > 0.15 ? 0.95 : 0.8;
        T *= alpha;
        rlim = std::min((double)Rmax, std::max(1.0, rlim * (1.0 - 0.44 + ar)));
        if (opt.verbose && st.temperatures % 10 == 0)
            std::fprintf(stderr, "[run %d] T#%d  T=%.4g  hpwl=%lld  acc=%.3f  rlim=%.1f  moves/T=%lld  t=%.1fs\n",
                         opt.runId, st.temperatures, T, P.cost, ar, rlim, movesPerT, secondsSince(opt.start));
        if (T < 0.005 * (double)P.cost / D.numNets()) break;
    }
    // Final zero-temperature pass (greedy) with the smallest window.
    for (long long m = 0; m < movesPerT; m++) {
        tryMove(0.0, 1);
        if ((m & 4095) == 0 && secondsSince(opt.start) > opt.deadline) break;
    }
    st.effectiveEffort = st.temperatures ? (double)st.moves / (st.temperatures * nPow) : 0;
    return st;
}

long long refine(Placement& P, Rng& rng, const AnnealOptions& opt, int maxPasses) {
    const Design& D = P.D;
    std::vector<int> order = D.movable;
    std::vector<int> xs, ys;
    const long long before = P.cost;
    const int kRegionMaxDegree = 256;  // huge nets barely constrain the region; skip for speed

    for (int pass = 0; pass < maxPasses; pass++) {
        const long long passStart = P.cost;
        for (size_t i = order.size(); i > 1; i--) std::swap(order[i - 1], order[rng.below((uint32_t)i)]);
        for (size_t oi = 0; oi < order.size(); oi++) {
            if ((oi & 1023) == 0 && secondsSince(opt.start) > opt.deadline) return before - P.cost;
            const int a = order[oi];
            xs.clear(); ys.clear();
            for (int i = D.cellNetStart[a]; i < D.cellNetStart[a + 1]; i++) {
                int n = D.cellNets[i];
                if (D.netDegree(n) > kRegionMaxDegree) continue;
                int x0 = INT_MAX, x1 = INT_MIN, y0 = INT_MAX, y1 = INT_MIN;
                for (int j = D.netStart[n]; j < D.netStart[n + 1]; j++) {
                    int c = D.netPins[j];
                    if (c == a) continue;
                    x0 = std::min(x0, P.x[c]); x1 = std::max(x1, P.x[c]);
                    y0 = std::min(y0, P.y[c]); y1 = std::max(y1, P.y[c]);
                }
                xs.push_back(x0); xs.push_back(x1); ys.push_back(y0); ys.push_back(y1);
            }
            if (xs.empty()) continue;
            std::sort(xs.begin(), xs.end());
            std::sort(ys.begin(), ys.end());
            const size_t k = xs.size();  // even
            const int xl = xs[k / 2 - 1], xh = xs[k / 2], yl = ys[k / 2 - 1], yh = ys[k / 2];
            const int ax = P.x[a], ay = P.y[a];
            if (ax >= xl && ax <= xh && ay >= yl && ay <= yh) continue;  // already optimal
            const int cx = std::min(std::max(ax, xl), xh), cy = std::min(std::max(ay, yl), yh);

            // Candidate sites of the right type nearest to (cx,cy).
            const TypeSites& ts = D.sites[D.cellType[a]];
            int ci = (int)(std::lower_bound(ts.xs.begin(), ts.xs.end(), cx) - ts.xs.begin());
            long long best = 0;
            int bx = -1, by = -1;
            for (int c = std::max(0, ci - 2); c < std::min((int)ts.xs.size(), ci + 2); c++) {
                const std::vector<int>& col = ts.ys[c];
                int yi = (int)(std::lower_bound(col.begin(), col.end(), cy) - col.begin());
                for (int j = std::max(0, yi - 2); j < std::min((int)col.size(), yi + 2); j++) {
                    int tx = ts.xs[c], ty = col[j];
                    if (tx == ax && ty == ay) continue;
                    int o = P.occ[P.site(tx, ty)];
                    if (o >= 0 && D.cellFixed[o]) continue;
                    long long d = P.evaluate(a, tx, ty);
                    if (d < best) { best = d; bx = tx; by = ty; }
                }
            }
            if (bx >= 0) { P.evaluate(a, bx, by); P.commit(); }
        }
        if (opt.verbose)
            std::fprintf(stderr, "[run %d] refine pass %d: hpwl=%lld (-%lld)\n", opt.runId, pass + 1, P.cost,
                         passStart - P.cost);
        if (passStart - P.cost <= passStart / 100000) break;  // < 0.001% gain
    }
    return before - P.cost;
}
