#!/usr/bin/env python3
"""Effort x seed x benchmark x configuration sweep for the placer.

Every job is one single-threaded placer run (--runs 1 --threads 1) with a fixed
--effort, so results are reproducible and independent of machine speed; only
the measured time depends on the machine. Jobs run in parallel (one per core
by default) and each finished job is appended to the CSV immediately, so an
interrupted sweep can be resumed with --resume.

Examples
  # Default plan: all benchmarks, efforts 0.5..8, 5 seeds, analytical start
  scripts/sweep.py --bench-dir ../benchmarks --out sweep.csv

  # Starting-temperature hypothesis on the dense designs
  scripts/sweep.py --bench-dir ../benchmarks --benchmarks FPGA01,FPGA03,FPGA04 \\
      --efforts 1 --seeds 5 --config cold: --config "warm:--t0-scale 4 --r0 0.15" \\
      --config "given:--init given" --out t0.csv

  # Only print the job list and a rough time estimate
  scripts/sweep.py --bench-dir ../benchmarks --dry-run

Then: scripts/summarize.py sweep.csv
"""
import argparse
import csv
import os
import re
import shlex
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIELDS = ["benchmark", "config", "effort", "seed", "hpwl", "time_s", "temps", "legal", "args", "host", "parallel"]

# Seconds per unit of effort of one cold-start job on the 6-core i5-9600K with
# 5 jobs in parallel (t0.csv; memory contention makes this ~1.5-2x a solo run,
# warmer starts take ~1.25x longer). Used only for --dry-run estimates.
EST_SEC_PER_EFFORT = {"FPGA00": 0.3, "FPGA01": 45, "FPGA02": 102, "FPGA03": 627, "FPGA04": 884}


def parse_list(text, conv):
    return [conv(v) for v in text.split(",") if v.strip()]


def parse_seeds(text):
    if "-" in text:  # range a-b
        a, b = text.split("-")
        return list(range(int(a), int(b) + 1))
    if "," in text:
        return parse_list(text, int)
    return list(range(1, int(text) + 1))  # count


def find_benchmarks(bench_dir, wanted):
    names = sorted(d for d in os.listdir(bench_dir)
                   if os.path.isfile(os.path.join(bench_dir, d, d + ".sitemap")))
    if wanted:
        missing = [w for w in wanted if w not in names]
        if missing:
            sys.exit(f"benchmarks not found in {bench_dir}: {', '.join(missing)}")
        names = wanted
    return names


