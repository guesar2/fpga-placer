#!/usr/bin/env bash
# Coarse-share test (Nov 11): FPGA03/04 at a 30-min single-thread budget,
# then FPGA01/02 with the full default settings (default budget, 4 runs).
set -uo pipefail
BD=benchmarks__0411_entrega; BIN=bin/placer-dev3
for b in FPGA03 FPGA04; do
  for s in 0.5 0.65 0.8; do
    scripts/budget_ab.sh $BD $b 1800 1 cluster_budget_1800.csv $BIN "s$s:--coarse-share $s --seed 3" &
  done
done
wait
mkdir -p ab_out
for b in FPGA01 FPGA02; do
  d=$BD/$b
  for s in 0.5 0.8; do
    tag=${b}_s$s
    $BIN $d/$b.sitemap $d/$b.nets $d/$b.pl ab_out/$tag.pl --seed 1 --coarse-share $s > ab_out/$tag.out 2> ab_out/$tag.err
    ok=$($BD/evaluator $d/$b.sitemap $d/$b.nets $d/$b.pl ab_out/$tag.pl 2>&1 | grep -c PASS)
    echo "$b s$s $(grep RESULT ab_out/$tag.out) pass=$ok" >> share_default.txt
  done
done
