#!/usr/bin/env bash
# Runs one benchmark with several seeds (one annealing run per seed) and
# reports best / mean / std-dev of HPWL and mean runtime, as the project spec
# requires for randomized algorithms (at least 5 experiments).
#
#   scripts/seed_study.sh <benchmark_folder> <num_seeds> [placer options...]
#
# Example (reproducible, fixed effort):
#   scripts/seed_study.sh ../benchmarks/FPGA01 5 --effort 2
set -euo pipefail
DIR=${1:?benchmark folder}; K=${2:?number of seeds}; shift 2
ROOT=$(cd "$(dirname "$0")/.." && pwd)
b=$(basename "$DIR")
TMP=$(mktemp -d)
for s in $(seq 1 "$K"); do
  "$ROOT/placer" "$DIR/$b.sitemap" "$DIR/$b.nets" "$DIR/$b.pl" "$TMP/s$s.pl" --runs 1 --seed "$s" "$@" 2>/dev/null \
    | sed -n "s/.*hpwl=\([0-9]*\) .*time=\([0-9.]*\)s.*/$s \1 \2/p"
done | tee "$TMP/res.txt"
awk -v b="$b" '{h[NR]=$2; t+=$3; s+=$2; if(NR==1||$2<best){best=$2; bs=$1}}
  END{m=s/NR; for(i=1;i<=NR;i++) v+=(h[i]-m)^2; sd=sqrt(v/NR);
      printf "%s: seeds=%d best=%d (seed %d) mean=%.0f std=%.0f (%.2f%%) mean_time=%.1fs\n", b, NR, best, bs, m, sd, 100*sd/m, t/NR}' "$TMP/res.txt"
rm -rf "$TMP"
