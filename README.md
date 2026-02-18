# sPHENIX Real Data QA Pipeline

A reproducible quality-assurance pipeline for sPHENIX detector data, built on CERN ROOT. It extracts physics metrics from per-run histogram files, aggregates them with configurable weighting, detects outliers via robust z-scores, and **automatically diagnoses anomalies with physics-informed reasoning**.

The pipeline covers **three detector subsystems**: INTT (intermediate silicon tracker), MVTX (monolithic active pixel vertex tracker), and TPC (time projection chamber) across **27 metrics** covering ADC distributions, cluster properties, beam-clock offsets, sensor/chip health, laser timing, spatial resolution, and sector uniformity. It produces per-run verdicts (GOOD / SUSPECT / BAD) with explanations of likely hardware, physics, or engineering causes for any flags.

## Quick start

### Prerequisites

- **ROOT >= 6.30** (C++ macros run via `root -l -b -q`)
- **GNU Make**
- **Git LFS** (input ROOT files and outputs are LFS-tracked)

A ready-to-go environment is provided via either:

- **Docker / Dev Container**: `.devcontainer/devcontainer.json` (ROOT 6.30.06 on Ubuntu 22)
- **Binder**: `.binder/environment.yml` (conda, Python 3.10 + ROOT 6.30-6.34)

### Running the pipeline

```bash
# Pull LFS objects (input ROOT files)
git lfs pull

# Run the core QA pipeline: extract -> aggregate -> robust z -> dashboard
make run-qa
```

This processes the 13 input ROOT files listed in `20250928/lists/files.txt` and writes results to `20250928/out/`.

For the full analysis (adds consistency checks, PCA, correlation analysis, control charts, fit quality, diagnostics, verdicts, and reports):

```bash
make full
```

### Running on mock data (no LFS needed)

```bash
cd 20250928
root -l -b -q 'macros/make_mock_inputs.C()'
make full LIST=lists/mock_files.txt WEIGHTING=entries
```

## Pipeline stages

| Stage | Makefile target | What it does |
|---|---|---|
| **Extract** | `extract` | Config-driven metric extraction from ROOT files; `skip` method defers to physqa |
| **PhysQA Extract** | `physqa` | Physics-level extraction (Landau fits, Fourier, MVTX chip health, TPC laser timing) |
| **Aggregate** | `aggregate` | Pools per-file CSVs into per-run summaries with configurable weighting |
| **Robust Z** | `robust` | Appends local median, MAD, and z-score columns; flags weak (\|z\|>3) and strong (\|z\|>5) outliers |
| **Merge** | `merge` | Joins all per-run CSVs into a single wide-format CSV |
| **Consistency** | `analyze` | Physics consistency checks (trends, changepoints, threshold violations) |
| **Control charts** | `control` | Shewhart + CUSUM statistical process control (9 key metrics) |
| **PCA** | `pca` | Multi-metric PCA with scree plot, loadings heatmap, and Mahalanobis outlier detection |
| **Correlation** | `correlation` | Cross-metric Pearson correlation matrix and strong-pair flagging |
| **Fit quality** | `fit-quality` | Physics-informed fit assessment (Landau, uniformity chi2, Fourier) |
| **Dashboard** | `dashboard` | Config-driven trend plots and auto-sized summary dashboard (PNG + PDF) |
| **QA Report** | `qa-report` | Generates `REPORT.md` with per-metric statistics and health overview |
| **Verdict** | `verdict` | Automated run verdicts with physics-informed diagnosis |
| **Report** | `report` | Consolidated QA report PDF |
| **Smoke test** | `smoke-test` | Shell-based pipeline validation (no Python dependency) |

## Detector coverage

### INTT (10 core + 3 physics metrics)
- ADC peak, median, p90, Landau MPV
- Phi uniformity (KS + chi2), phi RMS
- BCO peak, BCO modulation R1
- Cluster size mean, sensor occupancy, hit asymmetry

### MVTX (6 metrics)
- Dead chip fraction per layer (L0, L1, L2)
- Hot chip fraction per layer (L0, L1, L2)

### TPC (8 metrics)
- Laser timing mean (North, South, N-S delta)
- Cluster phi/z size ring slope
- Spatial resolution (rphi, z)
- Sector ADC uniformity chi2

## Configuration

### `20250928/metrics.conf`

Defines which metrics to extract. Each line is:

```
metric_name, histogram_key, aggregation_method
```

Methods: `maxbin`, `median`, `p90`, `mean`, `rms`, `ks_uniform_p`, `chi2_uniform_red`, `asym`, and `skip` (extraction handled by `physqa_extract.C`).

### Makefile variables

| Variable | Default | Description |
|---|---|---|
| `LIST` | `lists/files.txt` | Input file list |
| `CONF` | `metrics.conf` | Metric definitions |
| `WEIGHTING` | `ivar` | Aggregation weighting: `ivar`, `entries`, or `mean` |
| `ROBUST_W` | `5` | Sliding window width for robust z-scores |
| `THRESH` | `configs/thresholds.csv` | Per-metric hard threshold bounds |
| `MARKERS` | `configs/markers.csv` | Known-event markers (beam trips, calibrations) |

## Project layout

```
.
├── Makefile                    # Top-level entry point (delegates to 20250928/)
├── 20250928/
│   ├── Makefile                # Primary build orchestration
│   ├── metrics.conf            # Metric definitions (27 metrics)
│   ├── lists/files.txt         # Input ROOT file list
│   ├── data/                   # Input ROOT histogram files (LFS)
│   ├── macros/                 # ROOT C++ macros
│   ├── configs/                # YAML + CSV configs (thresholds, markers, explanations, physics rules)
│   ├── scripts/                # Validation & utility scripts (smoke_test.sh)
│   ├── out/                    # All outputs: CSVs, plots, reports (LFS)
│   ├── docs/                   # Documentation & changelogs
│   └── diagnostics/            # Diagnostic output bundles
├── scripts/                    # Top-level utility scripts
├── .github/workflows/          # GitHub Actions CI (smoke test)
├── .devcontainer/              # VS Code dev container config
└── .binder/                    # Binder environment config
```

