#!/usr/bin/env bash
# Sweep Clotho-GAR routing over multiple traffic patterns at varying injection rates.
# Results go to m5out/results/clotho_<pattern>_<rate>/
# A per-pattern summary CSV is written to m5out/results/clotho_<pattern>_summary.csv
#
# Injection rate ranges (matching run_rl_sweep.sh comment):
#   uniform_random : 0.01 to 0.40 inclusive, step 0.03  (14 points)
#   shuffle        : 0.01 to 0.20 inclusive, step 0.01  (20 points)
#   transpose      : 0.01 to 0.20 inclusive, step 0.01  (20 points)
#   bit_reverse    : 0.01 to 0.20 inclusive, step 0.01  (20 points)
#   bit_rotation   : 0.01 to 0.20 inclusive, step 0.01  (20 points)
#   neighbor       : 0.01 to 0.40 inclusive, step 0.03  (14 points)

set -euo pipefail

GEM5=./build/NULL/gem5.opt
CFG=configs/example/garnet_synth_traffic.py
RESULTS=m5out/results
TICKS_PER_CYCLE=1000   # 1ps tick resolution, 1GHz clock

mkdir -p "$RESULTS"

run_pattern() {
    local PATTERN=$1
    local STEPS=$2
    local STEP_SIZE=$3

    local SUMMARY="$RESULTS/clotho_${PATTERN}_rand_vnet_summary.csv"
    echo "injection_rate,avg_packet_latency_cycles,avg_packet_network_latency_cycles,avg_packet_queueing_latency_cycles" > "$SUMMARY"

    echo ""
    echo "=== Pattern: ${PATTERN} (${STEPS} rates, step ${STEP_SIZE}) ==="

    for i in $(seq 0 $(( STEPS - 1 ))); do
        RATE=$(awk "BEGIN { printf \"%.2f\", 0.01 + $i * $STEP_SIZE }")
        OUTDIR="$RESULTS/clotho_${PATTERN}_rand_vnet${RATE}"
        mkdir -p "$OUTDIR"

        MTTF_FILE="$OUTDIR/mttf_clotho_${PATTERN}_rand_vnet${RATE}.txt"

        echo "  inj=${RATE}"

        $GEM5 --outdir="$OUTDIR" $CFG \
            --num-cpus=64 --num-dirs=64 \
            --network=garnet --topology=Mesh_XY --mesh-rows=8 \
            --sim-cycles=50000000 \
            --synthetic="$PATTERN" \
            --injectionrate="$RATE" \
            --inj-vnet=-1 \
            --router-latency=1 --link-latency=1 --vcs-per-vnet=4 \
            --routing-algorithm=3 \
            --garnet-deadlock-threshold=20000000 \
            --mttf-output-file="$MTTF_FILE"

        STATS="$OUTDIR/stats.txt"

        AVG_PKT=$(grep "^system\.ruby\.network\.average_packet_latency " "$STATS" \
                  | awk "{printf \"%.4f\", \$2 / $TICKS_PER_CYCLE}")
        AVG_NET=$(grep "^system\.ruby\.network\.average_packet_network_latency " "$STATS" \
                  | awk "{printf \"%.4f\", \$2 / $TICKS_PER_CYCLE}")
        AVG_Q=$(grep "^system\.ruby\.network\.average_packet_queueing_latency " "$STATS" \
                | awk "{printf \"%.4f\", \$2 / $TICKS_PER_CYCLE}")

        echo "$RATE,$AVG_PKT,$AVG_NET,$AVG_Q" >> "$SUMMARY"
        echo "    avg_packet_latency = ${AVG_PKT} cycles"
    done

    echo "  Summary: $SUMMARY"
}

# uniform_random: 0.01–0.40, step 0.03 (14 points)
run_pattern "uniform_random" 14 0.03

# shuffle: 0.01–0.20, step 0.01 (20 points)
run_pattern "shuffle" 20 0.01

# transpose: 0.01–0.20, step 0.01 (20 points)
run_pattern "transpose" 20 0.01

# bit_reverse: 0.01–0.20, step 0.01 (20 points)
run_pattern "bit_reverse" 20 0.01

# bit_rotation: 0.01–0.20, step 0.01 (20 points)
run_pattern "bit_rotation" 20 0.01

# neighbor: 0.01–0.40, step 0.03 (14 points)
run_pattern "neighbor" 14 0.03

echo ""
echo "All Clotho-GAR sweeps complete. Summaries in $RESULTS/clotho_*_rand_vnet_summary.csv"
