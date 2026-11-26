#!/bin/bash
# =============================================================================
# test_all.sh - Run rrul test for all 4 queue discs and generate report.txt
# =============================================================================
# This script builds the simulation, runs rrul for PfifoFast, FqCoDel, FqPie,
# and FqCobalt (seed 1, nClients=3), and writes a single report.txt file
# with comprehensive results for each queue disc.
#
# Usage: ./test_all.sh [output_dir]
# Example: ./test_all.sh results
#
# Requires: ns-3 built with CMake (./ns3 build)
# Simulation file must be in scratch/aqm-bufferbloat-simulation.cc
# =============================================================================

set -e  # Exit on error (but we'll handle individual run failures)

OUTPUT_DIR="${1:-results_test_all}"
SIM_TIME=60
SEED=1
N_CLIENTS=3
TEST="rrul"
QUEUE_DISCS=("PfifoFast" "FqCoDel" "FqPie" "FqCobalt")

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=========================================="
echo "AQM Bufferbloat Test Suite - RRUL Test"
echo "=========================================="
echo "Output directory: $OUTPUT_DIR"
echo "Clients: $N_CLIENTS"
echo "Seed: $SEED"
echo "Simulation time: ${SIM_TIME}s"
echo "Queue discs: ${QUEUE_DISCS[*]}"
echo "=========================================="

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Report file
REPORT_FILE="$OUTPUT_DIR/report.txt"
> "$REPORT_FILE"  # Clear report file

# Function to log to both console and report
log() {
    echo -e "$1" | tee -a "$REPORT_FILE"
}

log "Test started at: $(date)"
log ""

# Build once
log "Building simulation..."
if ./ns3 build 2>&1 | tee -a "$REPORT_FILE"; then
    log "${GREEN}Build succeeded${NC}"
else
    log "${RED}Build failed${NC}"
    exit 1
fi
log ""

# Track results for comparison
declare -A run_status
declare -A json_paths
declare -A json_sizes
declare -A avg_rtt
declare -A max_rtt
declare -A enqueued
declare -A dequeued
declare -A dropped
declare -A bottleneck_config

