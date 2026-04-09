#!/usr/bin/env bash
# Sweep DOR (XY) routing over uniform_random traffic at varying injection rates.
# Results go to m5out/results/dor_uniform_<rate>/
# A summary CSV is written to m5out/results/dor_uniform_summary.csv

set -euo pipefail

GEM5=./build/NULL/gem5.opt
CFG=configs/example/garnet_synth_traffic.py
RESULTS=m5out/results
TICKS_PER_CYCLE=1000   # 1ps tick resolution, 1GHz clock

mkdir -p "$RESULTS"

SUMMARY="$RESULTS/dor_neighbor_rand_vnet_summary.csv"
echo "injection_rate,avg_packet_latency_cycles,avg_packet_network_latency_cycles,avg_packet_queueing_latency_cycles" > "$SUMMARY"

# Uniform-Random: Injection rates 0.01 to 0.40 inclusive, step 0.03
# Shuffle: Injection rates 0.01 to 0.20 inclusive, step 0.01
# Transpose: Injection rates 0.01 to 0.20 inclusive, step 0.01
# Bit Complement: Injection rates 0.01 to 0.20 inclusive, step 0.01
# Bit Reverse: Injection rates 0.01 to 0.20 inclusive, step 0.01
# Bit Rotation: Injection rates 0.01 to 0.20 inclusive, step 0.01
# Neighbor: Injection rates 0.01 to 0.40 inclusive, step 0.03
for i in $(seq 0 13); do
    RATE=$(awk "BEGIN { printf \"%.2f\", 0.01 + $i * 0.03 }")
    OUTDIR="$RESULTS/dor_neighbor_rand_vnet${RATE}"
    mkdir -p "$OUTDIR"

    MTTF_FILE="$OUTDIR/mttf_dor_neighbor_rand_vnet${RATE}.txt"

    echo "=== Running DOR neighbor inj=${RATE} ==="

    $GEM5 --outdir="$OUTDIR" $CFG \
        --num-cpus=64 --num-dirs=64 \
        --network=garnet --topology=Mesh_XY --mesh-rows=8 \
        --sim-cycles=50000000 \
        --synthetic=neighbor \
        --injectionrate="$RATE" \
        --inj-vnet=-1 \
        --router-latency=1 --link-latency=1 --vcs-per-vnet=4 \
        --routing-algorithm=1 \
        --garnet-deadlock-threshold=20000000 \
        --mttf-output-file="$MTTF_FILE"

    STATS="$OUTDIR/stats.txt"

    # Extract latencies (ticks) and convert to cycles
    AVG_PKT=$(grep "^system\.ruby\.network\.average_packet_latency " "$STATS" \
              | awk "{printf \"%.4f\", \$2 / $TICKS_PER_CYCLE}")
    AVG_NET=$(grep "^system\.ruby\.network\.average_packet_network_latency " "$STATS" \
              | awk "{printf \"%.4f\", \$2 / $TICKS_PER_CYCLE}")
    AVG_Q=$(grep "^system\.ruby\.network\.average_packet_queueing_latency " "$STATS" \
            | awk "{printf \"%.4f\", \$2 / $TICKS_PER_CYCLE}")

    echo "$RATE,$AVG_PKT,$AVG_NET,$AVG_Q" >> "$SUMMARY"
    echo "  avg_packet_latency = ${AVG_PKT} cycles"
done

echo ""
echo "Done. Summary: $SUMMARY"
