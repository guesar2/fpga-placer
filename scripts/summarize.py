#!/usr/bin/env python3
"""Summarizes a sweep CSV (from scripts/sweep.py).

Prints, per benchmark / configuration / effort: runs, best, mean, std-dev,
mean time, legality. Then, per benchmark and configuration:
  * a linear fit  time ~ a + b * effort   (seconds on the sweep machine), and
  * the "knee": the smallest effort whose mean HPWL is within --tol of the
    best mean, with the time it took -- a candidate time budget.
With matplotlib installed it also writes <csv>_hpwl_vs_effort.png and
<csv>_hpwl_vs_time.png.

  scripts/summarize.py sweep.csv [--tol 0.01] [--markdown] [--no-plot]
"""
import argparse
import csv
import itertools
import math
import os
from collections import defaultdict


def stats(v):
    m = sum(v) / len(v)
    sd = math.sqrt(sum((x - m) ** 2 for x in v) / len(v)) if len(v) > 1 else 0.0
    return m, sd


def linfit(xs, ys):
    n = len(xs)
    if n < 2 or len(set(xs)) < 2:
        return (0.0, ys[0] / xs[0]) if n else (0.0, 0.0)
    mx, my = sum(xs) / n, sum(ys) / n
    b = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sum((x - mx) ** 2 for x in xs)
    return my - b * mx, b


