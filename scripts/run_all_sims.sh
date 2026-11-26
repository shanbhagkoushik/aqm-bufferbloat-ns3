#!/bin/bash
# =============================================================================
# Run all AQM simulation combinations
# =============================================================================
# This script runs the simulation for all 4 queue discs × 3 tests = 12 combinations
# with multiple seeds for statistical validity.
#
# Usage: ./run_all_sims.sh [output_dir] [seeds...]
# Example: ./run_all_sims.sh results 1 2 3 4 5
#
# Requires: ns-3 built with CMake (./ns3 build)
# Simulation file must be in scratch/aqm-bufferbloat-simulation.cc
# =============================================================================

set -e  # Exit on error

OUTPUT_DIR="${1:-results}"
SEEDS=("${@:2}")  # All arguments after first
if [ ${#SEEDS[@]} -eq 0 ]; then
    SEEDS=(1 2 3 4 5)  # Default: 5 seeds
fi

# Number of client/server pairs (default 3)
N_CLIENTS=3

# Queue discs to test (FqCobaltQueueDisc exists in ns-3 master)
QUEUE_DISCS=("PfifoFast" "FqCoDel" "FqPie" "FqCobalt")

# FlentApplication test names (from FlentApplication source)
TESTS=("tcp_upload" "tcp_download" "rrul")

# Simulation time (seconds) - Flent tests typically run 60s
SIM_TIME=60

# Enable NetAnim (set to 1 to generate .xml files for visualization)
ENABLE_NETANIM=0

# Create output directory
mkdir -p "$OUTPUT_DIR"

echo "=========================================="
echo "AQM Bufferbloat Simulation Suite"
echo "=========================================="
echo "Output directory: $OUTPUT_DIR"
echo "Clients: $N_CLIENTS"
echo "Seeds: ${SEEDS[*]}"
echo "Queue discs: ${QUEUE_DISCS[*]}"
echo "Tests: ${TESTS[*]}"
echo "Simulation time: ${SIM_TIME}s per run"
echo "Total runs: $((${#QUEUE_DISCS[@]} * ${#TESTS[@]} * ${#SEEDS[@]}))"
echo "=========================================="

# Build once
echo "Building simulation..."
./ns3 build

# Counter
TOTAL=$((${#QUEUE_DISCS[@]} * ${#TESTS[@]} * ${#SEEDS[@]}))
CURRENT=0

# Run all combinations
for QDISC in "${QUEUE_DISCS[@]}"; do
    for TEST in "${TESTS[@]}"; do
        for SEED in "${SEEDS[@]}"; do
            CURRENT=$((CURRENT + 1))
            RUN=$SEED  # Use seed as run number too

            echo ""
            echo "[$CURRENT/$TOTAL] QueueDisc=$QDISC Test=$TEST Seed=$SEED"

            # Directory name matches simulation: {QueueDisc}_{Test}_s{Seed}
            OUTPUT_SUBDIR="$OUTPUT_DIR/${QDISC}_${TEST}_s${SEED}"
            mkdir -p "$OUTPUT_SUBDIR"

            # Run simulation using ./ns3
            NETANIM_FLAG=""
            if [ "$ENABLE_NETANIM" -eq 1 ]; then
                NETANIM_FLAG="--enableNetAnim=1"
            fi
            ./ns3 run "scratch/aqm-bufferbloat-simulation \
                --queueDisc=$QDISC \
                --test=$TEST \
                --nClients=$N_CLIENTS \
                --seed=$SEED \
                --run=$RUN \
                --simTime=$SIM_TIME \
                --outputDir=$OUTPUT_SUBDIR \
                $NETANIM_FLAG"

            echo "  -> Results in $OUTPUT_SUBDIR"
        done
    done
done

echo ""
echo "=========================================="
echo "All simulations complete!"
echo "Results in: $OUTPUT_DIR"
echo "=========================================="

# Summary: list generated JSON files
echo ""
echo "Generated Flent JSON files:"
find "$OUTPUT_DIR" -name "*.json" -type f | sort