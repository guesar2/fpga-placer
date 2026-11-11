// cluster.hpp — Netlist clustering and a coarse (clustered) placement problem.
//
// Idea from clustering-driven placement (e.g. Kahng et al., DAC 2024): group
// strongly connected cells, place the much smaller clustered netlist, then
// break the clusters up and finish with a low-temperature flat anneal.
//
// FPGA-specific part: a cluster holds up to k cells of ONE resource type, and
// it is placed on a "block" = k vertically consecutive sites of that type in
// one column. Unclustering is then trivially legal: the members take the
// block's k sites. Blocks are anchored at their middle site, so the coarse
// problem is an ordinary Design (cells = clusters, sites = block anchors) and
// the existing annealer / legalizer run on it unchanged.
//
// Only types whose sites form contiguous column runs (rowGap 1) and that have
// many movable cells are clustered (CLB on the course benchmarks); all other
// cells are singleton clusters on their original sites.
#pragma once
#include "design.hpp"
#include "optimize.hpp"

#include <vector>

struct ClusterOptions {
    int k = 4;                 // max cells per cluster (= block height)
    int maxNetDegree = 16;     // nets above this size are ignored when scoring
    int minTypeCells = 1000;   // cluster a type only if it has this many movable cells
};

struct Clustering {
    Design coarse;                       // clusters as cells, block anchors as sites
    std::vector<int> clusterOf;          // fine cell -> coarse cell
    std::vector<int> memStart, members;  // coarse cell -> its fine cells (CSR)
    std::vector<char> clustered;         // per type: was it clustered?
    int k = 1;
    bool ok = false;                     // false: clustering not applicable (use flat flow)

    // Fine positions from a coarse placement: members of a cluster fill the
    // sites of its block (in member order). Singletons keep their site.
    void uncluster(const Design& D, const std::vector<int>& cx, const std::vector<int>& cy,
                   std::vector<int>& fx, std::vector<int>& fy) const;
};

// Builds the clustering. fineX/fineY (e.g. the analytical placement) give the
// initial coarse placement: each cluster is legalized near its members' centroid.
Clustering buildClustering(const Design& D, const std::vector<int>& fineX, const std::vector<int>& fineY,
                           const ClusterOptions& opt, Rng& rng, bool verbose);