def fmt_time(s):
    return f"{s:.1f}s" if s < 120 else f"{s / 60:.1f}min" if s < 7200 else f"{s / 3600:.2f}h"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--tol", type=float, default=0.01, help="knee tolerance vs best mean (default 1%%)")
    ap.add_argument("--markdown", action="store_true", help="print tables as markdown (for the report)")
    ap.add_argument("--no-plot", action="store_true")
    ap.add_argument("--portfolio", default="cold,warm", help="two configs A,B for the portfolio estimate")
    ap.add_argument("--portfolio-k", type=int, default=4, help="parallel runs in the portfolio (default 4)")
    ap.add_argument("--ref", default="cold", help="reference configuration for comparisons (default: cold, "
                    "else the first alphabetically)")
    a = ap.parse_args()

    groups = defaultdict(list)
    errors = 0
    with open(a.csv) as f:
        for r in csv.DictReader(f):
            if not r["hpwl"]:
                errors += 1
                continue
            groups[(r["benchmark"], r["config"], float(r["effort"]))].append(r)

    sep = " | " if a.markdown else "  "
    head = ["benchmark", "config", "effort", "runs", "best", "mean", "std", "std%", "mean time", "legal"]
    print(sep.join(head) if a.markdown else
          f"{'benchmark':<9} {'config':<8} {'effort':>6} {'runs':>4} {'best':>11} {'mean':>11} {'std':>9} "
          f"{'std%':>6} {'mean time':>10} legal")
    if a.markdown:
        print(sep.join(["---"] * len(head)))
    summary = {}
    for key in sorted(groups):
        rows = groups[key]
        h = [int(r["hpwl"]) for r in rows]
        t = [float(r["time_s"]) for r in rows]
        m, sd = stats(h)
        tm, _ = stats(t)
        legal = sum(r["legal"] == "PASS" for r in rows)
        checked = sum(r["legal"] in ("PASS", "FAIL") for r in rows)
        lg = f"{legal}/{checked}" if checked else "n/a"
        summary[key] = (min(h), m, sd, tm, t)
        b, c, e = key
        cells = [b, c, f"{e:g}", str(len(h)), str(min(h)), f"{m:.0f}", f"{sd:.0f}", f"{100 * sd / m:.2f}",
                 fmt_time(tm), lg]
        print(sep.join(cells) if a.markdown else
              f"{b:<9} {c:<8} {e:>6g} {len(h):>4} {min(h):>11} {m:>11.0f} {sd:>9.0f} {100 * sd / m:>6.2f} "
              f"{fmt_time(tm):>10} {lg}")
    if errors:
        print(f"\n{errors} failed jobs (empty hpwl) ignored")

    print(f"\nTime per effort and knee (smallest effort with mean within {100 * a.tol:g}% of best mean):")
    by_bc = defaultdict(list)
    for (b, c, e), v in summary.items():
        by_bc[(b, c)].append((e, v))
    for (b, c), lst in sorted(by_bc.items()):
        lst.sort()
        xs = [e for e, v in lst for _ in v[4]]
        ys = [t for e, v in lst for t in v[4]]
        a0, b0 = linfit(xs, ys)
        best_mean = min(v[1] for _, v in lst)
        knee = next(e for e, v in lst if v[1] <= best_mean * (1 + a.tol))
        kv = dict(lst)[knee]
        print(f"  {b:<9} {c:<8} time ~ {a0:.1f} + {b0:.1f}*effort s   knee effort {knee:g} "
              f"(mean {kv[1]:.0f}, {fmt_time(kv[3])} single-thread on this machine; "
              f"best mean {best_mean:.0f})")

    # ---- configurations vs a reference (same benchmark and effort) ----------
    configs = sorted({c for _, c in by_bc})
    if len(configs) > 1:
        ref = a.ref if a.ref in configs else configs[0]
        print(f"\nConfigurations vs '{ref}' (same benchmark/effort). Welch t = dmean / sqrt(s1^2/n1 + s2^2/n2) "
              f"with sample std; |t| > ~2.3 (5 vs 5 runs) is unlikely to be seed noise:")
        for (b, c, e) in sorted(groups):
            if c == ref or (b, ref, e) not in groups:
                continue
            x = [int(r["hpwl"]) for r in groups[(b, c, e)]]
            y = [int(r["hpwl"]) for r in groups[(b, ref, e)]]
            mx, my = sum(x) / len(x), sum(y) / len(y)
            vx = sum((v - mx) ** 2 for v in x) / (len(x) - 1) if len(x) > 1 else 0.0
            vy = sum((v - my) ** 2 for v in y) / (len(y) - 1) if len(y) > 1 else 0.0
            se = math.sqrt(vx / len(x) + vy / len(y))
            t = (mx - my) / se if se > 0 else float("nan")
            print(f"  {b:<9} effort {e:<5g} {c:<8} {100 * (mx - my) / my:+6.2f}%  t={t:+6.2f}  "
                  f"({'better' if mx < my else 'worse'})")

    # ---- best-of-K runs vs one run with K times the effort -------------------
    # E[min of a random K-subset of the n observed runs] (exact over subsets):
    # sorted ascending, x_(i) is the minimum of C(n-i, K-1) of the C(n, K) subsets.
    print("\nBest-of-K (expected min over K of the n seeds) vs mean of a single run at K x effort:")
    for (b, c), lst in sorted(by_bc.items()):
        means = {e: v[1] for e, v in lst}
        for e, v in sorted(lst):
            h = sorted(int(r["hpwl"]) for r in groups[(b, c, e)])
            n = len(h)
            parts = []
            for K in range(2, n + 1):
                emin = sum(h[i] * math.comb(n - 1 - i, K - 1) for i in range(n)) / math.comb(n, K)
                alt = means.get(e * K)
                parts.append(f"K={K}: {emin:.0f}" + (f" vs {alt:.0f}" if alt else ""))
            if parts:
                print(f"  {b:<9} {c:<8} effort {e:<5g} mean {v[1]:.0f}  " + "  ".join(parts))

    # ---- portfolio: best of (K - W) runs of one config and W of another ------
    # Exact expectation over all subsets of the observed seeds; time is the
    # slower config's mean time (runs execute in parallel).
    pa, pb = a.portfolio.split(",") if a.portfolio else ("", "")
    if pa in configs and pb in configs:
        K = a.portfolio_k
        print(f"\nPortfolio of {K} parallel runs (expected best; '{pa}' x (K-W) + '{pb}' x W):")
        for (b, c, e) in sorted(groups):
            if c != pa or (b, pb, e) not in groups:
                continue
            ha = [int(r["hpwl"]) for r in groups[(b, pa, e)]]
            hb = [int(r["hpwl"]) for r in groups[(b, pb, e)]]
            ta, tb = summary[(b, pa, e)][3], summary[(b, pb, e)][3]
            cells = []
            for W in range(0, K + 1):
                if K - W > len(ha) or W > len(hb):
                    continue
                sa = list(itertools.combinations(ha, K - W)) if K - W else [()]
                sb = list(itertools.combinations(hb, W)) if W else [()]
                ev = sum(min(x + y) for x in sa for y in sb) / (len(sa) * len(sb))
                t = max(ta if K - W else 0, tb if W else 0)
                cells.append(f"W={W}: {ev:.0f} ({fmt_time(t)})")
            print(f"  {b:<9} effort {e:<5g} " + "  ".join(cells))

    if a.no_plot:
        return
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib not installed: skipping plots; pip install matplotlib)")
        return
    benches = sorted({b for b, _ in by_bc})
    base = os.path.splitext(a.csv)[0]
    for xkind in ("effort", "time"):
        ncol = min(3, len(benches))
        nrow = math.ceil(len(benches) / ncol)
        fig, axes = plt.subplots(nrow, ncol, figsize=(5 * ncol, 3.8 * nrow), squeeze=False)
        for i, b in enumerate(benches):
            ax = axes[i // ncol][i % ncol]
            for (bb, c), lst in sorted(by_bc.items()):
                if bb != b:
                    continue
                lst = sorted(lst)
                xs = [e if xkind == "effort" else v[3] for e, v in lst]
                ms = [v[1] for _, v in lst]
                sds = [v[2] for _, v in lst]
                ax.errorbar(xs, ms, yerr=sds, marker="o", capsize=3, label=c)
            ax.set_xscale("log")
            ax.set_title(b)
            ax.set_xlabel("effort (inner_num)" if xkind == "effort" else "mean time per run (s)")
            ax.set_ylabel("HPWL (mean ± std)")
            ax.grid(True, alpha=0.3)
            ax.legend(fontsize=8)
        for j in range(len(benches), nrow * ncol):
            axes[j // ncol][j % ncol].axis("off")
        fig.tight_layout()
        out = f"{base}_hpwl_vs_{xkind}.png"
        fig.savefig(out, dpi=130)
        print(f"wrote {out}")


if __name__ == "__main__":
    main()
