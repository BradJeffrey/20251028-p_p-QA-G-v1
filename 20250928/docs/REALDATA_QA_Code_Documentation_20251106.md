# sPHENIX Real-Data QA -- Code Documentation

Comprehensive code-level reference for the QA pipeline: macro behavior, metric definitions, file layout, Makefile targets, and troubleshooting.

---

## 1) Architecture overview

The pipeline is a sequence of ROOT C++ macros orchestrated by a Makefile. Each stage reads CSV or ROOT files, processes them, and writes CSV/plot/report outputs to `out/`.

```
ROOT files -> extract -> per-file CSVs
                          -> aggregate -> per-run CSVs
                                           -> robust z -> outlier-flagged CSVs
                                                           -> merge -> wide CSV
                                                           -> analyze -> consistency checks
                                                           -> dashboard -> plots
                                                           -> qa-report -> REPORT.md
```

All macros are invoked via `root -l -b -q` (stored in Makefile variable `$(ROOTCMD)`). Configuration is centralized in `metrics.conf`, with overridable Makefile variables.

---

## 2) Metric definitions

Metrics are defined in `metrics.conf`, one per line:

```
metric_name, histogram_key, extraction_method
```

### Current metrics (10)

| Metric | Histogram | Method | What it measures |
|---|---|---|---|
| `intt_adc_peak` | `h_InttRawHitQA_adc` | `maxbin` | ADC peak position (gain tracking) |
| `intt_adc_median_p50` | `h_InttRawHitQA_adc` | `median` | ADC median (typical amplitude) |
| `intt_adc_p90` | `h_InttRawHitQA_adc` | `p90` | ADC 90th percentile (noise tail) |
| `intt_phi_uniform_r1` | `h_InttClusterQA_clusterPhi_incl` | `ks_uniform_p` | Azimuthal uniformity (KS p-value) |
| `intt_phi_chi2_reduced` | `h_InttClusterQA_clusterPhi_incl` | `chi2_uniform_red` | Azimuthal uniformity (chi2/NDF) |
| `intt_bco_peak` | `h_InttRawHitQA_bco` | `maxbin` | Beam-clock offset peak |
| `cluster_size_intt_mean` | `h_InttClusterQA_clusterSize` | `mean` | Mean cluster size (strips) |
| `cluster_phi_intt_rms` | `h_InttClusterQA_clusterPhi_incl` | `rms` | Cluster phi spread |
| `intt_hits_asym` | `h_InttRawHitQA_sensorOccupancy` | `asym` | Hit asymmetry (max-min)/(max+min) |
| `intt_phi_chi2_reduced` | `h_InttClusterQA_clusterPhi_l34` | `chi2_uniform_red` | Layer 3-4 azimuthal uniformity |

### Extraction methods

| Method | Implementation | Notes |
|---|---|---|
| `maxbin` | `h->GetMaximumBin()` x-center | Peak location |
| `median` | Quantile at 0.5 via `GetQuantiles` | Robust central value |
| `p90` | Quantile at 0.9 | Upper tail probe |
| `mean` | `h->GetMean()` | Arithmetic mean |
| `rms` | `h->GetRMS()` | Standard deviation |
| `ks_uniform_p` | Kolmogorov-Smirnov p-value vs uniform | Uniformity test |
| `chi2_uniform_red` | Chi2/NDF vs flat reference | Bin-level uniformity |
| `asym` | `(max-min)/(max+min)` over bin contents | Balance/imbalance |

All methods return `NaN` for empty or zero-entry histograms.

---

## 3) Macro reference

### Core pipeline macros

#### `extract_quick.C`
- **Signature**: `void extract_quick(const char* listfile = "lists/files.txt")`
- **Purpose**: Primary metric extraction from ROOT files. Hardcoded metric logic.
- **Output**: `out/metrics_{name}.csv` (per-file: `run, segment, file, value, error, weight`)
- **Notes**: Handles missing histograms gracefully (writes NaN, weight=0).

