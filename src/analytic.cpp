// analytic.cpp — see analytic.hpp.
#include "analytic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace {

const int kCliqueMaxDegree = 16;
const int kMaxDegree = 1000;

// Sparse symmetric matrix in CSR form.
struct Csr {
    int n = 0;
    std::vector<int> start, col;
    std::vector<double> val, diag;
};

struct QpSystem {
    Csr A;                         // Laplacian of the movable variables
    std::vector<double> bx, by;    // contributions of fixed pins
    int numVars = 0;
    std::vector<int> varOf;        // cell -> variable or -1
};

QpSystem buildSystem(const Design& D, const Placement& P) {
    QpSystem S;
    S.varOf.assign(D.numCells(), -1);
    int nv = 0;
    for (int c : D.movable) S.varOf[c] = nv++;
    const int numCellVars = nv;
    // Star variables.
    std::vector<int> starOf(D.numNets(), -1);
    for (int n = 0; n < D.numNets(); n++) {
        int k = D.netDegree(n);
        if (k > kCliqueMaxDegree && k <= kMaxDegree) starOf[n] = nv++;
    }
    S.numVars = nv;
    S.bx.assign(nv, 0.0);
    S.by.assign(nv, 0.0);
    std::vector<double> diag(nv, 0.0);
    struct Trip { int r, c; double v; };
    std::vector<Trip> off;

    // Connects two "endpoints": each is a variable (>=0) or a fixed cell (-1 - cell).
    auto connect = [&](int u, int v, double w) {
        if (u >= 0 && v >= 0) {
            diag[u] += w; diag[v] += w;
            off.push_back({u, v, -w});
            off.push_back({v, u, -w});
        } else if (u >= 0 || v >= 0) {
            int var = u >= 0 ? u : v, fixedCell = u >= 0 ? -1 - v : -1 - u;
            diag[var] += w;
            S.bx[var] += w * P.x[fixedCell];
            S.by[var] += w * P.y[fixedCell];
        }
    };
    auto endpoint = [&](int cell) { return S.varOf[cell] >= 0 ? S.varOf[cell] : -1 - cell; };

    for (int n = 0; n < D.numNets(); n++) {
        const int k = D.netDegree(n);
        if (k > kMaxDegree) continue;
        const int* pins = &D.netPins[D.netStart[n]];
        if (k <= kCliqueMaxDegree) {
            double w = 1.0 / (k - 1);
            for (int i = 0; i < k; i++)
                for (int j = i + 1; j < k; j++) connect(endpoint(pins[i]), endpoint(pins[j]), w);
        } else {
            double w = (double)k / (k - 1);
            for (int i = 0; i < k; i++) connect(starOf[n], endpoint(pins[i]), w);
        }
    }
    // Tiny regularization so components without any fixed pin stay well-posed.
    const double eps = 1e-6;
    for (int v = 0; v < nv; v++) {
        diag[v] += eps;
        S.bx[v] += eps * D.W * 0.5;
        S.by[v] += eps * D.H * 0.5;
    }
    std::sort(off.begin(), off.end(), [](const Trip& a, const Trip& b) { return a.r != b.r ? a.r < b.r : a.c < b.c; });
    Csr& A = S.A;
    A.n = nv;
    A.diag = diag;
    A.start.assign(nv + 1, 0);
    for (size_t i = 0; i < off.size();) {
        size_t j = i;
        double v = 0;
        while (j < off.size() && off[j].r == off[i].r && off[j].c == off[i].c) v += off[j++].v;
        A.col.push_back(off[i].c);
        A.val.push_back(v);
        A.start[off[i].r + 1]++;
        i = j;
    }
    for (int r = 0; r < nv; r++) A.start[r + 1] += A.start[r];
    (void)numCellVars;
    return S;
}

