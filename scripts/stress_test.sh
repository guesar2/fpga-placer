#!/usr/bin/env bash
# Robustness test on synthetic denser/larger designs (scripts/make_stress.py),
# standing in for the hidden FPGA05. Each design runs once, sequentially, with
# the given placer options; reports HPWL, runtime, peak memory, deadline hits,
# whether clustering fell back to the flat flow, and the evaluator verdict.
#
#   scripts/stress_test.sh <stress_dir> <binary> <out.csv> <designs> [placer options...]
#
# Example: scripts/stress_test.sh stress bin/placer-v6 stress.csv S25,S45 --time-budget 1200
set -euo pipefail
DIR=${1:?stress dir}; BIN=${2:?binary}; CSV=${3:?out csv}; LIST=${4:?comma list}
shift 4
OUT="$DIR/out"
mkdir -p "$OUT"
[ -f "$CSV" ] || echo "design,cells,hpwl,time_s,peak_mb,deadline_hits,clustering,legal,options" > "$CSV"
IFS=, read -ra DS <<< "$LIST"
for b in "${DS[@]}"; do
  d="$DIR/$b"
  /usr/bin/time -v "$BIN" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$OUT/$b.pl" --verbose "$@" \
      > "$OUT/$b.out" 2> "$OUT/$b.err"
  hp=$(sed -n 's/.*RESULT hpwl=\([0-9]*\).*/\1/p' "$OUT/$b.out")
  t=$(sed -n 's/.*time=\([0-9.]*\)s.*/\1/p' "$OUT/$b.out")
  mb=$(awk '/Maximum resident set size/{printf "%.0f", $NF/1024}' "$OUT/$b.err")
  dh=$(grep -c 'deadline hit' "$OUT/$b.err" || true)
  if grep -q 'using flat flow' "$OUT/$b.err"; then cl=flat-fallback
  elif grep -q '^clustering ' "$OUT/$b.err"; then cl=clustered
  else cl=flat; fi
  "$DIR/evaluator" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$OUT/$b.pl" > "$OUT/$b.eval" 2>&1 || true
  ok=$(grep -c PASS "$OUT/$b.eval" || true)
  legal=$([ "$ok" -ge 2 ] && echo PASS || echo FAIL)
  cells=$(grep -c . "$d/$b.pl")
  echo "$b,$cells,$hp,$t,$mb,$dh,$cl,$legal,\"$*\"" | tee -a "$CSV"
done