#### `extract_metrics_v2.C`
- **Signature**: `void extract_metrics_v2(const char* listfile, const char* conffile)`
- **Purpose**: Config-driven extraction. Reads metric definitions from `metrics.conf` and dispatches to the appropriate method.
- **Output**: Same format as `extract_quick.C`
- **Methods supported**: All 8 methods listed above (full parity with extract_quick).

#### `aggregate_per_run_v2.C`
- **Signature**: `void aggregate_per_run_v2(const char* conf, const char* weighting = "entries")`
- **Purpose**: Pools per-file CSVs into per-run summaries with configurable weighting.
- **Weighting schemes**: `ivar` (inverse variance), `entries` (count-weighted), `mean` (simple average)
- **Output**: `out/metrics_{name}_perrun.csv` and `out/metric_{name}_perrun.{png,pdf}`

#### `add_robust_z.C`
- **Signature**: `void add_robust_z(const char* conf, int W = 5)`
- **Purpose**: Appends robust outlier columns to per-run CSVs using a sliding window of width `W`.
- **Algorithm**: Local median + MAD (median absolute deviation). Computes z-score: `(value - median) / (1.4826 * MAD)`.
- **Columns added**: `neighbors_median, neighbors_mad, z_local, is_outlier_weak, is_outlier_strong`
- **Thresholds**: Weak = |z| > 3, Strong = |z| > 5.

#### `merge_per_run.C`
- **Signature**: `void merge_per_run(const char* conf, const char* outfile = "out/metrics_perrun_wide.csv")`
- **Purpose**: Joins all per-run CSVs into a single wide-format CSV (one row per run, one column per metric value).
- **Output**: `out/metrics_perrun_wide.csv`

#### `analyze_consistency_v2.C`
- **Signature**: `void analyze_consistency_v2(const char* conf, const char* markers_csv, const char* thresh_csv)`
- **Purpose**: Physics consistency checks across metrics. Supports configurable thresholds and event markers.
- **Inputs**: `configs/thresholds.csv` (hard bounds), `configs/markers.csv` (known events)
- **Output**: `out/consistency_summary.csv`

#### `plot_dashboard.C`
- **Signature**: `void plot_dashboard(const char* conf = "metrics.conf")`
- **Purpose**: Config-driven summary dashboard. Reads all metrics from conf, deduplicates, auto-sizes grid.
- **Grid logic**: `ceil(sqrt(N))` columns, enough rows to fit N metrics.
- **Output**: `out/dashboard_{ncols}x{nrows}.{png,pdf}` (e.g., `dashboard_3x3`)

#### `generate_report_md.C`
- **Signature**: `void generate_report_md(const char* conf = "metrics.conf")`
- **Purpose**: Generates a markdown report with per-metric statistics and health overview.
- **Stats computed**: Total runs, finite count, NaN count, NaN%, mean, std, min, max, weak outliers, strong outliers.
- **Output**: `out/REPORT.md`

### Advanced analysis macros

| Macro | Purpose |
|---|---|
| `physqa_extract.C` | Physics-level QA extraction (Landau fits, Fourier analysis) |
| `derive_metric_pair.C` | Derived metrics from pairs (diff, ratio) |
| `segment_consistency.C` | Per-segment coefficient-of-variation analysis |
| `intt_ladder_health.C` | INTT ladder-level health diagnostics |
| `control_charts.C` | Shewhart/CUSUM statistical process control charts |
| `pca_multimetric.C` | SVD-based PCA across metrics |
| `correlate_metrics.C` | Metric pair correlation analysis |
| `flag_outliers.C` | Outlier flagging utility |

### Utility macros

