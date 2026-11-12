#!/usr/bin/env bash
# Fixed-effort comparison on one benchmark: one single-thread run per (effort,
# seed), one sequential job per effort, all jobs in parallel. With a fixed
# --effort the HPWL does not depend on machine load (bit-reproducible), so
# this can run next to other jobs; only the times are affected.
#
#   scripts/effort_ab.sh <benchmarks_dir> <binary> <bench> <efforts> <seeds> <out.csv>
#
# Example: scripts/effort_ab.sh benchmarks__0411_entrega bin/placer-v6 FPGA01 5.0,6.2 101,102,103 effort_ab.csv
set -euo pipefail
BENCH=${1:?}; BIN=${2:?}; B=${3:?}; EFFS=${4:?}; SEEDS=${5:?}; CSV=${6:?}
OUT=effort_ab_out; mkdir -p "$OUT"
d="$BENCH/$B"
[ -f "$CSV" ] || echo "benchmark,effort,seed,hpwl,time_s,legal" > "$CSV"
IFS=, read -ra EL <<< "$EFFS"; IFS=, read -ra SL <<< "$SEEDS"
for e in "${EL[@]}"; do
  (
    for s in "${SL[@]}"; do
      tag="${B}_e${e}_s${s}"
      nice -n 10 "$BIN" "$d/$B.sitemap" "$d/$B.nets" "$d/$B.pl" "$OUT/$tag.pl" --effort "$e" --seed "$s" \
          --runs 1 --threads 1 > "$OUT/$tag.out" 2> "$OUT/$tag.err" || true
      hp=$(sed -n 's/.*RESULT hpwl=\([0-9]*\).*/\1/p' "$OUT/$tag.out")
      t=$(sed -n 's/.*time=\([0-9.]*\)s.*/\1/p' "$OUT/$tag.out")
      ok=$("$BENCH/evaluator" "$d/$B.sitemap" "$d/$B.nets" "$d/$B.pl" "$OUT/$tag.pl" 2>&1 | grep -c PASS || true)
      echo "$B,$e,$s,$hp,$t,$([ "$ok" -ge 2 ] && echo PASS || echo FAIL)" >> "$CSV"
    done
  ) &
done
wait