# Run each queue disc
for QDISC in "${QUEUE_DISCS[@]}"; do
    log "=========================================="
    log "Running: QueueDisc=$QDISC Test=$TEST Seed=$SEED"
    log "=========================================="

    OUTPUT_SUBDIR="$OUTPUT_DIR/${QDISC}_${TEST}_s${SEED}"
    mkdir -p "$OUTPUT_SUBDIR"

    # Run simulation
    RUN_OUTPUT=$(mktemp)
    RUN_ERROR=$(mktemp)

    if ./ns3 run "scratch/aqm-bufferbloat-simulation \
        --queueDisc=$QDISC \
        --test=$TEST \
        --nClients=$N_CLIENTS \
        --seed=$SEED \
        --run=$SEED \
        --simTime=$SIM_TIME \
        --outputDir=$OUTPUT_SUBDIR" > "$RUN_OUTPUT" 2> "$RUN_ERROR"; then
        RUN_EXIT=0
    else
        RUN_EXIT=$?
    fi

    # Read output
    OUTPUT_CONTENT=$(cat "$RUN_OUTPUT")
    ERROR_CONTENT=$(cat "$RUN_ERROR")

    # Check if run succeeded
    if [ $RUN_EXIT -eq 0 ]; then
        run_status[$QDISC]="SUCCESS"
        log "${GREEN}Run succeeded${NC}"
    else
        run_status[$QDISC]="FAILED (exit code $RUN_EXIT)"
        log "${RED}Run failed (exit code $RUN_EXIT)${NC}"
        if [ -s "$RUN_ERROR" ]; then
            log "${RED}Error output:${NC}"
            cat "$RUN_ERROR" | tee -a "$REPORT_FILE"
        fi
        if [ -s "$RUN_OUTPUT" ]; then
            log "${YELLOW}Standard output:${NC}"
            cat "$RUN_OUTPUT" | tee -a "$REPORT_FILE"
        fi
    fi

    # Always include stdout in report for debugging
    echo "$OUTPUT_CONTENT" >> "$REPORT_FILE"

    # Extract key metrics from output
    # Queue Disc Type line
    QDISC_TYPE=$(echo "$OUTPUT_CONTENT" | grep "Queue Disc Type:" | head -1)
    if [ -n "$QDISC_TYPE" ]; then
        log "  $QDISC_TYPE"
    fi

    # BQL status
    BQL_LINE=$(echo "$OUTPUT_CONTENT" | grep "BQL (DynamicQueueLimits):" | head -1)
    if [ -n "$BQL_LINE" ]; then
        log "  $BQL_LINE"
    fi

    # Bottleneck configuration
    BOTTLENECK=$(echo "$OUTPUT_CONTENT" | sed -n '/=== Bottleneck Link Configuration ===/,/===/p' | head -10)
    if [ -n "$BOTTLENECK" ]; then
        bottleneck_config[$QDISC]="$BOTTLENECK"
        echo "$BOTTLENECK" >> "$REPORT_FILE"
    fi

    # Queue disc statistics (R1->R2 and R2->R1)
    STATS_R1=$(echo "$OUTPUT_CONTENT" | sed -n '/--- R1->R2 (upload\/UDP direction) ---/,/---/p' | head -8)
    STATS_R2=$(echo "$OUTPUT_CONTENT" | sed -n '/--- R2->R1 (download direction) ---/,/---/p' | head -8)

    if [ -n "$STATS_R1" ]; then
        log "  R1->R2 stats:"
        echo "$STATS_R1" | tee -a "$REPORT_FILE"
        # Extract enqueued, dequeued, dropped
        ENQ1=$(echo "$STATS_R1" | grep "Total enqueued:" | sed 's/.*Total enqueued:[[:space:]]*//')
        DEQ1=$(echo "$STATS_R1" | grep "Total dequeued:" | sed 's/.*Total dequeued:[[:space:]]*//')
        DRP1=$(echo "$STATS_R1" | grep "Total dropped:" | sed 's/.*Total dropped:[[:space:]]*//')
    fi

    if [ -n "$STATS_R2" ]; then
        log "  R2->R1 stats:"
        echo "$STATS_R2" | tee -a "$REPORT_FILE"
        ENQ2=$(echo "$STATS_R2" | grep "Total enqueued:" | sed 's/.*Total enqueued:[[:space:]]*//')
        DEQ2=$(echo "$STATS_R2" | grep "Total dequeued:" | sed 's/.*Total dequeued:[[:space:]]*//')
        DRP2=$(echo "$STATS_R2" | grep "Total dropped:" | sed 's/.*Total dropped:[[:space:]]*//')
    fi

    # Sum totals
    enqueued[$QDISC]="$(( ${ENQ1:-0} + ${ENQ2:-0} ))"
    dequeued[$QDISC]="$(( ${DEQ1:-0} + ${DEQ2:-0} ))"
    dropped[$QDISC]="$(( ${DRP1:-0} + ${DRP2:-0} ))"

    # Ping RTT from Flow Monitor (ICMP)
    ICMP_RTT=$(echo "$OUTPUT_CONTENT" | grep -A2 "proto 1" | grep "Mean Delay:" | sed 's/.*Mean Delay:[[:space:]]*//' | sed 's/[[:space:]]ms//')
    if [ -n "$ICMP_RTT" ]; then
        avg_rtt[$QDISC]="$ICMP_RTT"
    fi

    # Max RTT - look for it in the output
    MAX_RTT=$(echo "$OUTPUT_CONTENT" | grep -i "max.*rtt\|max.*ping\|p99" | head -1 | sed 's/.*://' | sed 's/[[:space:]]ms//' | sed 's/[[:space:]]*$//')
    if [ -n "$MAX_RTT" ]; then
        max_rtt[$QDISC]="$MAX_RTT"
    else
        max_rtt[$QDISC]="N/A"
    fi

    # Find JSON file
    JSON_FILE=$(find "$OUTPUT_SUBDIR" -name "*.json" -type f | head -1)
    if [ -n "$JSON_FILE" ] && [ -f "$JSON_FILE" ]; then
        json_paths[$QDISC]="$JSON_FILE"
        SIZE=$(stat -c%s "$JSON_FILE" 2>/dev/null || stat -f%z "$JSON_FILE" 2>/dev/null || echo "unknown")
        json_sizes[$QDISC]="$SIZE bytes"
        log "  ${GREEN}JSON found:${NC} $JSON_FILE ($SIZE bytes)"
    else
        json_paths[$QDISC]="MISSING"
        json_sizes[$QDISC]="N/A"
        log "  ${RED}JSON file MISSING${NC} in $OUTPUT_SUBDIR"
    fi

    log ""

    # Cleanup temp files
    rm -f "$RUN_OUTPUT" "$RUN_ERROR"
done

# =============================================================================
# Generate Summary Report
# =============================================================================
log "=========================================="
log "SUMMARY REPORT"
log "=========================================="
log ""

# Check for identical results
log "Checking for identical results across queue discs..."
# Compare total throughput (from stats) - if all queue discs have same enqueued/dequeued/dropped
ENQ_VALUES=()
DEQ_VALUES=()
DRP_VALUES=()
for QDISC in "${QUEUE_DISCS[@]}"; do
    ENQ_VALUES+=("${enqueued[$QDISC]}")
    DEQ_VALUES+=("${dequeued[$QDISC]}")
    DRP_VALUES+=("${dropped[$QDISC]}")