## Key macros

| Macro | Purpose |
|---|---|
| `extract_metrics_v2.C` | Config-driven metric extraction (supports `skip` for physqa metrics) |
| `physqa_extract.C` | Physics-level extraction: Landau fits, Fourier, MVTX chip health, TPC laser/resolution |
| `aggregate_per_run_v2.C` | Weighted per-run aggregation |
| `add_robust_z.C` | Robust outlier detection (local median + MAD) |
| `plot_dashboard.C` | Config-driven trend plots and auto-sized summary dashboard |
| `generate_report_md.C` | Generates `REPORT.md` with per-metric stats and health overview |
| `analyze_consistency_v2.C` | Physics consistency checks with threshold & marker support |
| `merge_per_run.C` | Wide-format CSV merging |
| `correlation_matrix.C` | Cross-metric Pearson correlation analysis with heatmap |
| `pca_multimetric.C` | PCA with scree plot, loadings, Mahalanobis outlier detection |
| `intt_ladder_health.C` | INTT detector health diagnostics |
| `control_charts.C` | Shewhart + CUSUM statistical control charts |
| `verdict_engine.C` | Automated physics-informed run verdicts and diagnosis |
| `fit_quality.C` | Per-histogram fit quality assessment (Landau, chi2, Fourier) |
| `make_mock_inputs.C` | Generate mock ROOT files for testing (INTT + MVTX + TPC) |

## Outputs

After a full pipeline run, `20250928/out/` contains:

- **`metrics_*.csv`** -- per-metric, per-file measurements (columns: `run, segment, file, value, error, weight`)
- **`metrics_*_perrun.csv`** -- per-run aggregates with robust z-score columns
- **`metrics_perrun_wide.csv`** -- all metrics joined into one row per run
- **`metric_*_perrun.{png,pdf}`** -- per-metric trend plots with outlier annotations
- **`dashboard_NxM.{png,pdf}`** -- auto-sized summary dashboard (grid scales with metric count)
- **`correlation_matrix.{csv,png,pdf}`** -- cross-metric correlation matrix and heatmap
- **`correlation_flags.csv`** -- strongly correlated metric pairs (\|R\| > 0.7)
- **`qa_pca_pc12.{png,pdf}`** -- PCA scatter plot (PC1 vs PC2)
- **`qa_pca_scree.{png,pdf}`** -- PCA scree plot (variance explained)
- **`qa_pca_loadings.{png,pdf}`** -- PCA loadings heatmap
- **`pca_outliers.csv`** -- Mahalanobis distance outlier detection
- **`REPORT.md`** -- per-metric statistics table and health overview
- **`VERDICT.md`** -- automated physics-informed run verdicts and diagnosis report
- **`verdicts.csv`** -- per-run, per-metric machine-readable verdicts
- **`run_verdicts.csv`** -- per-run aggregate verdict (GOOD/SUSPECT/BAD)
- **`fit_quality.csv`** -- per-histogram fit results with quality flags
- **`fit_quality_flags.csv`** -- flagged runs with poor fits
- **`consistency_summary.csv`** -- physics consistency flags
- **`_stamp.txt`** -- session metadata (date, run range, config)

## Automated verdict system

The pipeline includes a physics-informed verdict engine that automatically classifies each run:

- **GOOD** -- all metrics within expected ranges
- **SUSPECT** -- one or more metrics flagged for review
- **BAD** -- severe anomalies detected; run recommended for exclusion

For every flagged run, the engine reports:
- **Pattern**: what kind of anomaly (spike, gradual drift, step change, sustained shift)
- **Causes**: plausible physics/hardware/engineering explanations with detector-specific reasoning
- **Action**: recommended next step with subsystem-specific guidance

The reasoning is driven by a physics knowledge base (`configs/physics_rules.yaml`) that encodes domain knowledge for all three subsystems. The engine reads:
- Robust z-scores from per-run CSVs
- QC status from consistency analysis
- Control chart flags (Shewhart + CUSUM)
- INTT ladder health (dead/hot counts)
- Fit quality assessments (Landau, uniformity, Fourier)
- Cross-metric correlation flags (correlated anomalies = stronger evidence)

Results are in `out/VERDICT.md` (human-readable) and `out/verdicts.csv` (machine-readable).

## Validation

Run the smoke test to verify pipeline outputs without Python dependencies:

```bash
make smoke-test
```

This checks that all expected CSVs, columns, stamp files, verdict outputs, fit quality, correlation, PCA, and dashboard outputs exist, and reports NaN rates per metric.

## CI

GitHub Actions runs the full pipeline on mock data for every push to `main`:

```
.github/workflows/smoke.yml
```

This generates mock data, runs `make full`, and validates all outputs via the smoke test.

## Known issues

- `intt_hits_asym` produces all-NaN values across the current 13 runs (sensor occupancy histograms are empty in this data). This is a data limitation, not a code bug.
- PhysQA metrics (MVTX chip health, TPC laser timing, TPC resolution) may produce NaN if the corresponding detector histograms are not present in the input ROOT files. The pipeline handles this gracefully.

## Current data

- **Run range**: 66522 -- 68604 (13 runs)
- **Last run date**: 2025-11-18
- **Active metrics**: 27 (10 INTT core + 3 INTT physics + 6 MVTX + 8 TPC)
