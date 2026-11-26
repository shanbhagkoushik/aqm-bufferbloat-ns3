# AQM Bufferbloat Mitigation Analysis

Performance analysis of four Active Queue Management (AQM) algorithms for mitigating bufferbloat in TCP/UDP networks using ns-3 with FlentApplication.

## Overview

This project evaluates the effectiveness of different AQM algorithms in reducing bufferbloat — the excessive latency caused by bloated network queues — under various traffic conditions.

## Topology

```
    C0 ──┐
    C1 ──┤     10 Mbps, 10 ms      ┌── D0
    C2 ──┤   R1 ────────────────── R2 ──┤
         │                          │   ├── D1
         └──────────────────────────┘   └── D2
```

- **3 Flent client nodes** (C0, C1, C2) on left
- **3 server nodes** (D0, D1, D2) on right
- **2 routers** (R1, R2) with a single bottleneck link
- **Bottleneck**: 10 Mbps bandwidth, 10 ms one-way delay
- **Queue discs** applied on **both** bottleneck egress interfaces (R1→R2 and R2→R1)

## Queue Disciplines Tested

| Queue Disc | TypeId | Description |
|------------|--------|-------------|
| PfifoFast | `ns3::PfifoFastQueueDisc` | Baseline: simple FIFO with 3 bands |
| FqCoDel | `ns3::FqCoDelQueueDisc` | Flow Queue Controlled Delay |
| FqPie | `ns3::FqPieQueueDisc` | Flow Queue PIE (Proportional Integral) |
| FqCobalt | `ns3::FqCobaltQueueDisc` | Flow Queue COBALT (combines CoDel + BLUE) |

## Test Scenarios (FlentApplication)

| Test | Description | TCP Flows | UDP Flows |
|------|-------------|-----------|-----------|
| `tcp_upload` | 1 TCP flow per client (client→server) | 3 | 0 |
| `tcp_download` | 1 TCP flow per client (server→client) | 3 | 0 |
| `rrul` | 8 TCP (4 up + 4 down per client, DSCP BE/BK/CS5/EF) + 3 UDP echo per client | 24 | 9 |

## Metrics Collected

1. **Throughput** (Mbps) — per-flow and aggregate
2. **Latency** (ms) — average, median (p50), p99
3. **Loss rate** (%)
4. **Jain's Fairness Index (JFI)**
   - **Flow-level**: Fairness across all TCP flows (24 for rrul, 3 for single-flow tests)
   - **Client-level**: Fairness across 3 per-client aggregate TCP throughputs

## Requirements

- **ns-3-dev** master branch at commit `bd2e108bf` (or later)
- **FlentApplication** module (merged in master, or from branch `GSoC-2026-flent-application` at `gitlab.com/k.kedar/ns-3-dev`)
- C++17 compatible compiler
- Python 3.8+ with `numpy`, `pandas`, `matplotlib` for analysis

## Build & Run

### 1. Build ns-3 with FlentApplication

```bash
# Clone ns-3 with FlentApplication
git clone https://gitlab.com/k.kedar/ns-3-dev.git
cd ns-3-dev
git checkout GSoC-2026-flent-application

# Build
./ns3 configure --enable-examples --enable-tests
./ns3 build
```

### 2. Copy simulation to scratch

```bash
cp ../aqm-bufferbloat-ns3/sim/aqm-bufferbloat-simulation.cc scratch/
```

### 3. Run all simulations

```bash
# From ns-3-dev root
cd ../aqm-bufferbloat-ns3
./scripts/run_all_sims.sh results 1 2 3 4 5
```

This runs 4 queue discs × 3 tests × 5 seeds = **60 simulations**.
Output directories: `results/{QDISC}_{TEST}_s{SEED}/`

### 4. Analyze results

```bash
cd ../aqm-bufferbloat-ns3
python3 analysis/analyze_results.py results/ --output-dir analysis_output
```

Outputs:
- `analysis_output/summary.csv` — aggregated statistics (mean ± std)
- `analysis_output/plots/` — grouped bar charts with error bars (PNG)

## Results

| Queue Disc | Test | Throughput (Mbps) | Latency p99 (ms) | Loss (%) | JFI (flow) | JFI (client) |
|------------|------|-------------------|------------------|----------|------------|--------------|
| PfifoFast | tcp_upload | — | — | — | — | — |
| FqCoDel | tcp_upload | — | — | — | — | — |
| FqPie | tcp_upload | — | — | — | — | — |
| FqCobalt | tcp_upload | — | — | — | — | — |
| PfifoFast | tcp_download | — | — | — | — | — |
| FqCoDel | tcp_download | — | — | — | — | — |
| FqPie | tcp_download | — | — | — | — | — |
| FqCobalt | tcp_download | — | — | — | — | — |
| PfifoFast | rrul | — | — | — | — | — |
| FqCoDel | rrul | — | — | — | — | — |
| FqPie | rrul | — | — | — | — | — |
| FqCobalt | rrul | — | — | — | — | — |

*Run the simulation to populate this table. See `results/aqm_results.csv` after analysis.*

## Limitations

1. **Single bottleneck topology** — real networks have multiple bottlenecks
2. **Fixed link parameters** — 10 Mbps / 10 ms may not represent all scenarios
3. **FlentApplication only** — no custom traffic generators (BulkSend, OnOff)
4. **Queue size fixed at 1000 packets** — not parameterized
5. **5 seeds** — more seeds would improve statistical confidence
6. **No BQL/TSO** — Byte Queue Limits and TCP Segmentation Offload not modeled
7. **ns-3 model fidelity** — simulation results may differ from real hardware

## License

GPL-2.0 — see [LICENSE](LICENSE) for details.