done

# Check if all values are identical
if [ "${ENQ_VALUES[0]}" = "${ENQ_VALUES[1]}" ] && [ "${ENQ_VALUES[1]}" = "${ENQ_VALUES[2]}" ] && [ "${ENQ_VALUES[2]}" = "${ENQ_VALUES[3]}" ] && \
   [ "${DEQ_VALUES[0]}" = "${DEQ_VALUES[1]}" ] && [ "${DEQ_VALUES[1]}" = "${DEQ_VALUES[2]}" ] && [ "${DEQ_VALUES[2]}" = "${DEQ_VALUES[3]}" ] && \
   [ "${DRP_VALUES[0]}" = "${DRP_VALUES[1]}" ] && [ "${DRP_VALUES[1]}" = "${DRP_VALUES[2]}" ] && [ "${DRP_VALUES[2]}" = "${DRP_VALUES[3]}" ]; then
    log "${YELLOW}WARNING: All four queue discs produced IDENTICAL enqueued/dequeued/dropped totals!${NC}"
    log "${YELLOW}This may indicate the AQM is not being applied correctly.${NC}"
    log ""
fi

# Per-queue-disc summary table
log "Per-Queue-Disc Results:"
log ""
printf "%-12s %-10s %-15s %-15s %-15s %-15s %-12s %-12s %-12s %s\n" \
    "QueueDisc" "Status" "Avg RTT (ms)" "Max RTT (ms)" "Enqueued" "Dequeued" "Dropped" "JSON Size" "JSON Status" >> "$REPORT_FILE"
printf "%-12s %-10s %-15s %-15s %-15s %-15s %-12s %-12s %-12s %s\n" \
    "QueueDisc" "Status" "Avg RTT (ms)" "Max RTT (ms)" "Enqueued" "Dequeued" "Dropped" "JSON Size" "JSON Status"

for QDISC in "${QUEUE_DISCS[@]}"; do
    printf "%-12s %-10s %-15s %-15s %-15s %-15s %-12s %-12s %-12s %s\n" \
        "$QDISC" \
        "${run_status[$QDISC]}" \
        "${avg_rtt[$QDISC]:-N/A}" \
        "${max_rtt[$QDISC]:-N/A}" \
        "${enqueued[$QDISC]:-N/A}" \
        "${dequeued[$QDISC]:-N/A}" \
        "${dropped[$QDISC]:-N/A}" \
        "${json_sizes[$QDISC]}" \
        "$( [ "${json_paths[$QDISC]}" = "MISSING" ] && echo "MISSING" || echo "OK" )" \
        | tee -a "$REPORT_FILE"
done
log ""

# Detailed results per queue disc
for QDISC in "${QUEUE_DISCS[@]}"; do
    log "--- $QDISC ---"
    log "  Status: ${run_status[$QDISC]}"
    log "  Avg ICMP RTT: ${avg_rtt[$QDISC]:-N/A} ms"
    log "  Max RTT: ${max_rtt[$QDISC]:-N/A} ms"
    log "  Total Enqueued: ${enqueued[$QDISC]:-N/A}"
    log "  Total Dequeued: ${dequeued[$QDISC]:-N/A}"
    log "  Total Dropped: ${dropped[$QDISC]:-N/A}"
    log "  JSON File: ${json_paths[$QDISC]}"
    log "  JSON Size: ${json_sizes[$QDISC]}"
    if [ -n "${bottleneck_config[$QDISC]}" ]; then
        log "  Bottleneck Config:"
        echo "${bottleneck_config[$QDISC]}" | while IFS= read -r line; do
            log "    $line"
        done
    fi
    log ""
done

# Flag any issues
log "=========================================="
log "ISSUES DETECTED"
log "=========================================="
ISSUES=0

for QDISC in "${QUEUE_DISCS[@]}"; do
    if [ "${run_status[$QDISC]}" != "SUCCESS" ]; then
        log "${RED}FAILURE: $QDISC run failed - ${run_status[$QDISC]}${NC}"
        ISSUES=$((ISSUES + 1))
    fi
    if [ "${json_paths[$QDISC]}" = "MISSING" ]; then
        log "${RED}MISSING JSON: $QDISC - no Flent JSON output file found${NC}"
        ISSUES=$((ISSUES + 1))
    fi
done

if [ $ISSUES -eq 0 ]; then
    log "${GREEN}No issues detected. All runs completed successfully with JSON output.${NC}"
else
    log "${RED}Total issues: $ISSUES${NC}"
fi

log ""
log "Test completed at: $(date)"
log "Report saved to: $REPORT_FILE"
log "=========================================="

# Exit with error code if any issues
if [ $ISSUES -gt 0 ]; then
    exit 1
else
    exit 0
fi