// placement.hpp — Mutable placement state with incremental HPWL.
//
// For every net we keep its bounding box plus how many pins lie on each of the
// four edges. Moving one pin then updates the box in O(1), except when the only
// pin on an edge moves inward; only then is the net rescanned (O(degree)).
//
// Moves are evaluated without modifying the state (evaluate()), and applied
// only if accepted (commit()), so rejected moves cost one evaluation, not two.
#pragma once
#include "design.hpp"

#include <climits>
#include <vector>

struct BBox {
    int xmin, xmax, ymin, ymax;  // box
    int nxmin, nxmax, nymin, nymax;  // pins sitting on each edge
    long long hpwl() const { return (long long)(xmax - xmin) + (ymax - ymin); }
};

class Placement {
public:
    explicit Placement(const Design& d);

    const Design& D;
    std::vector<int> x, y;   // position of each cell
    std::vector<int> occ;    // site (x*H+y) -> cell id, -1 if empty
    std::vector<BBox> box;   // per net
    long long cost = 0;      // total HPWL

    int site(int sx, int sy) const { return sx * D.H + sy; }

    void setPositions(const std::vector<int>& nx, const std::vector<int>& ny);  // rebuilds everything
    long long fullHpwl() const;           // from scratch, for verification
    bool isLegal(std::string* why) const; // checks every rule of the project spec

    // Proposed move: cell `a` goes to (tx,ty). If that site holds movable cell b,
    // b goes to a's old site (swap). Returns the HPWL change. Must be followed by
    // commit() or by another evaluate() (which discards this one).
    long long evaluate(int a, int tx, int ty);
    void commit();

private:
    struct Pending { int net; BBox nb; };
    std::vector<Pending> pend_;
    std::vector<int> stamp_;
    int stampVal_ = 0;
    int pa_ = -1, pb_ = -1, pax_, pay_, ptx_, pty_;
    long long pdelta_ = 0;

    void rebuildBox(int n, BBox& b, int movedCell, int mx, int my) const;
    bool movePin(BBox& b, int ox, int oy, int nx, int ny) const;
};