def run_job(job, args, tmpdir):
    b, cfg_name, cfg_args, effort, seed = job
    d = os.path.join(args.bench_dir, b)
    out_pl = os.path.join(tmpdir, f"{b}_{cfg_name}_{effort}_{seed}.pl")
    cmd = [args.placer, f"{d}/{b}.sitemap", f"{d}/{b}.nets", f"{d}/{b}.pl", out_pl,
           "--runs", "1", "--threads", "1", "--seed", str(seed), "--effort", str(effort)] + cfg_args
    t0 = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    wall = time.time() - t0
    m = re.search(r"RESULT hpwl=(\d+).*time=([\d.]+)s", p.stdout)
    temps = re.search(r"temps (\d+)", p.stderr)
    row = {"benchmark": b, "config": cfg_name, "effort": effort, "seed": seed,
           "hpwl": m.group(1) if m else "", "time_s": m.group(2) if m else f"{wall:.2f}",
           "temps": temps.group(1) if temps else "", "legal": "", "args": " ".join(cfg_args),
           "host": os.uname().nodename, "parallel": args.parallel}
    if p.returncode != 0 or not m:
        row["legal"] = "ERROR"
        sys.stderr.write(f"\n[error] {' '.join(cmd)}\n{p.stderr[-2000:]}\n")
    elif args.evaluator:
        ev = subprocess.run([args.evaluator, f"{d}/{b}.sitemap", f"{d}/{b}.nets", f"{d}/{b}.pl", out_pl],
                            capture_output=True, text=True)
        text = ev.stdout + ev.stderr
        row["legal"] = "PASS" if text.count("PASS") >= 2 else "FAIL"
    if not args.keep_pl and os.path.exists(out_pl):
        os.remove(out_pl)
    return row


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bench-dir", required=True, help="folder with FPGA00/, FPGA01/, ... and the evaluator")
    ap.add_argument("--benchmarks", default="", help="comma list (default: all found)")
    ap.add_argument("--efforts", default="0.5,1,2,4,8", help="comma list of --effort values")
    ap.add_argument("--seeds", default="5", help="count (5), range (1-5) or list (1,3,7)")
    ap.add_argument("--config", action="append", default=[],
                    help='"name:extra placer args", repeatable (default: "qp:")')
    ap.add_argument("--parallel", type=int, default=os.cpu_count() or 1, help="concurrent jobs (default: #cores)")
    ap.add_argument("--placer", default=os.path.join(ROOT, "placer"))
    ap.add_argument("--out", default="sweep.csv")
    ap.add_argument("--resume", action="store_true", help="skip jobs already in --out")
    ap.add_argument("--no-eval", action="store_true", help="do not run the evaluator on each result")
    ap.add_argument("--keep-pl", action="store_true", help="keep every generated .pl (in <out>_pl/)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.access(args.placer, os.X_OK):
        sys.exit(f"placer binary not found: {args.placer} (run make first)")
    ev = os.path.join(args.bench_dir, "evaluator")
    args.evaluator = None if args.no_eval or not os.access(ev, os.X_OK) else ev
    benches = find_benchmarks(args.bench_dir, parse_list(args.benchmarks, str))
    efforts = parse_list(args.efforts, float)
    seeds = parse_seeds(args.seeds)
    configs = []
    for c in args.config or ["qp:"]:
        name, _, extra = c.partition(":")
        configs.append((name.strip() or "qp", shlex.split(extra)))

    jobs = [(b, cn, ca, e, s) for b in benches for cn, ca in configs for e in efforts for s in seeds]
    done = set()
    if args.resume and os.path.exists(args.out):
        with open(args.out) as f:
            for r in csv.DictReader(f):
                if r["hpwl"]:
                    done.add((r["benchmark"], r["config"], float(r["effort"]), int(r["seed"])))
    jobs = [j for j in jobs if (j[0], j[1], j[3], j[4]) not in done]
    # Longest jobs first so the pool stays busy until the end.
    jobs.sort(key=lambda j: -EST_SEC_PER_EFFORT.get(j[0], 900) * j[3])

    est = sum(EST_SEC_PER_EFFORT.get(j[0], 900) * j[3] for j in jobs)
    print(f"{len(jobs)} jobs ({len(done)} already done), {args.parallel} in parallel; "
          f"rough estimate {est / 3600:.1f} CPU-h -> ~{est / 3600 / args.parallel:.1f} h wall "
          f"(based on an i5-9600K at 5 parallel jobs; unknown benchmarks assumed 900 s/effort)")
    if args.dry_run:
        for j in jobs:
            print(f"  {j[0]}  {j[1]:<8} effort={j[3]:<5g} seed={j[4]}  {' '.join(j[2])}")
        return

    tmpdir = (os.path.splitext(args.out)[0] + "_pl") if args.keep_pl else os.path.join(
        os.path.dirname(os.path.abspath(args.out)), ".sweep_tmp")
    os.makedirs(tmpdir, exist_ok=True)
    new_file = not os.path.exists(args.out) or not args.resume
    f = open(args.out, "w" if new_file else "a", newline="")
    w = csv.DictWriter(f, fieldnames=FIELDS)
    if new_file:
        w.writeheader()
    t_start = time.time()
    with ThreadPoolExecutor(max_workers=args.parallel) as pool:
        futs = [pool.submit(run_job, j, args, tmpdir) for j in jobs]
        for i, fut in enumerate(as_completed(futs), 1):
            r = fut.result()
            w.writerow(r)
            f.flush()
            print(f"[{i}/{len(jobs)} {time.time() - t_start:7.0f}s] {r['benchmark']} {r['config']} "
                  f"effort={r['effort']} seed={r['seed']} hpwl={r['hpwl']} t={r['time_s']}s {r['legal']}",
                  flush=True)
    f.close()
    if not args.keep_pl:
        try:
            os.rmdir(tmpdir)
        except OSError:
            pass
    print(f"done -> {args.out}   next: scripts/summarize.py {args.out}")


if __name__ == "__main__":
    try:
        main()
    except BrokenPipeError:  # e.g. output piped into head
        sys.stderr.close()
