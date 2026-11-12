#!/usr/bin/env bash
# Equal-time A/B of the exact window optimization: the same binary at the
# default time budget with --window-passes 0 and P, over several seeds. Runs
# are sequential so they never compete for cores. Window passes run inside the
# 5% of each batch reserved after annealing, so both variants get the same
# annealing time.
#
#   scripts/window_ab.sh <benchmarks_dir> <binary> <bench> <seeds> <passes> <out.csv> [placer options...]
#
# Example: scripts/window_ab.sh benchmarks__0411_entrega bin/placer-v6dev FPGA01 1,2,3 3 window_ab.csv
set -euo pipefail
BENCH=${1:?benchmarks dir}; BIN=${2:?binary}; B=${3:?benchmark}; SEEDS=${4:?seeds}; P=${5:?passes}; CSV=${6:?out csv}
shift 6
OUT=window_ab_out
mkdir -p "$OUT"
d="$BENCH/$B"
[ -f "$CSV" ] || echo "benchmark,passes,seed,hpwl,time_s,window_gain,deadline_hits,legal" > "$CSV"
IFS=, read -ra SL <<< "$SEEDS"
for s in "${SL[@]}"; do
  for p in 0 "$P"; do
    tag="${B}_w${p}_s${s}"
    "$BIN" "$d/$B.sitemap" "$d/$B.nets" "$d/$B.pl" "$OUT/$tag.pl" --seed "$s" --window-passes "$p" "$@" \
        > "$OUT/$tag.out" 2> "$OUT/$tag.err"
    hp=$(sed -n 's/.*RESULT hpwl=\([0-9]*\).*/\1/p' "$OUT/$tag.out")
    t=$(sed -n 's/.*time=\([0-9.]*\)s.*/\1/p' "$OUT/$tag.out")
    # window gain of the best run is not printed separately; sum over runs for reference
    wg=$(sed -n 's/.*window -\([0-9]*\).*/\1/p' "$OUT/$tag.err" | paste -sd+ | bc)
    dh=$(grep -c 'deadline hit' "$OUT/$tag.err" || true)
    ok=$("$BENCH/evaluator" "$d/$B.sitemap" "$d/$B.nets" "$d/$B.pl" "$OUT/$tag.pl" 2>&1 | grep -c PASS || true)
    legal=$([ "$ok" -ge 2 ] && echo PASS || echo FAIL)
    echo "$B,$p,$s,$hp,$t,$wg,$dh,$legal" | tee -a "$CSV"
  done
done
