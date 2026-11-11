#!/usr/bin/env bash
# Time-budget comparison of placer configurations on one benchmark, sequential
# runs (1 thread each), results appended to a CSV with evaluator check.
#   scripts/budget_ab.sh <bench_dir> <bench> <budget_s> <seeds> <out.csv> <binary> "name:args" ...
set -uo pipefail
BD=$1; b=$2; BUD=$3; SEEDS=$4; OUT=$5; BIN=$6; shift 6
d="$BD/$b"; tmp=$(mktemp -d)
[ -f "$OUT" ] || echo "benchmark,config,seed,budget,hpwl,time_s,deadline_hits,legal" > "$OUT"
for seed in $(seq 1 "$SEEDS"); do
  for cfg in "$@"; do
    name=${cfg%%:*}; args=${cfg#*:}
    out=$("$BIN" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$tmp/o.pl" --runs 1 --threads 1 --seed "$seed" \
          --time-budget "$BUD" $args 2> "$tmp/err")
    hp=$(echo "$out" | sed -n 's/.*hpwl=\([0-9]*\).*time=\([0-9.]*\)s.*/\1,\2/p')
    dl=$(grep -c "deadline hit" "$tmp/err")
    ok=$("$BD/evaluator" "$d/$b.sitemap" "$d/$b.nets" "$d/$b.pl" "$tmp/o.pl" 2>&1 | grep -c PASS)
    echo "$b,$name,$seed,$BUD,$hp,$dl,$([ "$ok" -ge 2 ] && echo PASS || echo FAIL)" >> "$OUT"
  done
done
rm -rf "$tmp"
