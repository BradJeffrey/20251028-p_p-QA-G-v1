# sPHENIX Real Data QA Pipeline

A reproducible quality-assurance pipeline for sPHENIX detector data, built on CERN ROOT. It extracts physics metrics from per-run histogram files, aggregates them with configurable weighting, detects outliers via robust z-scores, and **automatically diagnoses anomalies with physics-informed reasoning**.

The pipeline currently targets **INTT** (intermediate silicon tracker) histograms across 10 metrics covering ADC distributions, cluster properties, beam-clock offsets, and sensor occupancy. It produces per-run verdicts (GOOD / SUSPECT / BAD) with explanations of likely hardware, physics, or engineering causes for any flags.

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
| **Dashboard** | `dashboard` | Config-driven trend plots and auto-sized summary dashboard (PNG + PDF) |
| **Merge** | `merge` | Joins all per-run CSVs into a single wide-format CSV |
| **Consistency** | `analyze` | Runs physics consistency checks across metrics |
| **QA Report** | `qa-report` | Generates `REPORT.md` with per-metric statistics and health overview |
| **PCA** | `pca` | Multi-metric principal component analysis |
| **Control charts** | `control` | Statistical process control charts |
| **Fit quality** | `fit-quality` | Physics-informed fit assessment (Landau, uniformity chi2, Fourier) |
| **Verdict** | `verdict` | Automated run verdicts with physics-informed diagnosis |
| **Report** | `report` | Consolidated QA report PDF |
| **Smoke test** | `smoke-test` | Shell-based pipeline validation (no Python dependency) |

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
| `THRESH` | `configs/thresholds.csv` | Per-metric hard threshold bounds |
| `MARKERS` | `configs/markers.csv` | Known-event markers (beam trips, calibrations) |

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
│   ├── configs/                # YAML + CSV configs (thresholds, markers, explanations)
│   ├── scripts/                # Validation & utility scripts (smoke_test.sh)
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
| `extract_metrics_v2.C` | Config-driven metric extraction (full method parity with extract_quick) |
| `plot_dashboard.C` | Config-driven trend plots and auto-sized summary dashboard |
| `generate_report_md.C` | Generates `REPORT.md` with per-metric stats and health overview |
| `analyze_consistency_v2.C` | Physics consistency checks with threshold & marker support |
| `merge_per_run.C` | Wide-format CSV merging |
| `pca_multimetric.C` | PCA across metrics |
| `intt_ladder_health.C` | INTT detector health diagnostics |
| `control_charts.C` | Statistical control charts |
| `verdict_engine.C` | Automated physics-informed run verdicts and diagnosis |
| `fit_quality.C` | Per-histogram fit quality assessment (Landau, chi2, Fourier) |

## Outputs

After a full pipeline run, `20250928/out/` contains:

- **`metrics_*.csv`** -- per-metric, per-file measurements (columns: `run, segment, file, value, error, weight`)
- **`metrics_*_perrun.csv`** -- per-run aggregates with robust z-score columns
- **`metrics_perrun_wide.csv`** -- all metrics joined into one row per run
- **`metric_*_perrun.{png,pdf}`** -- per-metric trend plots with outlier annotations
- **`dashboard_NxM.{png,pdf}`** -- auto-sized summary dashboard (grid scales with metric count)
- **`REPORT.md`** -- per-metric statistics table and health overview
- **`VERDICT.md`** -- automated physics-informed run verdicts and diagnosis report
- **`verdicts.csv`** -- per-run, per-metric machine-readable verdicts
- **`run_verdicts.csv`** -- per-run aggregate verdict (GOOD/SUSPECT/BAD)
- **`fit_quality.csv`** -- per-histogram fit results with quality flags
- **`consistency_summary.csv`** -- physics consistency flags
- **`_stamp.txt`** -- session metadata (date, run range, config)

## Automated verdict system

The pipeline includes a physics-informed verdict engine that automatically classifies each run:

- **GOOD** -- all metrics within expected ranges
- **SUSPECT** -- one or more metrics flagged for review
- **BAD** -- severe anomalies detected; run recommended for exclusion

For every flagged run, the engine reports:
- **Pattern**: what kind of anomaly (spike, gradual drift, step change, sustained shift)
- **Causes**: plausible physics/hardware/engineering explanations (e.g., "Temperature-dependent gain drift in INTT silicon sensors")
- **Action**: recommended next step (e.g., "Check INTT temperature logs and HV records")

The reasoning is driven by a physics knowledge base (`configs/physics_rules.yaml`) that encodes detector-specific domain knowledge. The engine reads:
- Robust z-scores from per-run CSVs
- QC status from consistency analysis
- Control chart flags (Shewhart + CUSUM)
- INTT ladder health (dead/hot counts)
- Fit quality assessments

Results are in `out/VERDICT.md` (human-readable) and `out/verdicts.csv` (machine-readable).

## Validation

Run the smoke test to verify pipeline outputs without Python dependencies:

```bash
make smoke-test
```

This checks that all expected CSVs, columns, and stamp files exist, and reports NaN rates per metric.

## Known issues

- `intt_hits_asym` produces all-NaN values across the current 13 runs (sensor occupancy histograms are empty in this data). This is a data limitation, not a code bug.
- `derived` and `control` targets reference physqa metrics (e.g. Landau MPV, TPC uniformity) that are not in `metrics.conf`. These targets fail gracefully via Make's `-` prefix and will activate when multi-subsystem data is available.

## Current data

- **Run range**: 66522 -- 68604 (13 runs)
- **Last run date**: 2025-11-18
- **Active metrics**: 10 (see `metrics.conf`)
