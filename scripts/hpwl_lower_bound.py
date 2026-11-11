#!/usr/bin/env python3
"""Provable lower bound on total HPWL for a benchmark (sum of per-net bounds).

For each net, every cell needs its own site of its type, so the net's bounding
box (w+1 columns x h+1 rows, HPWL = w + h) must contain at least m_t sites of
each type t used by the net, at least k sites in total, and all fixed pins.
For each type, g_t(w, h) = max number of type-t sites inside any (w+1)x(h+1)
window of the real sitemap; f_t(m) = min{w + h : g_t(w, h) >= m}. Then

    LB(net) = max( HPWL of the fixed pins, f_all(k), max_t f_t(m_t) )

and the sum over nets is a lower bound on any legal placement. It is loose:
each net is bounded as if it alone got the best spot on the chip, ignoring
that nets share cells and compete for sites.

  .venv/bin/python -I scripts/hpwl_lower_bound.py <benchmarks_dir> [FPGA01 ...]
"""
import os
import sys
from collections import Counter

import numpy as np


def read_sitemap(path):
    with open(path) as f:
        W, H = map(int, f.readline().split())
        types = {}
        grid = {}
        for line in f:
            p = line.split()
            if len(p) < 3 or p[2] == "INVALID":
                continue
            grid[(int(p[0]), int(p[1]))] = types.setdefault(p[2], len(types))
    return W, H, types, grid


def window_max(W, H, types, grid):
    """g[t][s] = max sites of type t (last index: any type) in a window with w + h = s."""
    T = len(types)
    occ = np.zeros((T + 1, W, H), dtype=np.int32)
    for (x, y), t in grid.items():
        occ[t, x, y] = 1
        occ[T, x, y] = 1
    best = np.zeros((T + 1, W + H - 1), dtype=np.int64)
    cx = np.concatenate([np.zeros((T + 1, 1, H), np.int64), np.cumsum(occ, axis=1)], axis=1)
    for w in range(W):
        cols = cx[:, w + 1:, :] - cx[:, :W - w, :]  # (T+1, W-w, H): sums over w+1 columns
        cy = np.concatenate([np.zeros((T + 1, W - w, 1), np.int64), np.cumsum(cols, axis=2)], axis=2)
        for h in range(H):
            v = (cy[:, :, h + 1:] - cy[:, :, :H - h]).max(axis=(1, 2))
            np.maximum(best[:, w + h], v, out=best[:, w + h])
    return np.maximum.accumulate(best, axis=1)  # a larger s can always use the window of a smaller s


def f_of(best_t, m):
    """min s with best_t[s] >= m (inf if impossible)."""
    if m <= 1:
        return 0
    s = int(np.searchsorted(best_t, m))
    return s if s < len(best_t) else float("inf")


def bound(bench_dir, b, types, best):
    d = os.path.join(bench_dir, b)
    ctype, fixed = {}, {}
    with open(os.path.join(d, b + ".pl")) as f:
        for line in f:
            p = line.split()
            if len(p) < 5:
                continue
            ctype[p[0]] = types[p[1]]
            if p[4] == "F":
                fixed[p[0]] = (int(p[2]), int(p[3]))
    T = len(types)
    total = 0
    by_deg = Counter()
    cache = {}
    with open(os.path.join(d, b + ".nets")) as f:
        for line in f:
            p = line.split()
            cells = set(p[1:])
            if len(cells) < 2:
                continue
            cnt = Counter(ctype[c] for c in cells)
            key = tuple(sorted(cnt.items()))
            if key not in cache:
                lb = f_of(best[T], len(cells))
                for t, m in cnt.items():
                    lb = max(lb, f_of(best[t], m))
                cache[key] = lb
            lb = cache[key]
            fp = [fixed[c] for c in cells if c in fixed]
            if len(fp) >= 2:
                xs, ys = zip(*fp)
                lb = max(lb, max(xs) - min(xs) + max(ys) - min(ys))
            total += lb
            k = len(cells)
            by_deg["2" if k == 2 else "3-10" if k <= 10 else "11-100" if k <= 100 else ">100"] += lb
    return total, by_deg


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    bench_dir = sys.argv[1]
    benches = sys.argv[2:] or sorted(e for e in os.listdir(bench_dir)
                                     if os.path.isfile(os.path.join(bench_dir, e, e + ".sitemap")))
    cache = {}
    print(f"{'bench':<7} {'lower bound':>12}   by net degree (2 / 3-10 / 11-100 / >100)")
    for b in benches:
        sm = os.path.join(bench_dir, b, b + ".sitemap")
        key = open(sm, "rb").read()
        if key not in cache:
            W, H, types, grid = read_sitemap(sm)
            cache[key] = (types, window_max(W, H, types, grid))
        types, best = cache[key]
        total, by = bound(bench_dir, b, types, best)
        print(f"{b:<7} {total:>12,}   {by['2']:,} / {by['3-10']:,} / {by['11-100']:,} / {by['>100']:,}",
              flush=True)


if __name__ == "__main__":
    main()
