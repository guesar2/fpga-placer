// cluster.cpp — Heavy-edge clustering and construction of the coarse design.
#include "cluster.hpp"

#include "analytic.hpp"

#include <algorithm>
#include <cstdio>

void Clustering::uncluster(const Design& D, const std::vector<int>& cx, const std::vector<int>& cy,
                           std::vector<int>& fx, std::vector<int>& fy) const {
    fx = D.initX;
    fy = D.initY;
    for (int c = 0; c < coarse.numCells(); c++) {
        const int n = memStart[c + 1] - memStart[c];
        const bool block = clustered[coarse.cellType[c]] && !coarse.cellFixed[c];
        for (int j = 0; j < n; j++) {
            const int m = members[memStart[c] + j];
            fx[m] = cx[c];
            fy[m] = block ? cy[c] - k / 2 + j : cy[c];
        }
    }
}

Clustering buildClustering(const Design& D, const std::vector<int>& fineX, const std::vector<int>& fineY,
                           const ClusterOptions& opt, Rng& rng, bool verbose) {
    Clustering C;
    C.k = std::max(1, opt.k);
    const int k = C.k, T = (int)D.typeNames.size(), N = D.numCells();
    if (k < 2) return C;

    // ---- which types to cluster ------------------------------------------------
    std::vector<int> movableOfType(T, 0);
    for (int c : D.movable) movableOfType[D.cellType[c]]++;
    C.clustered.assign(T, 0);
    bool any = false;
    for (int t = 0; t < T; t++)
        if (D.sites[t].rowGap == 1 && movableOfType[t] >= opt.minTypeCells) C.clustered[t] = 1, any = true;
    if (!any) return C;

    // ---- blocks: k consecutive free sites in one column, anchored at the middle --
    std::vector<char> fixedAt((size_t)D.W * D.H, 0);
    for (int c = 0; c < N; c++)
        if (D.cellFixed[c]) fixedAt[(size_t)fineX[c] * D.H + fineY[c]] = 1;
    Design& G = C.coarse;
    G.W = D.W;
    G.H = D.H;
    G.typeNames = D.typeNames;
    G.siteType.assign((size_t)D.W * D.H, -1);
    std::vector<long long> blocksOfType(T, 0);
    for (int t = 0; t < T; t++) {
        const TypeSites& ts = D.sites[t];
        for (size_t ci = 0; ci < ts.xs.size(); ci++) {
            const int x = ts.xs[ci];
            const auto& ys = ts.ys[ci];
            if (!C.clustered[t]) {
                for (int y : ys) G.siteType[(size_t)x * D.H + y] = (int8_t)t;
                continue;
            }
            size_t i = 0;
            while (i < ys.size()) {  // maximal run of consecutive, non-fixed sites
                size_t j = i;
                while (j < ys.size() && !fixedAt[(size_t)x * D.H + ys[j]] &&
                       (j == i || ys[j] == ys[j - 1] + 1))
                    j++;
                if (j == i) { i++; continue; }
                for (size_t s = i; s + k <= j; s += k) {
                    G.siteType[(size_t)x * D.H + ys[s] + k / 2] = (int8_t)t;
                    blocksOfType[t]++;
                }
                i = j;
            }
        }
    }

    // ---- heavy-edge matching, repeated until clusters reach k cells -------------
    std::vector<int> root(N);
    std::vector<std::vector<int>> mem(N);
    for (int c = 0; c < N; c++) root[c] = c, mem[c] = {c};
    auto eligible = [&](int c) { return !D.cellFixed[c] && C.clustered[D.cellType[c]]; };
    std::vector<double> score(N, 0.0);
    std::vector<int> touched;
    std::vector<int> matchedPass(N, -1);
    int passes = 0;
    for (int need = 1; need < k; need *= 2) passes++;
    passes += 1;  // one extra pass to fill clusters that are still below k
    for (int pass = 0; pass < passes; pass++) {
        std::vector<int> order;
        for (int c = 0; c < N; c++)
            if (eligible(c) && root[c] == c && (int)mem[c].size() < k) order.push_back(c);
        for (size_t i = order.size(); i > 1; i--) std::swap(order[i - 1], order[rng.below((uint32_t)i)]);
        int merges = 0;
        for (int c : order) {
            if (root[c] != c || matchedPass[c] == pass) continue;
            const int sc = (int)mem[c].size();
            touched.clear();
            for (int m : mem[c])
                for (int i = D.cellNetStart[m]; i < D.cellNetStart[m + 1]; i++) {
                    const int n = D.cellNets[i], deg = D.netDegree(n);
                    if (deg > opt.maxNetDegree) continue;
                    const double w = 1.0 / (deg - 1);
                    for (int j = D.netStart[n]; j < D.netStart[n + 1]; j++) {
                        const int r = root[D.netPins[j]];
                        if (r == c || !eligible(r) || D.cellType[r] != D.cellType[c] || matchedPass[r] == pass ||
                            sc + (int)mem[r].size() > k)
                            continue;
                        if (score[r] == 0.0) touched.push_back(r);
                        score[r] += w;
                    }
                }
            int best = -1;
            double bs = 0;
            for (int r : touched) {
                // Prefer the heaviest connection; break ties toward smaller clusters.
                double s = score[r] / std::max(1.0, 0.5 * (double)mem[r].size());
                if (s > bs) bs = s, best = r;
                score[r] = 0.0;
            }
            if (best < 0) continue;
            for (int m : mem[best]) root[m] = c;
            mem[c].insert(mem[c].end(), mem[best].begin(), mem[best].end());
            mem[best].clear();
            mem[best].shrink_to_fit();
            matchedPass[c] = matchedPass[best] = pass;
            merges++;
        }
        if (merges == 0) break;
    }

    // ---- coarse cells -------------------------------------------------------------
    C.clusterOf.assign(N, -1);
    C.memStart.push_back(0);
    std::vector<long long> clustersOfType(T, 0);
    for (int c = 0; c < N; c++) {
        if (root[c] != c) continue;
        const int id = G.numCells();
        G.cellName.push_back("cl" + std::to_string(id));
        G.cellType.push_back(D.cellType[c]);
        G.cellFixed.push_back(D.cellFixed[c]);
        G.initX.push_back(fineX[c]);
        G.initY.push_back(fineY[c]);
        if (!D.cellFixed[c]) G.movable.push_back(id);
        if (eligible(c)) clustersOfType[D.cellType[c]]++;
        for (int m : mem[c]) C.clusterOf[m] = id, C.members.push_back(m);
        C.memStart.push_back((int)C.members.size());
    }
    for (int t = 0; t < T; t++)
        if (C.clustered[t] && clustersOfType[t] > blocksOfType[t]) {
            if (verbose)
                std::fprintf(stderr, "clustering: %lld clusters of %s > %lld blocks; using flat flow\n",
                             clustersOfType[t], D.typeNames[t].c_str(), blocksOfType[t]);
            return C;  // not enough blocks (very dense design): ok stays false
        }
    G.buildSiteIndex();

    // ---- coarse nets: map pins to clusters, drop duplicates and internal nets ----
    std::vector<int> seen(G.numCells(), -1);
    G.netStart.push_back(0);
    for (int n = 0; n < D.numNets(); n++) {
        const size_t before = G.netPins.size();
        for (int j = D.netStart[n]; j < D.netStart[n + 1]; j++) {
            const int c = C.clusterOf[D.netPins[j]];
            if (seen[c] != n) seen[c] = n, G.netPins.push_back(c);
        }
        if (G.netPins.size() - before < 2) G.netPins.resize(before);
        else G.netStart.push_back((int)G.netPins.size());
    }
    G.buildCellNets();

    // ---- initial coarse placement: clusters legalized near their centroid ---------
    std::vector<double> fx(G.numCells()), fy(G.numCells());
    for (int c = 0; c < G.numCells(); c++) {
        double sx = 0, sy = 0;
        const int n = C.memStart[c + 1] - C.memStart[c];
        for (int j = C.memStart[c]; j < C.memStart[c + 1]; j++) sx += fineX[C.members[j]], sy += fineY[C.members[j]];
        fx[c] = sx / n;
        fy[c] = sy / n;
    }
    std::vector<int> lx, ly;
    legalize(G, fx, fy, lx, ly);
    G.initX = lx;
    G.initY = ly;
    C.ok = true;
    if (verbose) {
        for (int t = 0; t < T; t++)
            if (C.clustered[t])
                std::fprintf(stderr, "clustering %s: %d cells -> %lld clusters (avg %.2f), %lld blocks of %d\n",
                             D.typeNames[t].c_str(), movableOfType[t], clustersOfType[t],
                             (double)movableOfType[t] / std::max(1LL, clustersOfType[t]), blocksOfType[t], k);
        std::fprintf(stderr, "coarse design: %d cells (%zu movable), %d nets (fine: %d cells, %d nets)\n",
                     G.numCells(), G.movable.size(), G.numNets(), D.numCells(), D.numNets());
    }
    return C;
}
