#!/usr/bin/env bash
# Runs the placer on every benchmark folder, checks each result with the course
# evaluator and prints a results table (HPWL, legality, runtime).
#
#   scripts/run_benchmarks.sh <benchmarks_dir> <output_dir> [placer options...]
#
# Example:
#   scripts/run_benchmarks.sh ../benchmarks outputs --runs 2
set -euo pipefail
BENCH=${1:?benchmarks dir}; OUT=${2:?output dir}; shift 2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
EVAL="$BENCH/evaluator"
mkdir -p "$OUT"

printf "| %-8s | %12s | %-9s | %10s |\n" Benchmark HPWL Legal "Time (s)"
printf "|----------|--------------|-----------|------------|\n"
for dir in "$BENCH"/*/; do
  b=$(basename "$dir")
  [ -f "$dir/$b.sitemap" ] || continue
  mkdir -p "$OUT/$b"
  out="$OUT/$b/new_$b.pl"
  "$ROOT/placer" "$dir/$b.sitemap" "$dir/$b.nets" "$dir/$b.pl" "$out" "$@" \
      > "$OUT/$b/result.txt" 2> "$OUT/$b/placer.log"
  t=$(sed -n 's/.*time=\([0-9.]*\)s.*/\1/p' "$OUT/$b/result.txt")
  "$EVAL" "$dir/$b.sitemap" "$dir/$b.nets" "$dir/$b.pl" "$out" > "$OUT/$b/evaluator.txt" 2>&1 || true
  hp=$(sed -n 's/.*Test Placement: *\([0-9]*\).*/\1/p' "$OUT/$b/evaluator.txt")
  pass=$(grep -c 'PASS' "$OUT/$b/evaluator.txt" || true)
  legal=$([ "$pass" -ge 2 ] && echo PASS || echo FAIL)
  printf "| %-8s | %12s | %-9s | %10s |\n" "$b" "$hp" "$legal" "$t"
done
