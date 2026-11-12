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

/// Axis-aligned bounding box of one net, with pin counts on each edge.
///
/// The edge counts make updates incremental: when a pin leaves an edge that
/// other pins still sit on, the box is unchanged and no rescan is needed.
struct BBox {
    int xmin, xmax, ymin, ymax;      ///< Box extent in site coordinates (inclusive).
    int nxmin, nxmax, nymin, nymax;  ///< Number of pins lying on each edge.

    /// Half-perimeter wirelength of the box: (xmax - xmin) + (ymax - ymin).
    long long hpwl() const { return (long long)(xmax - xmin) + (ymax - ymin); }
};

/// Mutable placement of a Design: cell positions, site occupancy and per-net
/// bounding boxes, with the total HPWL kept up to date incrementally.
///
/// Typical use is evaluate() to price a move, then commit() if it is accepted.
class Placement {
public:
    /// Creates a placement of `d` at the coordinates of the input .pl
    /// (Design::initX / initY) and builds occupancy, boxes and cost.
    /// @param d Design to place; must outlive this object.
    explicit Placement(const Design& d);

    const Design& D;          ///< The design being placed.
    std::vector<int> x, y;    ///< Site coordinates of each cell, indexed by cell id.
    std::vector<int> occ;     ///< Occupancy by site index (see site()): cell id, or -1 if empty.
    std::vector<BBox> box;    ///< Bounding box of each net, indexed by net id.
    long long cost = 0;       ///< Current total HPWL (sum of box[n].hpwl()).

    /// Linear index of site (sx, sy) into occ.
    int site(int sx, int sy) const { return sx * D.H + sy; }

    /// Places every cell at (nx[c], ny[c]) and rebuilds occupancy, boxes and cost.
    /// @param nx,ny Coordinates for every cell, fixed cells included.
    void setPositions(const std::vector<int>& nx, const std::vector<int>& ny);

    /// Recomputes total HPWL from scratch, ignoring cached boxes (for verification).
    /// @return Total HPWL; equals `cost` when the incremental state is consistent.
    long long fullHpwl() const;

    /// Checks every legality rule in the project spec: each cell on a site of
    /// its own type, no overlaps, fixed cells at their original coordinates.
    /// @param why If non-null and the placement is illegal, receives a reason.
    /// @return true if the placement is legal.
    bool isLegal(std::string* why) const;

    /// Computes the HPWL change of moving cell `a` to (tx, ty) without changing
    /// the state. If the target holds a movable cell b, the move is a swap and
    /// b goes to a's old site. Nets containing both a and b are skipped.
    /// @pre The target site is compatible with a's type and holds no fixed cell.
    /// @return HPWL change (negative means an improvement).
    /// @note Only the most recent evaluation is kept; another evaluate()
    ///       discards it, commit() applies it.
    long long evaluate(int a, int tx, int ty);

    /// Applies the move priced by the last evaluate(), updating positions,
    /// occupancy, boxes and cost.
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