| Macro | Purpose |
|---|---|
| `dump_keys.C` | List all histogram keys in a ROOT file |
| `diagnose_runs.C` | Per-run diagnostic report |
| `build_summary_docs.C` | Generate per-run summary markdown |
| `build_summary_root.C` | Generate summary ROOT file |
| `generate_metrics_doc.C` | Auto-generate metrics documentation |
| `make_report.C` | Consolidated PDF report from stamp + outputs |
| `make_mock_inputs.C` | Generate mock ROOT files for testing |
| `run_all.C` | Legacy all-in-one runner |

---

## 4) Makefile targets

### Pipeline targets

| Target | Dependencies | Description |
|---|---|---|
| `all` | `full` | Default: runs the full pipeline |
| `core` | `CORE_STEPS` | `extract physqa aggregate robust merge analyze stamp` |
| `full` | `FULL_STEPS` | Core + `derived segmentcv intthealth control pca dashboard qa-report report` |
| `extract` | -- | Config-driven extraction via `extract_metrics_v2.C` |
| `physqa` | -- | Physics QA extraction (Landau, Fourier) |
| `aggregate` | -- | Per-run aggregation |
| `robust` | -- | Robust z-score computation |
| `merge` | -- | Wide-format CSV join |
| `analyze` | -- | Consistency checks with thresholds and markers |
| `derived` | -- | Derived metric pairs (graceful failure with `-` prefix) |
| `segmentcv` | -- | Segment-level consistency |
| `intthealth` | -- | INTT ladder health |
| `control` | -- | Control charts |
| `pca` | -- | PCA analysis |
| `dashboard` | -- | Config-driven summary dashboard |
| `qa-report` | -- | REPORT.md generation |
| `report` | -- | PDF report |
| `stamp` | -- | Session metadata stamp |

### Convenience targets

| Target | Description |
|---|---|
| `run-qa` | Quick pipeline: `extract_quick -> aggregate -> robust -> aliases -> dashboard` |
| `smoke-test` | Shell-based output validation (no Python) |
| `check` | Count output files |
| `list_runs` | Print sorted unique run numbers |
| `clean` | Remove plots and per-run CSVs |
| `clobber` | Remove all generated outputs |
| `diagnose` | Run physics diagnostics |
| `summary-docs` | Build per-run summary markdown |
| `metrics-doc` | Auto-generate metrics documentation |
| `full-diagnose` | `diagnose + summary-docs + metrics-doc` |

### Variables

| Variable | Default | Description |
|---|---|---|
| `ROOTCMD` | `root -l -b -q` | ROOT command prefix |
| `LIST` | `lists/files.txt` | Input file list |
| `CONF` | `metrics.conf` | Metric definitions |
| `MARKERS` | `configs/markers.csv` | Known-event markers |
| `THRESH` | `configs/thresholds.csv` | Per-metric hard bounds |
| `WEIGHTING` | `ivar` | Aggregation method |
| `WIDE` | `out/metrics_perrun_wide.csv` | Wide CSV output path |
| `ROBUST_W` | `5` | Sliding window width |

---

## 5) File layout

```
20250928/
  Makefile                  Primary build orchestration
  metrics.conf              Metric definitions (name, histogram, method)
  lists/
    files.txt               Input ROOT file list (one path per line)
  data/
    *.root                  Input ROOT histogram files (Git LFS)
  macros/
    extract_quick.C         Hardcoded metric extraction
    extract_metrics_v2.C    Config-driven metric extraction
    aggregate_per_run_v2.C  Per-run aggregation with weighting
    add_robust_z.C          Robust z-score outlier detection
    merge_per_run.C         Wide-format CSV merge
    analyze_consistency_v2.C  Consistency checks
    plot_dashboard.C        Auto-sized summary dashboard
    generate_report_md.C    REPORT.md generation
    physqa_extract.C        Physics QA (Landau, Fourier)
    ...                     (see macro reference above)
  configs/
    metrics_explanations.yaml  Physics rationale for each metric
    thresholds.csv            Hard bounds (metric, lo, hi)
    markers.csv               Known-event markers (type, label, start, end)
    cluster_map.yaml          Cluster mapping config
    severity_thresholds.yaml  Severity level definitions
  scripts/
    smoke_test.sh           Pipeline output validation
    validate_perrun_schema.sh  Per-run CSV schema check
    summary_outliers.sh     Outlier summary script
  out/
    metrics_*.csv           Per-file measurements
    metrics_*_perrun.csv    Per-run aggregates with z-scores
    metrics_perrun_wide.csv All metrics wide-format
    metric_*_perrun.{png,pdf}  Per-metric trend plots
    dashboard_NxM.{png,pdf} Summary dashboard
    REPORT.md               Per-metric statistics report
    consistency_summary.csv Consistency flags
    _stamp.txt              Session metadata
  docs/
    README_Documentation_Index_20251106.md  Document map
    REALDATA_QA_User_Quickstart_20251106.md  User guide
    REALDATA_QA_Code_Documentation_20251106.md  This file
    archive/                Archived ChatGPT handoff documents
  diagnostics/
    *.txt, *.csv, *.png     Diagnostic output bundles
```

