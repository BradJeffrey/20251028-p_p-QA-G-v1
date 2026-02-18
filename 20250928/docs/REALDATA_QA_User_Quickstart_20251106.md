# sPHENIX Real-Data QA Pipeline -- User Quickstart

A short, task-oriented guide to running the QA pipeline and reading outputs.

---

## Prerequisites

- **ROOT >= 6.30** (C++ macros run via `root -l -b -q`)
- **GNU Make**
- **Git LFS** (input ROOT files are LFS-tracked)

Environments provided:
- Docker / Dev Container: `.devcontainer/devcontainer.json`
- Binder: `.binder/environment.yml`

---

## 1) Pull data

```bash
git lfs pull
```

The 13 input ROOT files (runs 66522--68604) will download into `20250928/data/`.

---

## 2) Run the pipeline

From the **repo root**:

```bash
# Core QA: extract -> aggregate -> robust z -> merge -> analyze -> dashboard -> report
make run-qa

# Full analysis (adds consistency checks, PCA, control charts, diagnostics):
make full
```

Or run individual stages from inside `20250928/`:

```bash
cd 20250928
make extract          # per-file metric extraction
make aggregate        # per-run aggregation (entries weighting)
make robust           # robust z-scores and outlier flags
make merge            # wide-format CSV join
make analyze          # physics consistency checks
make dashboard        # trend plots + auto-sized summary dashboard
make qa-report        # REPORT.md with per-metric statistics
```

---

## 3) Validate outputs

```bash
make smoke-test
```

This shell-based check (no Python needed) verifies that all expected CSVs, columns, and stamp files exist. It reports NaN rates per metric and flags any 100% NaN metrics.

---

## 4) Read the outputs

All outputs land in `20250928/out/`:

| File pattern | What it contains |
|---|---|
| `metrics_*.csv` | Per-file measurements: `run, segment, file, value, error, weight` |
| `metrics_*_perrun.csv` | Per-run aggregates with robust z-score columns |
| `metrics_perrun_wide.csv` | All metrics joined, one row per run |
| `metric_*_perrun.{png,pdf}` | Per-metric trend plots with outlier annotations |
| `dashboard_NxM.{png,pdf}` | Auto-sized summary dashboard (grid scales with metric count) |
| `REPORT.md` | Per-metric statistics table and health overview |
| `consistency_summary.csv` | Physics consistency flags |
| `_stamp.txt` | Session metadata (date, run range, config) |

---

## 5) Adjust configuration

### metrics.conf

Each line defines one metric:

```
metric_name, histogram_key, extraction_method
```

Methods: `maxbin`, `median`, `p90`, `mean`, `rms`, `ks_uniform_p`, `chi2_uniform_red`, `asym`.

### Makefile variables

| Variable | Default | What it controls |
|---|---|---|
| `WEIGHTING` | `entries` | Aggregation weighting: `ivar`, `entries`, or `mean` |
| `ROBUST_W` | `5` | Sliding window width for robust z-scores |
| `THRESH` | `configs/thresholds.csv` | Per-metric hard threshold bounds |
| `MARKERS` | `configs/markers.csv` | Known-event markers (beam trips, calibrations) |

Override on the command line:

```bash
make run-qa WEIGHTING=ivar ROBUST_W=7
```

---

## 6) Common tasks

### Add a new metric

1. Add a line to `metrics.conf` with the metric name, histogram key, and extraction method.
2. Run `make run-qa` -- the config-driven extractors will pick it up automatically.

### Investigate outliers

Open the relevant `metric_*_perrun.{png,pdf}` -- outlier runs are marked with color-coded points. The per-run CSV has columns `z_local`, `is_outlier_weak` (|z|>3), and `is_outlier_strong` (|z|>5).

### Check NaN rates

Run `make smoke-test` for a quick summary. Or inspect `REPORT.md` after `make qa-report`.

---

## 7) Troubleshooting

- **ROOT axis warnings (`wmin == wmax`)**: Happens when a metric is flat across all runs. Plots still generate. Check `intt_hits_asym` if sensor occupancy histograms are empty.
- **Missing per-run CSVs**: Ensure `make extract` and `make aggregate` ran first.
- **NaN values in CSVs**: Usually means the histogram was missing or empty in that ROOT file. `weight=0` indicates the measurement should be excluded from aggregation.

---

## Further reading

- **Code Documentation**: `REALDATA_QA_Code_Documentation_20251106.md` (macro internals, metric definitions, file layout)
- **Documentation Index**: `README_Documentation_Index_20251106.md` (map of all docs)
- **Config explanations**: `configs/metrics_explanations.yaml` (physics rationale for each metric)
