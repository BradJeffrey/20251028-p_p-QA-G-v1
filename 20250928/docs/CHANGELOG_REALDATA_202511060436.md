# 🧠 sPHENIX QA Pipeline — Change Log (Real Data Edition)
**Session Timestamp:** 2025-11-06 04:36 (UTC-5)  
**Repository:** `BradJeffrey/20251028-p_p-QA-G-v1`  
**Branch:** `results/intt-metrics`  
**Working Directory:** `20250928/`

---

## 📅 Session Summary

This session extended and validated the real-data QA pipeline in ROOT for the sPHENIX INTT detector.  
The conversation covered macro updates, metrics additions, aggregation fixes, and verification of outputs.

### 🧾 Summary of Key Updates

| Area | Description | Status |
|------|--------------|--------|
| **Macros** | Updated `extract_quick.C` to include new metrics: `cluster_size_intt_mean`, `cluster_phi_intt_rms`, and `intt_hits_asym`. | ✅ Done |
| **Configuration** | Added new entries to `metrics.conf` and confirmed deduplication. | ✅ Done |
| **Aggregation** | Switched `aggregate_per_run_v2.C` to use `entries` weighting (avoiding IVAR NaNs). | ✅ Done |
| **Outputs** | All metrics produce per-file and per-run CSVs + plots. Dashboard regenerated. | ✅ Done |
| **Verification** | All outputs confirmed populated except `intt_hits_asym` (values remain NaN). | ⚠️ Partial |
| **Commit & Push** | Pushed commit `4fe3aba` to `results/intt-metrics`. | ✅ Done |

---

## 🧩 Macro and Code Changes

### `extract_quick.C` (Updated)

**Added functionality:**
- Reads new histograms:  
  - `h_InttClusterQA_clusterSize`  
  - `h_InttClusterQA_clusterPhi_incl`  
  - `h_InttRawHitQA_sensorOccupancy`
- Computes:  
  - Mean cluster size (`cluster_size_intt_mean`)  
  - RMS of inclusive phi (`cluster_phi_intt_rms`)  
  - Hit asymmetry (`intt_hits_asym`) = (max - min) / (max + min)
- Writes NaN rows with `weight=0` if missing histograms.

**Hit Asymmetry Code Snippet:**
```cpp
if (hocc && hocc->GetEntries() > 0) {
    double max_val = 0, min_val = 0; bool first_bin = true;
    for (int bi = 1; bi <= hocc->GetNbinsX(); ++bi) {
        double content = hocc->GetBinContent(bi);
        if (first_bin) { max_val = min_val = content; first_bin = false; }
        else {
            if (content > max_val) max_val = content;
            if (content < min_val) min_val = content;
        }
    }
    double asym = TMath::QuietNaN();
    if (max_val + min_val > 0) asym = (max_val - min_val) / (max_val + min_val);
    csv_hits_asym << run << "," << seg << "," << fpath << "," << asym << ",0," << hocc->GetEntries() << "\n";
} else {
    csv_hits_asym << run << "," << seg << "," << fpath << ",nan,0,0\n";
}
```

---

## ⚙️ Configuration and Data Changes

### `metrics.conf` (Updated)
Appended new metric definitions:
```text
cluster_size_intt_mean, h_InttClusterQA_clusterSize, mean
cluster_phi_intt_rms,  h_InttClusterQA_clusterPhi_incl, rms
intt_hits_asym,        h_InttRawHitQA_sensorOccupancy, asym
```
Confirmed no duplicates after edit.

### `lists/files.txt`
Lists all real data runs (run66522–run68604).  
Used as input for macro processing.

---

## 📈 Pipeline Execution Summary

Commands executed successfully:

```bash
root -l -b -q 'macros/extract_quick.C("lists/files.txt")'
root -l -b -q 'macros/aggregate_per_run_v2.C("metrics.conf","entries")'
root -l -b -q 'macros/plot_dashboard.C()'
```

### Outputs Generated

| Output Type | Description | Files Created |
|--------------|-------------|----------------|
| **CSV (Per-file)** | Metric extraction results for each run | `metrics_*.csv` |
| **CSV (Per-run)** | Aggregated values with entries weighting | `metrics_*_perrun.csv` |
| **Plots** | Histograms per run and per metric | `metric_*_perrun.{pdf,png}` |
| **Dashboard** | Combined 2×2 INTT dashboard | `dashboard_intt_2x2.{pdf,png}` |

**Newly added plots confirmed:**
- `metric_cluster_size_intt_mean_perrun.pdf/png`
- `metric_cluster_phi_intt_rms_perrun.pdf/png`
- `metric_intt_hits_asym_perrun.pdf/png`

---

## 🧠 Verification Results

All metrics except **`intt_hits_asym`** are populated with numeric values.  
The asymmetry metric currently yields `nan` for every run — requires follow-up investigation (check occupancy histograms).

| Metric | Status | Notes |
|---------|---------|-------|
| `intt_adc_peak` | ✅ | Values ~7.5 and 0.5 confirmed |
| `intt_adc_p90` | ✅ | Finite values across runs |
| `intt_adc_median_p50` | ✅ | Median values ~3.0–3.4 |
| `intt_bco_peak` | ✅ | Peaks 61.5–122.5 verified |
| `intt_phi_chi2_reduced` | ✅ | Large values, finite |
| `intt_phi_uniform_r1` | ✅ | P-values 0.01–0.09 |
| `cluster_size_intt_mean` | ✅ | 1.70–1.81 confirmed |
| `cluster_phi_intt_rms` | ✅ | 1.58–1.92 confirmed |
| `intt_hits_asym` | ⚠️ | All NaN; needs logic review |

---

## 🧭 Repository Actions

### Commit Summary
```bash
git add 20250928/macros/extract_quick.C         20250928/metrics.conf         20250928/out/*
git commit -m "Rewrite extract_quick.C to add cluster size mean, phi RMS, and hit asymmetry; regenerate outputs"
git push origin results/intt-metrics
```
**Commit:** `4fe3aba`  
**Branch:** `results/intt-metrics`  
**Status:** Synced and verified on GitHub

---

## 🚀 Next Steps / Recommendations

1. **Fix `intt_hits_asym` logic** – ensure occupancy histograms have nonzero bin contents; possibly filter empty bins.  
2. **Bring `extract_metrics_v2.C` to parity** – replicate new metrics and weighting semantics.  
3. **Add axis guards** – in `aggregate_per_run_v2.C` to avoid `wmin==wmax` warnings.  
4. **Implement `qa-smoke` CI workflow** – automate extractor→aggregator→dashboard in ROOT container.  
5. **Generate `out/REPORT.md`** – summarize metrics coverage (finite counts, NaN %, mean±std).

---

## 🧾 Session Metadata

| Item | Value |
|------|--------|
| **Session ID** | `REALDATA_20251106` |
| **Time (UTC‑5)** | Nov 6, 2025 — 04:36 AM |
| **Assistant Commit ID** | `4fe3aba` |
| **Working Branch** | `results/intt-metrics` |
| **Location** | `20250928/` |

---

_This changelog was automatically generated at the end of the session and can be placed at:_  
`20251028-p_p-QA-G-v1/20250928/docs/CHANGELOG_REALDATA_202511060436.md`
