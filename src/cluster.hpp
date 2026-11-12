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

/// Settings of the clustering (CLI: --cluster K).
struct ClusterOptions {
    int k = 4;                ///< Maximum cells per cluster (= block height in sites).
    int maxNetDegree = 16;    ///< Nets with more pins are ignored when scoring cell pairs.
    int minTypeCells = 1000;  ///< A type is clustered only if it has at least this many movable cells.
};

/// Result of clustering: the coarse design plus the maps between fine cells
/// and clusters.
struct Clustering {
    Design coarse;                       ///< Clusters as cells, block anchor sites as sites.
    std::vector<int> clusterOf;          ///< Fine cell id -> coarse cell id.
    std::vector<int> memStart, members;  ///< Coarse cell -> its fine cells, in CSR form.
    std::vector<char> clustered;         ///< Per type id: 1 if that type was clustered.
    int k = 1;                           ///< Block height actually used.
    bool ok = false;                     ///< false if clustering does not apply (use the flat flow).

    /// Converts a coarse placement into fine positions: the members of each
    /// cluster fill the sites of its block in member order, and singletons
    /// keep their site. The result is legal by construction.
    /// @param D      The fine design.
    /// @param cx,cy  Coordinates of every coarse cell.
    /// @param fx,fy  Receive the coordinates of every fine cell.
    void uncluster(const Design& D, const std::vector<int>& cx, const std::vector<int>& cy,
                   std::vector<int>& fx, std::vector<int>& fy) const;
};

/// Groups cells by heavy-edge matching (score: sum of 1/(|e|-1) over shared
/// nets) into single-type clusters of at most opt.k cells, and builds the
/// coarse design with an initial legal placement.
/// @param D            The fine design.
/// @param fineX,fineY  A fine placement (e.g. the analytical one); each cluster
///                     starts legalized near its members' centroid.
/// @param opt          Clustering settings.
/// @param rng          Random source for the order in which cells are matched.
/// @param verbose      Print clustering statistics to stderr.
/// @return The clustering; check `ok` before using it.
Clustering buildClustering(const Design& D, const std::vector<int>& fineX, const std::vector<int>& fineY,
                           const ClusterOptions& opt, Rng& rng, bool verbose);