// Solves (A + diag(extra)) x = b with Jacobi-preconditioned conjugate gradient.
void solveCg(const Csr& A, const std::vector<double>& extra, const std::vector<double>& b, std::vector<double>& x,
             int maxIter, double tol) {
    const int n = A.n;
    std::vector<double> r(n), z(n), p(n), q(n), dinv(n);
    for (int i = 0; i < n; i++) dinv[i] = 1.0 / (A.diag[i] + extra[i]);
    auto mul = [&](const std::vector<double>& v, std::vector<double>& out) {
        for (int i = 0; i < n; i++) {
            double s = (A.diag[i] + extra[i]) * v[i];
            for (int k = A.start[i]; k < A.start[i + 1]; k++) s += A.val[k] * v[A.col[k]];
            out[i] = s;
        }
    };
    mul(x, q);
    double bnorm = 0;
    for (int i = 0; i < n; i++) { r[i] = b[i] - q[i]; bnorm += b[i] * b[i]; }
    bnorm = std::sqrt(bnorm) + 1e-30;
    for (int i = 0; i < n; i++) { z[i] = dinv[i] * r[i]; p[i] = z[i]; }
    double rz = 0;
    for (int i = 0; i < n; i++) rz += r[i] * z[i];
    for (int it = 0; it < maxIter; it++) {
        mul(p, q);
        double pq = 0;
        for (int i = 0; i < n; i++) pq += p[i] * q[i];
        if (pq <= 0) break;
        double a = rz / pq, rn = 0;
        for (int i = 0; i < n; i++) { x[i] += a * p[i]; r[i] -= a * q[i]; rn += r[i] * r[i]; }
        if (std::sqrt(rn) / bnorm < tol) break;
        double rz2 = 0;
        for (int i = 0; i < n; i++) { z[i] = dinv[i] * r[i]; rz2 += r[i] * z[i]; }
        double beta = rz2 / rz;
        rz = rz2;
        for (int i = 0; i < n; i++) p[i] = z[i] + beta * p[i];
    }
}

// "Next free index" arrays over the sites of one column (path-compressed).
struct FreeColumn {
    std::vector<int> up, down;  // up: size len+1 (len = none); down: offset by one (0 = none)
    explicit FreeColumn(int len) : up(len + 1), down(len + 1) {
        std::iota(up.begin(), up.end(), 0);
        std::iota(down.begin(), down.end(), 0);
    }
    int findUp(int i) {  // first free index >= i, or len
        int r = i;
        while (up[r] != r) r = up[r];
        while (up[i] != r) { int nx = up[i]; up[i] = r; i = nx; }
        return r;
    }
    int findDown(int i) {  // last free index <= i, or -1
        int k = i + 1, r = k;
        while (down[r] != r) r = down[r];
        while (down[k] != r) { int nx = down[k]; down[k] = r; k = nx; }
        return r - 1;
    }
    void occupy(int j) { up[j] = j + 1; down[j + 1] = j; }
};

}  // namespace

