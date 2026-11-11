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

struct AnalyticOptions {
    int iterations = 24;          // spreading iterations
    double anchor0 = 0.01;        // first anchor weight
    double anchorGrowth = 1.6;    // anchor weight multiplier per iteration
    bool verbose = false;
};

// Writes a legal placement into P (fixed cells untouched). Returns its HPWL.
long long analyticInit(Placement& P, const AnalyticOptions& opt);

// Nearest-free-site legalization of real-valued targets (exposed for reuse).
void legalize(const Design& D, const std::vector<double>& fx, const std::vector<double>& fy,
              std::vector<int>& outX, std::vector<int>& outY);