---

## 6) CSV formats

### Per-file (`metrics_{name}.csv`)

```
run,segment,file,value,error,weight
66522,0,data/DST_INTT_run...,128.5,0.0,45231
```

- `weight = 0` indicates the histogram was missing (value will be NaN).

### Per-run (`metrics_{name}_perrun.csv`)

After `aggregate` + `robust`:

```
run,value,stat_err,entries,neighbors_median,neighbors_mad,z_local,is_outlier_weak,is_outlier_strong
66522,128.3,0.21,3,128.5,0.15,1.33,0,0
```

- `is_outlier_weak = 1` when |z_local| > 3
- `is_outlier_strong = 1` when |z_local| > 5

### Wide format (`metrics_perrun_wide.csv`)

```
run,intt_adc_peak,intt_adc_median_p50,...
66522,128.3,127.0,...
```

One row per run, one column per metric value.

### Thresholds (`configs/thresholds.csv`)

```
metric,lo,hi
intt_phi_chi2_reduced,,5.0
intt_hits_asym,,1.0
```

Empty `lo` or `hi` means unbounded in that direction.

### Markers (`configs/markers.csv`)

```
type,label,start,end
```

Empty file is valid (no known events to mark).

---

## 7) Adding a new metric

1. Identify the histogram key in the ROOT files (use `macros/dump_keys.C` to list keys).
2. Choose an extraction method from the supported list.
3. Add a line to `metrics.conf`:
   ```
   my_new_metric, h_HistogramKey, method
   ```
4. Run `make extract` (or `make run-qa` for the full quick pipeline).
5. Optionally add an entry to `configs/metrics_explanations.yaml` with `formula`, `patterns`, `physics`, and `why` fields.
6. Optionally add threshold bounds in `configs/thresholds.csv`.

---

## 8) Troubleshooting

### ROOT axis warnings (`wmin == wmax`)
Occurs when a metric has identical values across all runs (or is all-NaN). Plots still generate. Common with `intt_hits_asym` when sensor occupancy histograms are empty.

### NaN values in CSVs
Usually means the histogram was missing or empty in that ROOT file. Check:
- Does the histogram key in `metrics.conf` match what's in the ROOT file? Use `dump_keys.C` to verify.
- Is `weight = 0`? That confirms a missing histogram, not a code bug.

### Missing per-run CSVs
Ensure `make extract` and `make aggregate` ran before downstream stages.

### `derived` / `control` targets fail
These reference physqa metrics (e.g., `intt_adc_landau_mpv`, `tpc_sector_adc_uniform_chi2`) not in `metrics.conf`. They fail gracefully (Make `-` prefix). They will work once multi-subsystem data and corresponding metrics.conf entries are available.

### Smoke test failures
Run `make smoke-test` and check which assertions fail. Common causes:
- Pipeline stages not run yet (missing CSVs).
- `_stamp.txt` missing (run `make stamp`).
- 100% NaN rate on a metric (warning, not failure -- usually a data issue).
