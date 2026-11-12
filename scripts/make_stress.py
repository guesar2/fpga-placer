#!/usr/bin/env python3
"""Build a synthetic, denser and larger benchmark from an existing one, to test
the placer on designs like the hidden FPGA05 (more cells, higher utilization).

A connected fraction F of the movable cells (optionally only of some types,
--copy-types) is copied (BFS over nets with at
most --bfs-max-pins pins, from a random start cell). Every net is copied with
its copied members; fixed cells (IOs) are shared with the original, so copied
nets that touch an IO keep it. Nets with more than --huge pins (clock/reset
like) are not copied: the copies are added to the original net instead. Net
copies with fewer than 2 distinct cells are dropped. The site map is the
original one; the input .pl is a random legal placement (fixed cells keep
their coordinates). Fails if a site type would be over capacity.

  python3 -I scripts/make_stress.py <src_dir> <name> <out_dir> --fraction 0.25 [--seed 1]

Example: python3 -I scripts/make_stress.py benchmarks__0411_entrega/FPGA04 FPGA04 stress/S25 --fraction 0.25
writes stress/S25/S25.{sitemap,nets,pl}.
"""
import argparse
import os
import random
import sys
from collections import Counter, deque


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src_dir")
    ap.add_argument("name")
    ap.add_argument("out_dir")
    ap.add_argument("--fraction", type=float, required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--huge", type=int, default=1000)
    ap.add_argument("--bfs-max-pins", type=int, default=16)
    ap.add_argument("--copy-types", default="", help="comma list of types to copy (default: all)")
    a = ap.parse_args()
    rng = random.Random(a.seed)
    src = lambda ext: os.path.join(a.src_dir, a.name + ext)
    out_name = os.path.basename(os.path.normpath(a.out_dir))
    os.makedirs(a.out_dir, exist_ok=True)
    dst = lambda ext: os.path.join(a.out_dir, out_name + ext)

    # ---- site map ----
    with open(src(".sitemap")) as f:
        header = f.readline()
        W, H = map(int, header.split())
        free = {}  # type -> list of (x, y)
        for line in f:
            p = line.split()
            if len(p) == 3:
                free.setdefault(p[2], []).append((int(p[0]), int(p[1])))

    # ---- cells ----
    cells = []  # (name, type, x, y, fixed)
    with open(src(".pl")) as f:
        for line in f:
            p = line.split()
            if len(p) == 5:
                cells.append((p[0], p[1], int(p[2]), int(p[3]), p[4] == "F"))
    ctype = {c[0]: c[1] for c in cells}
    fixed = {c[0] for c in cells if c[4]}
    movable = [c[0] for c in cells if not c[4]]
    allowed = set(a.copy_types.split(",")) if a.copy_types else None
    copyable = [c for c in movable if allowed is None or ctype[c] in allowed]

    # ---- nets ----
    nets = []
    with open(src(".nets")) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                nets.append((p[0], p[1:]))
    cell_nets = {}
    for i, (_, pins) in enumerate(nets):
        if len(pins) <= a.bfs_max_pins:
            for c in pins:
                cell_nets.setdefault(c, []).append(i)

    # ---- connected subset by BFS (restart from a random cell when exhausted) ----
    target = int(round(a.fraction * len(copyable)))
    chosen = set()
    order = copyable[:]
    rng.shuffle(order)
    oi = 0
    while len(chosen) < target:
        while order[oi] in chosen:
            oi += 1
        q = deque([order[oi]])
        chosen.add(order[oi])
        while q and len(chosen) < target:
            c = q.popleft()
            for ni in cell_nets.get(c, ()):
                for d in nets[ni][1]:
                    if (d not in chosen and d not in fixed and len(chosen) < target
                            and (allowed is None or ctype[d] in allowed)):
                        chosen.add(d)
                        q.append(d)
    copy = {c: c + "_s" for c in chosen}

    # ---- capacity check ----
    need = Counter(ctype[c] for c in movable) + Counter(ctype[c] for c in chosen)
    fixed_sites = {(c[2], c[3]) for c in cells if c[4]}
    for t, n in sorted(need.items()):
        cap = sum(1 for s in free.get(t, []) if s not in fixed_sites)
        print(f"{t}: {n} movable / {cap} free sites ({100.0 * n / max(cap, 1):.1f}%)", file=sys.stderr)
        if n > cap:
            sys.exit(f"type {t} over capacity; use a smaller --fraction")

    # ---- write nets ----
    kept = dropped = 0
    with open(dst(".nets"), "w") as f:
        for name, pins in nets:
            if len(pins) > a.huge:
                extra = [copy[c] for c in pins if c in copy]
                f.write(" ".join([name] + pins + extra) + "\n")
                continue
            f.write(" ".join([name] + pins) + "\n")
            if not any(c in copy for c in pins):
                continue
            cp = [copy[c] if c in copy else c for c in pins if c in copy or c in fixed]
            if len(set(cp)) >= 2:
                f.write(" ".join([name + "_s"] + cp) + "\n")
                kept += 1
            else:
                dropped += 1
    print(f"copied {len(chosen)} cells, {kept} nets ({dropped} dropped: <2 cells)", file=sys.stderr)

    # ---- random legal placement ----
    for t in free:
        free[t] = [s for s in free[t] if s not in fixed_sites]
        rng.shuffle(free[t])
    with open(dst(".pl"), "w") as f:
        for name, t, x, y, fx in cells:
            if fx:
                f.write(f"{name} {t} {x} {y} F\n")
            else:
                x, y = free[t].pop()
                f.write(f"{name} {t} {x} {y} M\n")
        for name, t, x, y, fx in cells:
            if name in copy:
                x, y = free[t].pop()
                f.write(f"{copy[name]} {t} {x} {y} M\n")

    with open(src(".sitemap")) as fi, open(dst(".sitemap"), "w") as fo:
        fo.write(fi.read())
    print(f"wrote {a.out_dir} ({len(cells) + len(chosen)} cells, W={W} H={H})", file=sys.stderr)


if __name__ == "__main__":
    main()
