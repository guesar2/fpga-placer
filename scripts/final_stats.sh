#!/usr/bin/env bash
# Statistics of the final default configuration required by the spec: each
# benchmark runs with several seeds (default time budget, default runs and
# threads), one invocation at a time so runs never compete for cores. Every
# output is checked with the evaluator. Resumable: (benchmark, seed) pairs
# already in the CSV are skipped. Small benchmarks go first; the large ones
# are interleaved by seed so partial results stay balanced.
#
#   scripts/final_stats.sh <benchmarks_dir> <binary> <out_dir> <seeds> [placer options...]
#
# Example: scripts/final_stats.sh benchmarks__0411_entrega bin/placer-v6 final_v6 1,2,3,4,5
# writes final_v6/stats.csv and final_v6/<bench>/seed<S>.{pl,out,err}.
# The per-run column lists the HPWL of each internal run (seeds S, S+1, ...).
set -euo pipefail
BENCH=${1:?benchmarks dir}; BIN=${2:?binary}; OUT=${3:?out dir}; SEEDS=${4:?seeds}
shift 4
CSV="$OUT/stats.csv"
mkdir -p "$OUT"
[ -f "$CSV" ] || echo "benchmark,seed,hpwl,time_s,mean_run_hpwl,deadline_hits,legal,per_run" > "$CSV"
IFS=, read -ra SL <<< "$SEEDS"
run_one() {
  local b=$1 s=$2 d="$BENCH/$1"
  shift 2
  grep -q "^$b,$s," "$CSV" && return 0
  mkdir -p "$OUT/$b"
  local tag="$OUT/$b/seed$s"
  # a failed run is recorded (FAIL) instead of stopping the whole study
  "$BIN" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$tag.pl" --seed "$s" "$@" > "$tag.out" 2> "$tag.err" || true
  local hp t mh dh ok legal pr
  hp=$(sed -n 's/.*RESULT hpwl=\([0-9]*\).*/\1/p' "$tag.out")
  t=$(sed -n 's/.*time=\([0-9.]*\)s.*/\1/p' "$tag.out")
  mh=$(sed -n 's/.*mean_hpwl=\([0-9.]*\).*/\1/p' "$tag.out")
  dh=$(grep -c 'deadline hit' "$tag.err" || true)
  ok=$("$BENCH/evaluator" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$tag.pl" 2>&1 | grep -c PASS || true)
  legal=$([ "$ok" -ge 2 ] && echo PASS || echo FAIL)
  pr=$(sed -n 's/^run [0-9]* (seed \([0-9]*\)): HPWL \([0-9]*\).*/\1:\2/p' "$tag.err" | sort -n | paste -sd' ')
  echo "$b,$s,$hp,$t,$mh,$dh,$legal,$pr" | tee -a "$CSV"
}
for b in FPGA00 FPGA01 FPGA02; do
  [ -d "$BENCH/$b" ] || continue
  for s in "${SL[@]}"; do run_one "$b" "$s" "$@"; done
done
for s in "${SL[@]}"; do
  for b in FPGA03 FPGA04; do
    [ -d "$BENCH/$b" ] || continue
    run_one "$b" "$s" "$@"
  done
done
