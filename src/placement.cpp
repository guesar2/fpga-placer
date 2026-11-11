// placement.cpp — Incremental bounding-box bookkeeping and legality checks.
#include "placement.hpp"

#include <string>

Placement::Placement(const Design& d) : D(d) {
    stamp_.assign(D.numNets(), 0);
    setPositions(D.initX, D.initY);
}

void Placement::setPositions(const std::vector<int>& nx, const std::vector<int>& ny) {
    x = nx;
    y = ny;
    occ.assign((size_t)D.W * D.H, -1);
    for (int c = 0; c < D.numCells(); c++) occ[site(x[c], y[c])] = c;
    box.resize(D.numNets());
    cost = 0;
    for (int n = 0; n < D.numNets(); n++) {
        rebuildBox(n, box[n], -1, 0, 0);
        cost += box[n].hpwl();
    }
}

// Recomputes the box of net n from cell positions; if movedCell >= 0 its
// position is taken as (mx,my) instead of the stored one.
void Placement::rebuildBox(int n, BBox& b, int movedCell, int mx, int my) const {
    b.xmin = b.ymin = INT_MAX;
    b.xmax = b.ymax = INT_MIN;
    b.nxmin = b.nxmax = b.nymin = b.nymax = 0;
    for (int i = D.netStart[n]; i < D.netStart[n + 1]; i++) {
        int c = D.netPins[i];
        int px = c == movedCell ? mx : x[c], py = c == movedCell ? my : y[c];
        if (px < b.xmin) { b.xmin = px; b.nxmin = 1; } else if (px == b.xmin) b.nxmin++;
        if (px > b.xmax) { b.xmax = px; b.nxmax = 1; } else if (px == b.xmax) b.nxmax++;
        if (py < b.ymin) { b.ymin = py; b.nymin = 1; } else if (py == b.ymin) b.nymin++;
        if (py > b.ymax) { b.ymax = py; b.nymax = 1; } else if (py == b.ymax) b.nymax++;
    }
}

// Moves one coordinate of a pin from o to v inside [mn,mx] with edge counts.
// Returns false when the result is unknown without a rescan.
static inline bool axisMove(int o, int v, int& mn, int& mx, int& cmn, int& cmx) {
    if (o == v) return true;
    if (o == mn) { if (cmn == 1 && v > mn) return false; cmn--; }
    if (o == mx) { if (cmx == 1 && v < mx) return false; cmx--; }
    if (v < mn) { mn = v; cmn = 1; } else if (v == mn) cmn++;
    if (v > mx) { mx = v; cmx = 1; } else if (v == mx) cmx++;
    return cmn > 0 && cmx > 0;
}

bool Placement::movePin(BBox& b, int ox, int oy, int nx, int ny) const {
    return axisMove(ox, nx, b.xmin, b.xmax, b.nxmin, b.nxmax) &&
           axisMove(oy, ny, b.ymin, b.ymax, b.nymin, b.nymax);
}

long long Placement::evaluate(int a, int tx, int ty) {
    pend_.clear();
    int ax = x[a], ay = y[a];
    int b = occ[site(tx, ty)];
    pa_ = a; pb_ = b; pax_ = ax; pay_ = ay; ptx_ = tx; pty_ = ty;

    // Nets containing both a and b keep the same pin coordinates (a swap just
    // exchanges two pins), so they are skipped. stamp s = net of a, s+1 = shared.
    stampVal_ += 2;
    const int s = stampVal_;
    for (int i = D.cellNetStart[a]; i < D.cellNetStart[a + 1]; i++) stamp_[D.cellNets[i]] = s;
    if (b >= 0)
        for (int i = D.cellNetStart[b]; i < D.cellNetStart[b + 1]; i++) {
            int n = D.cellNets[i];
            if (stamp_[n] == s) stamp_[n] = s + 1;
        }

    long long delta = 0;
    auto handle = [&](int cell, int n, int ox, int oy, int nx, int ny) {
        BBox nb = box[n];
        if (!movePin(nb, ox, oy, nx, ny)) rebuildBox(n, nb, cell, nx, ny);
        delta += nb.hpwl() - box[n].hpwl();
        pend_.push_back({n, nb});
    };
    for (int i = D.cellNetStart[a]; i < D.cellNetStart[a + 1]; i++) {
        int n = D.cellNets[i];
        if (stamp_[n] != s + 1) handle(a, n, ax, ay, tx, ty);
    }
    if (b >= 0)
        for (int i = D.cellNetStart[b]; i < D.cellNetStart[b + 1]; i++) {
            int n = D.cellNets[i];
            if (stamp_[n] != s + 1) handle(b, n, tx, ty, ax, ay);
        }
    pdelta_ = delta;
    return delta;
}

void Placement::commit() {
    for (const Pending& p : pend_) box[p.net] = p.nb;
    x[pa_] = ptx_; y[pa_] = pty_;
    occ[site(ptx_, pty_)] = pa_;
    occ[site(pax_, pay_)] = pb_;
    if (pb_ >= 0) { x[pb_] = pax_; y[pb_] = pay_; }
    cost += pdelta_;
    pend_.clear();
}

long long Placement::fullHpwl() const {
    long long t = 0;
    BBox b;
    for (int n = 0; n < D.numNets(); n++) { rebuildBox(n, b, -1, 0, 0); t += b.hpwl(); }
    return t;
}

bool Placement::isLegal(std::string* why) const {
    std::vector<int> used((size_t)D.W * D.H, -1);
    for (int c = 0; c < D.numCells(); c++) {
        auto fail = [&](const std::string& m) { if (why) *why = D.cellName[c] + ": " + m; return false; };
        if (x[c] < 0 || y[c] < 0 || x[c] >= D.W || y[c] >= D.H) return fail("out of grid");
        int s = site(x[c], y[c]);
        if (D.siteType[s] != D.cellType[c]) return fail("site type mismatch / invalid site");
        if (used[s] >= 0) return fail("overlaps " + D.cellName[used[s]]);
        used[s] = c;
        if (D.cellFixed[c] && (x[c] != D.initX[c] || y[c] != D.initY[c])) return fail("fixed cell moved");
    }
    return true;
}
