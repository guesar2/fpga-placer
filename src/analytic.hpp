// analytic.hpp — Quadratic (analytical) initial placement with anchor-based spreading.
//
// 1. Quadratic wirelength: 2-to-16-pin nets use a clique with weight 1/(k-1);
//    larger nets a star node with weight k/(k-1) (same optimum as the clique);
//    nets above kMaxDegree (clock/reset-like) are ignored. Fixed IO cells are
//    constants. x and y are solved separately with Jacobi-preconditioned CG
//    (the A x = b formulation in the course slides).
// 2. The unconstrained solution collapses around the few IOs, so we iterate
//    in the style of SimPL (Kim, Lee, Markov, ICCAD 2010): legalize the current
//    solution, add an anchor pulling each cell to its legal site with a growing
//    weight, re-solve. The best legal placement found is returned.
// 3. Legalization: cells of each type, in order of distance from their
//    centroid, take the nearest free compatible site (Manhattan distance),
//    found per column with "next free site" union-find arrays.
#pragma once
#include "placement.hpp"

#include <vector>

/// Settings of the analytical placement (CLI: --qp-iters, --qp-growth).
struct AnalyticOptions {
    int iterations = 24;        ///< Spreading iterations (legalize, anchor, re-solve).
    double anchor0 = 0.01;      ///< Anchor weight in the first spreading iteration.
    double anchorGrowth = 1.6;  ///< Anchor weight multiplier per iteration.
    bool verbose = false;       ///< Print the HPWL of every iteration to stderr.
};

/// Computes a quadratic placement with SimPL-style spreading and writes the
/// best legal result found into P. Fixed cells are not moved.
/// @param P   Placement to overwrite; its cost and boxes are rebuilt.
/// @param opt Spreading settings.
/// @return HPWL of the placement written into P.
long long analyticInit(Placement& P, const AnalyticOptions& opt);

/// Legalizes real-valued target positions: cells of each type, in order of
/// distance from their type's centroid, take the nearest free compatible site.
/// Fixed cells keep their coordinates.
/// @param D          Design whose cells are legalized.
/// @param fx,fy      Target coordinates of every cell.
/// @param outX,outY  Receive the legal integer coordinates of every cell.
void legalize(const Design& D, const std::vector<double>& fx, const std::vector<double>& fy,
              std::vector<int>& outX, std::vector<int>& outY);
