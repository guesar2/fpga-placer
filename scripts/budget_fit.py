#!/usr/bin/env python3
"""Fits the default time budget  budget(N) = a + c * N^p  (N = movable cells)
and plots it against the time limits and the measured sweep times.

The rule must be a generic function of design size (no per-benchmark values),
so a single power law is fitted through target budgets = FRAC * time limit of
the benchmarks whose limit binds (FPGA01-03; FPGA04 has about the same N as
FPGA03 but a longer limit; FPGA00 converges in seconds and only uses `a`).
The result is capped at --cap seconds.

  .venv/bin/python -I scripts/budget_fit.py --bench-dir benchmarks__0411_entrega \
      --sweep sweep.csv [--frac 0.7] [--a 60] [--cap 10800] [--out budget_fit.png]
"""
import argparse
import csv
import math
import os
from collections import defaultdict

LIMIT_S = {"FPGA00": 300, "FPGA01": 360, "FPGA02": 900, "FPGA03": 9000, "FPGA04": 14400}
FIT_BENCHES = ("FPGA01", "FPGA02", "FPGA03")


def movable_cells(bench_dir, b):
    with open(os.path.join(bench_dir, b, b + ".pl")) as f:
        return sum(1 for line in f if line.split()[-1:] == ["M"])


def fit_power(ns, ys, a):
    # least squares on log(y - a) = log c + p log N
    xs = [math.log(n) for n in ns]
    zs = [math.log(y - a) for y in ys]
    mx, mz = sum(xs) / len(xs), sum(zs) / len(zs)
    p = sum((x - mx) * (z - mz) for x, z in zip(xs, zs)) / sum((x - mx) ** 2 for x in xs)
    return math.exp(mz - p * mx), p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bench-dir", required=True)
    ap.add_argument("--sweep", default="sweep.csv")
    ap.add_argument("--frac", type=float, default=0.7, help="target fraction of each time limit")
    ap.add_argument("--a", type=float, default=60.0, help="constant term (s)")
    ap.add_argument("--cap", type=float, default=10800.0, help="hard cap (s)")
    ap.add_argument("--out", default="budget_fit.png")
    o = ap.parse_args()

    N = {b: movable_cells(o.bench_dir, b) for b in LIMIT_S}
    targets = {b: o.frac * LIMIT_S[b] for b in FIT_BENCHES}
    c, p = fit_power([N[b] for b in FIT_BENCHES], [targets[b] for b in FIT_BENCHES], o.a)
    budget = lambda n: min(o.cap, o.a + c * n ** p)

    print(f"budget(N) = min({o.cap:.0f}, {o.a:.0f} + {c:.6e} * N^{p:.6f}) seconds")
    print(f"{'bench':<7} {'N':>7} {'budget':>9} {'limit':>8} {'% limit':>8}")
    for b in LIMIT_S:
        bt = budget(N[b])
        print(f"{b:<7} {N[b]:>7} {bt / 60:>7.1f}min {LIMIT_S[b] / 60:>6.0f}min {100 * bt / LIMIT_S[b]:>7.0f}%")
    for n in (60000, 70000):
        print(f"(N={n}: {budget(n) / 60:.0f} min; FPGA05 limit 300 min)")

    # measured single-run times per effort (cold, 5 parallel jobs)
    times = defaultdict(list)
    hp = defaultdict(list)
    if os.path.exists(o.sweep):
        with open(o.sweep) as f:
            for r in csv.DictReader(f):
                if r["config"] == "cold" and r["hpwl"]:
                    times[(r["benchmark"], float(r["effort"]))].append(float(r["time_s"]))
                    hp[(r["benchmark"], float(r["effort"]))].append(int(r["hpwl"]))

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, (ax, ax2) = plt.subplots(1, 2, figsize=(13, 5))
    ns = [10 ** (2 + 3 * i / 200) for i in range(201)]
    ax.plot(ns, [budget(n) for n in ns], "k-", lw=2, label="budget(N) (fit)")
    for b in LIMIT_S:
        ax.scatter([N[b]], [LIMIT_S[b]], marker="_", s=400, color="tab:red", zorder=3)
        ax.annotate(b, (N[b], LIMIT_S[b]), textcoords="offset points", xytext=(6, 4), fontsize=8)
    ax.scatter([N[b] for b in FIT_BENCHES], [targets[b] for b in FIT_BENCHES], marker="o",
               facecolors="none", edgecolors="tab:red", s=60, zorder=3, label=f"target = {o.frac:g} × limit")
    ax.scatter([], [], marker="_", s=200, color="tab:red", label="time limit")
    cmap = plt.get_cmap("viridis")
    efforts = sorted({e for _, e in times})
    for i, e in enumerate(efforts):
        pts = [(N[b], sum(v) / len(v)) for (b, ee), v in times.items() if ee == e and b in N]
        ax.scatter(*zip(*pts), marker="x", color=cmap(i / max(1, len(efforts) - 1)), s=30,
                   label=f"measured, effort {e:g}")
    ax.set_xscale("log"); ax.set_yscale("log")
    ax.set_xlabel("movable cells N"); ax.set_ylabel("seconds")
    ax.set_title("Default time budget vs design size")
    ax.grid(True, which="both", alpha=0.3); ax.legend(fontsize=7, loc="upper left")

    # HPWL vs time, normalized to each benchmark's best mean, with the budget marked
    for k, b in enumerate(b for b in LIMIT_S if b != "FPGA00"):
        pts = sorted((sum(times[(b, e)]) / len(times[(b, e)]), sum(hp[(b, e)]) / len(hp[(b, e)]))
                     for (bb, e) in hp if bb == b)
        if not pts:
            continue
        best = min(h for _, h in pts)
        col = f"C{k}"
        ax2.plot([t for t, _ in pts], [h / best for _, h in pts], "o-", color=col, label=b)
        ax2.axvline(budget(N[b]) * 0.85, color=col, ls="--", lw=1)
    ax2.set_xscale("log")
    ax2.set_xlabel("single-run time (s, 5 parallel jobs)")
    ax2.set_ylabel("mean HPWL / best mean")
    ax2.set_title("HPWL vs time (dashed: 85% of budget = annealing share)")
    ax2.grid(True, which="both", alpha=0.3); ax2.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(o.out, dpi=130)
    print(f"wrote {o.out}")


if __name__ == "__main__":
    main()
