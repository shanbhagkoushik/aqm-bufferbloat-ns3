# AQM Bufferbloat Mitigation Analysis - Project Synopsis

## Overview
This project analyzes the performance of four Active Queue Management (AQM) algorithms for mitigating bufferbloat in TCP/UDP networks using ns-3 with FlentApplication.

## Algorithms Tested
1. **PfifoFast** (baseline)
2. **FqCoDel** (Flow Queue Controlled Delay)
3. **FqPie** (Flow Queue Proportional Integral Controller Enhanced)
4. **FqCobalt** (Flow Queue COBALT)

## Topology
- Dumbbell topology with 3 client/server pairs
- Bottleneck link: 10 Mbps, 10 ms RTT
- Queue discs applied on BOTH bottleneck egress interfaces

## Test Scenarios (FlentApplication)
- **tcp_upload**: 3 TCP flows (1 per client → server)
- **tcp_download**: 3 TCP flows (1 per server → client)
- **rrul**: 24 TCP flows + 9 UDP flows (8 TCP + 3 UDP per client)

## Metrics
- Throughput (Mbps)
- Latency: average, p50, p99 (ms)
- Loss rate (%)
- Jain's Fairness Index (flow-level across all TCP flows, client-level across 3 clients)

## Methodology
- 5 RNG seeds per configuration for statistical validity
- Fixed simulation time: 60 seconds
- Results aggregated with mean ± std across seeds

## Requirements
- ns-3-dev master (commit bd2e108bf) with FlentApplication (GSoC-2026-flent-application branch)