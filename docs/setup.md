# Setup Guide

## Prerequisites

### ns-3 with FlentApplication

The simulation requires ns-3 with the FlentApplication module. This is available in:

- **ns-3-dev master** (commit `bd2e108bf` or later) — FlentApplication merged
- **Branch**: `GSoC-2026-flent-application` at `gitlab.com/k.kedar/ns-3-dev`

```bash
# Option 1: Use the FlentApplication branch
git clone https://gitlab.com/k.kedar/ns-3-dev.git
cd ns-3-dev
git checkout GSoC-2026-flent-application

# Option 2: Use ns-3-dev master (if FlentApplication is merged)
git clone https://gitlab.com/nsnam/ns-3-dev.git
cd ns-3-dev
git checkout bd2e108bf  # or later
```

### Build ns-3

```bash
# With NetAnim support (for visualization)
./ns3 configure --enable-examples --enable-tests --enable-netanim
./ns3 build
```

### NetAnim Viewer

Download NetAnim from https://www.nsnam.org/netanim/ or install via package manager:
```bash
# Ubuntu/Debian
sudo apt-get install netanim

# Or download binary from ns-3 website
```

### Python Dependencies (for analysis)

```bash
pip install numpy pandas matplotlib
```

## Project Setup

```bash
# Clone this repository
git clone <this-repo>
cd aqm-bufferbloat-ns3

# Copy simulation to ns-3 scratch directory
cp sim/aqm-bufferbloat-simulation.cc /path/to/ns-3-dev/scratch/

# Or symlink for easier development
ln -s $(pwd)/sim/aqm-bufferbloat-simulation.cc /path/to/ns-3-dev/scratch/
```

## Running Simulations

### Quick test (single run)

```bash
cd /path/to/ns-3-dev
./ns3 run "scratch/aqm-bufferbloat-simulation --queueDisc=FqCoDel --test=rrul --nClients=3 --seed=1 --run=1 --simTime=60 --outputDir=/tmp/test"
```

### Quick test with NetAnim visualization

```bash
cd /path/to/ns-3-dev
./ns3 run "scratch/aqm-bufferbloat-simulation --queueDisc=FqCoDel --test=rrul --nClients=3 --seed=1 --run=1 --simTime=60 --outputDir=/tmp/test --enableNetAnim=1"
# Output: /tmp/test/aqm-FqCoDel-rrul-s1.xml
# Open with: netanim /tmp/test/aqm-FqCoDel-rrul-s1.xml
```

### Full batch (60 runs)

```bash
cd /path/to/aqm-bufferbloat-ns3
./scripts/run_all_sims.sh results 1 2 3 4 5
```

Output: `results/{QDISC}_{TEST}_s{SEED}/` with Flent JSON files.

### Full batch with NetAnim (generates 60 .xml files)

```bash
cd /path/to/aqm-bufferbloat-ns3
ENABLE_NETANIM=1 ./scripts/run_all_sims.sh results 1 2 3 4 5
```

## Analyzing Results

```bash
cd /path/to/aqm-bufferbloat-ns3
python3 analysis/analyze_results.py results/ --output-dir analysis_output
```

Outputs:
- `analysis_output/summary.csv` — aggregated statistics
- `analysis_output/plots/*.png` — grouped bar charts

## Verifying Installation

```bash
# Check FlentApplication is available
./ns3 run "scratch/aqm-bufferbloat-simulation --help" 2>&1 | grep -i flent

# Run API verification
./scripts/test_all.sh
```

## Directory Structure

```
aqm-bufferbloat-ns3/
├── sim/
│   └── aqm-bufferbloat-simulation.cc   # ns-3 simulation
├── scripts/
│   ├── run_all_sims.sh                 # 60-run batch
│   └── test_all.sh                     # API check + smoke test
├── analysis/
│   └── analyze_results.py              # JSON parsing, stats, plots
├── results/
│   └── aqm_results.csv                 # Template (populated after run)
├── docs/
│   ├── synopsis.md                     # Project synopsis
│   └── setup.md                        # This file
├── README.md                           # Full documentation
├── LICENSE                             # GPL-2.0
├── .gitignore
└── .gitattributes
```