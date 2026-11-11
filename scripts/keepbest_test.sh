#!/usr/bin/env bash
# keep-best test (Nov 11): fixed effort 1, 5 seeds, 1 run/1 thread, clustering on,
# placer with/without --keep-best. One lane per benchmark (2 cores).
set -uo pipefail
BD=benchmarks__0411_entrega; BIN=bin/placer-kb; OUT=keepbest.csv; L=kb_out
mkdir -p $L; echo "benchmark,variant,seed,hpwl,time_s,legal,restore" > $OUT
lane() { b=$1; d=$BD/$b
  for s in 1 2 3 4 5; do for v in off on; do
    f=""; [ $v = on ] && f=--keep-best
    $BIN $d/$b.sitemap $d/$b.nets $d/$b.pl $L/${b}_$v$s.pl --effort 1 --runs 1 --threads 1 --seed $s --verbose $f > $L/${b}_$v$s.out 2> $L/${b}_$v$s.err
    hp=$(sed -n 's/.*hpwl=\([0-9]*\).*time=\([0-9.]*\)s.*/\1,\2/p' $L/${b}_$v$s.out)
    ok=$($BD/evaluator $d/$b.sitemap $d/$b.nets $d/$b.pl $L/${b}_$v$s.pl 2>&1 | grep -c PASS)
    r=$(grep "restore best" $L/${b}_$v$s.err | sed 's/.*HPWL //' | tr '\n' ';')
    echo "$b,$v,$s,$hp,$([ $ok -ge 2 ] && echo PASS || echo FAIL),$r" >> $OUT
  done; done; }
lane FPGA01 & lane FPGA02 & wait