void legalize(const Design& D, const std::vector<double>& fx, const std::vector<double>& fy,
              std::vector<int>& outX, std::vector<int>& outY) {
    outX = D.initX;
    outY = D.initY;
    for (int t = 0; t < (int)D.typeNames.size(); t++) {
        std::vector<int> cells;
        for (int c : D.movable) if (D.cellType[c] == t) cells.push_back(c);
        if (cells.empty()) continue;
        const TypeSites& ts = D.sites[t];
        std::vector<FreeColumn> cols;
        cols.reserve(ts.xs.size());
        for (auto& ys : ts.ys) cols.emplace_back((int)ys.size());
        for (int c = 0; c < D.numCells(); c++) {
            if (!D.cellFixed[c] || D.cellType[c] != t) continue;
            int ci = ts.colOf[D.initX[c]];
            const auto& ys = ts.ys[ci];
            int j = (int)(std::lower_bound(ys.begin(), ys.end(), D.initY[c]) - ys.begin());
            cols[ci].occupy(j);
        }
        double mx = 0, my = 0;
        for (int c : cells) { mx += fx[c]; my += fy[c]; }
        mx /= cells.size(); my /= cells.size();
        std::vector<double> key(D.numCells());
        for (int c : cells) key[c] = std::fabs(fx[c] - mx) + std::fabs(fy[c] - my);
        std::sort(cells.begin(), cells.end(), [&](int a, int b) { return key[a] != key[b] ? key[a] < key[b] : a < b; });

        for (int c : cells) {
            const double tx = fx[c], ty = fy[c];
            int right = (int)(std::lower_bound(ts.xs.begin(), ts.xs.end(), tx) - ts.xs.begin()), left = right - 1;
            double best = 1e300;
            int bc = -1, bj = -1;
            auto tryCol = [&](int ci) {
                const auto& ys = ts.ys[ci];
                double dx = std::fabs(ts.xs[ci] - tx);
                int yi = (int)(std::lower_bound(ys.begin(), ys.end(), ty) - ys.begin());
                int u = cols[ci].findUp(yi);
                if (u < (int)ys.size()) { double d = dx + std::fabs(ys[u] - ty); if (d < best) { best = d; bc = ci; bj = u; } }
                int dn = yi > 0 ? cols[ci].findDown(yi - 1) : -1;
                if (dn >= 0) { double d = dx + std::fabs(ys[dn] - ty); if (d < best) { best = d; bc = ci; bj = dn; } }
            };
            while (left >= 0 || right < (int)ts.xs.size()) {
                double dl = left >= 0 ? tx - ts.xs[left] : 1e300;
                double dr = right < (int)ts.xs.size() ? ts.xs[right] - tx : 1e300;
                if (std::min(dl, dr) >= best) break;
                if (dl <= dr) tryCol(left--); else tryCol(right++);
            }
            if (bc < 0) continue;  // impossible when #cells <= #sites
            cols[bc].occupy(bj);
            outX[c] = ts.xs[bc];
            outY[c] = ts.ys[bc][bj];
        }
    }
}

long long analyticInit(Placement& P, const AnalyticOptions& opt) {
    const Design& D = P.D;
    QpSystem S = buildSystem(D, P);
    const int nv = S.numVars;
    std::vector<double> x(nv, D.W * 0.5), y(nv, D.H * 0.5), extra(nv, 0.0), bx, by;
    std::vector<double> fx(D.numCells()), fy(D.numCells());
    std::vector<int> lx, ly, bestX = P.x, bestY = P.y;
    long long bestCost = P.cost;

    auto toCells = [&]() {
        for (int c = 0; c < D.numCells(); c++) {
            int v = S.varOf[c];
            fx[c] = v >= 0 ? std::min(std::max(x[v], 0.0), D.W - 1.0) : P.x[c];
            fy[c] = v >= 0 ? std::min(std::max(y[v], 0.0), D.H - 1.0) : P.y[c];
        }
    };
    solveCg(S.A, extra, S.bx, x, 1000, 1e-6);
    solveCg(S.A, extra, S.by, y, 1000, 1e-6);
    double alpha = opt.anchor0;
    for (int it = 0; it <= opt.iterations; it++) {
        toCells();
        legalize(D, fx, fy, lx, ly);
        P.setPositions(lx, ly);
        if (opt.verbose) std::fprintf(stderr, "  analytic iter %d: legal HPWL %lld\n", it, P.cost);
        if (P.cost < bestCost) { bestCost = P.cost; bestX = lx; bestY = ly; }
        if (it == opt.iterations) break;
        // Anchor every movable cell to its legal site, then re-solve (warm start).
        bx = S.bx; by = S.by;
        for (int c : D.movable) {
            int v = S.varOf[c];
            extra[v] = alpha;
            bx[v] += alpha * lx[c];
            by[v] += alpha * ly[c];
        }
        solveCg(S.A, extra, bx, x, 300, 1e-5);
        solveCg(S.A, extra, by, y, 300, 1e-5);
        alpha *= opt.anchorGrowth;
    }
    P.setPositions(bestX, bestY);
    return P.cost;
}
