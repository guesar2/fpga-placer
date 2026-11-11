#!/usr/bin/env bash
# A/B test of two placer binaries at the same (default or given) time budget.
# Runs are sequential so the two versions never compete for cores; each output
# is checked with the course evaluator.
#
#   scripts/ab.sh <benchmarks_dir> <binary_A> <binary_B> <benchmarks> [placer options...]
#
# Example (the Nov 10 comparison: v2 = cold + fixed schedule, v3 = adaptive
# schedule + warm/cold portfolio, seed 1, default budget, FPGA03 and FPGA04):
#   scripts/ab.sh benchmarks__0411_entrega bin/placer-v2 bin/placer-v3 FPGA03,FPGA04 --seed 1
#
# Prints one line per (benchmark, binary) with the RESULT line and the number
# of evaluator PASS lines (2 = integrity + legality). Logs and .pl files go to
# ab_out/<bench>_<binary>.{pl,out,err}.
set -euo pipefail
BENCH=${1:?benchmarks dir}; A=${2:?binary A}; B=${3:?binary B}; LIST=${4:?comma list of benchmarks}
shift 4
OUT=ab_out
mkdir -p "$OUT"
IFS=, read -ra BENCHES <<< "$LIST"
for b in "${BENCHES[@]}"; do
  d="$BENCH/$b"
  for bin in "$A" "$B"; do
    tag="${b}_$(basename "$bin")"
    "$bin" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$OUT/$tag.pl" "$@" > "$OUT/$tag.out" 2> "$OUT/$tag.err"
    ok=$("$BENCH/evaluator" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$OUT/$tag.pl" 2>&1 | grep -c PASS || true)
    echo "$b $(basename "$bin") $(grep RESULT "$OUT/$tag.out") pass=$ok"
  done
done
