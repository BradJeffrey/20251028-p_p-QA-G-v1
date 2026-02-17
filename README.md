# sPHENIX Real Data QA Pipeline

A reproducible quality-assurance pipeline for sPHENIX detector data, built on CERN ROOT. It extracts physics metrics from per-run histogram files, aggregates them with configurable weighting, detects outliers via robust z-scores, and produces CSV summaries and visual dashboards.

The pipeline currently targets **INTT** (intermediate silicon tracker) histograms across 10 metrics covering ADC distributions, cluster properties, beam-clock offsets, and sensor occupancy.

## Quick start

### Prerequisites

- **ROOT >= 6.30** (C++ macros run via `root -l -b -q`)
- **GNU Make**
- **Git LFS** (input ROOT files and outputs are LFS-tracked)

A ready-to-go environment is provided via either:

- **Docker / Dev Container**: `.devcontainer/devcontainer.json` (ROOT 6.30.06 on Ubuntu 22)
- **Binder**: `.binder/environment.yml` (conda, Python 3.10 + ROOT)

### Running the pipeline

```bash
# Pull LFS objects (input ROOT files)
git lfs pull

# Run the core QA pipeline: extract -> aggregate -> robust z -> dashboard
make run-qa
```

This processes the 13 input ROOT files listed in `20250928/lists/files.txt` and writes results to `20250928/out/`.

For the full analysis (adds consistency checks, PCA, control charts, diagnostics, and reports):

```bash
make full
```

## Pipeline stages

| Stage | Makefile target | What it does |
|---|---|---|
| **Extract** | `extract` | Reads histograms from each ROOT file, computes per-file metric values, writes `metrics_*.csv` |
| **Aggregate** | `aggregate` | Pools per-file CSVs into per-run summaries (`metrics_*_perrun.csv`) with configurable weighting |
| **Robust Z** | `robust` | Appends local median, MAD, and z-score columns; flags weak (\|z\|>3) and strong (\|z\|>5) outliers |
| **Dashboard** | `dashboard` | Generates per-metric trend plots and a 2x2 summary dashboard (PNG + PDF) |
| **Merge** | `merge` | Joins all per-run CSVs into a single wide-format CSV |
| **Consistency** | `analyze` | Runs physics consistency checks across metrics |
| **PCA** | `pca` | Multi-metric principal component analysis |
| **Control charts** | `control` | Statistical process control charts |
| **Report** | `report` | Consolidated QA report PDF |

## Configuration

### `20250928/metrics.conf`

Defines which metrics to extract. Each line is:

```
metric_name, histogram_key, aggregation_method
```

Aggregation methods include `maxbin`, `median`, `p90`, `mean`, `rms`, `ks_uniform_p`, `chi2_uniform_red`, and `asym`.

### Makefile variables

| Variable | Default | Description |
|---|---|---|
| `LIST` | `lists/files.txt` | Input file list |
| `CONF` | `metrics.conf` | Metric definitions |
| `WEIGHTING` | `entries` | Aggregation weighting: `ivar`, `entries`, or `mean` |
| `ROBUST_W` | `5` | Sliding window width for robust z-scores |

## Project layout

```
.
├── Makefile                    # Top-level entry point (delegates to 20250928/)
├── 20250928/
│   ├── Makefile                # Primary build orchestration
│   ├── metrics.conf            # Metric definitions
│   ├── lists/files.txt         # Input ROOT file list
│   ├── data/                   # Input ROOT histogram files (LFS)
│   ├── macros/                 # ROOT C++ macros (~29 files)
│   ├── configs/                # YAML configs (cluster map, thresholds, explanations)
│   ├── scripts/                # Validation & utility scripts
│   ├── out/                    # All outputs: CSVs, plots, reports (LFS)
│   ├── docs/                   # Documentation & changelogs
│   └── diagnostics/            # Diagnostic output bundles
├── scripts/                    # Top-level utility scripts
├── .devcontainer/              # VS Code dev container config
└── .binder/                    # Binder environment config
```

## Key macros

| Macro | Purpose |
|---|---|
| `extract_quick.C` | Primary metric extraction from ROOT files |
| `aggregate_per_run_v2.C` | Weighted per-run aggregation |
| `add_robust_z.C` | Robust outlier detection (local median + MAD) |
| `plot_dashboard.C` | Trend plots and summary dashboard |
| `analyze_consistency_v2.C` | Physics consistency checks |
| `merge_per_run.C` | Wide-format CSV merging |
| `pca_multimetric.C` | PCA across metrics |
| `intt_ladder_health.C` | INTT detector health diagnostics |
| `control_charts.C` | Statistical control charts |

## Outputs

After a full pipeline run, `20250928/out/` contains:

- **`metrics_*.csv`** -- per-metric, per-file measurements (columns: `run, segment, file, value, error, weight`)
- **`metrics_*_perrun.csv`** -- per-run aggregates with robust z-score columns
- **`metrics_perrun_wide.csv`** -- all metrics joined into one row per run
- **`metric_*_perrun.{png,pdf}`** -- per-metric trend plots with outlier annotations
- **`dashboard_intt_2x2.{png,pdf}`** -- summary dashboard
- **`consistency_summary.csv`** -- physics consistency flags
- **`_stamp.txt`** -- session metadata (date, run range, config)

## Known issues

- `intt_hits_asym` can produce zero data range for some runs, causing ROOT axis warnings (plots still generate correctly).
- `extract_metrics_v2.C` (config-driven extractor) does not yet have full feature parity with `extract_quick.C`.
- One metric (`intt_hits_asym`) had NaN entries across all 13 runs in the last session.

## Current data

- **Run range**: 66522 -- 68604 (13 runs)
- **Last run date**: 2025-11-18
- **Active metrics**: 10 (see `metrics.conf`)
