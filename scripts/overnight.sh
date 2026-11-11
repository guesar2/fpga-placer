#!/bin/bash
# Overnight effort sweep (Nov 10): cold vs warm single runs at fixed effort,
# so the portfolio (best of cold+warm runs) can be evaluated at every effort.
# Effort-1 rows for FPGA01-04 were copied from t0.csv (identical binary output).
set -e
cd "$(dirname "$0")/.."
B=benchmarks__0411_entrega
# Wait for any running A/B test so timings are not disturbed.
while pgrep -f "ab.sh" > /dev/null; do sleep 30; done
CFG=(--config cold: --config "warm:--t0-scale 4 --r0 0.15")
python3 -I scripts/sweep.py --bench-dir $B --placer bin/placer-v3 --parallel 5 --seeds 5 \
    --efforts 0.5,1,2,4 "${CFG[@]}" --out sweep.csv --resume
python3 -I scripts/sweep.py --bench-dir $B --placer bin/placer-v3 --parallel 5 --seeds 5 \
    --benchmarks FPGA00,FPGA01,FPGA02 --efforts 8 "${CFG[@]}" --out sweep.csv --resume
