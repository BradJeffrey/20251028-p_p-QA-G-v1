# sPHENIX Real‑Data QA — Documentation & Handoff Guide (20251106)

This file explains **what each document is for** and **how to use them together** during a normal QA run.

> **Folder:** `20250928/docs/`

---

## 1) What’s in this folder (document map)

| File | Role | Primary audience | Use it when | Notes |
|---|---|---|---|---|
| **REALDATA_QA_User_Quickstart_20251106.md** | Short, task‑oriented guide to run the pipeline and read outputs. | Analysts & shifters | You just want the commands to run the QA and see the plots/CSVs. | Minimal theory; points to full docs when needed. |
| **REALDATA_QA_Code_Documentation_20251106.md** | Full **code‑level documentation**: macro behavior, metrics definitions, file layout, Makefile targets, and troubleshooting. | Developers & maintainers | You need to understand or change the code, add metrics, or debug. | Includes inline code snippets and advanced sections. |
| **CHANGELOG_LATEST.md** (symlink/copy to latest) | Human‑readable log of what changed in the most recent run/session. | Everyone | To see what happened most recently without hunting timestamps. | Updated by Makefile targets. |
| **CHANGELOG_REALDATA_YYYYMMDDHHMM.md** | Time‑stamped, immutable change log snapshots. | Everyone | For audit/history across sessions. | Created by Makefile `generate-changelog`. |
| **Makefile_PRODUCTION_20251106** | Pinned, known‑good Makefile with QA automation, NaN detection, and run‑level diagnostics. | Developers & automation | To reproduce this session’s automation or restore after edits. | Copied from repo root at pin time. |

---

## 2) Typical workflow

### A. During a QA run
1. **Set up environment** per `user_prelaunch_instructions_UPDATED.txt` (ROOT version, repo path, branch, data files).  
2. From repo root, run the pipeline (either via Makefile or manually):
   ```bash
   # Makefile (recommended)
   make run-qa
   # or manual:
   cd 20250928
   root -l -b -q 'macros/extract_metrics_v2.C("lists/files.txt","metrics.conf")'
   root -l -b -q 'macros/aggregate_per_run_v2.C("metrics.conf","entries")'
   root -l -b -q 'macros/plot_dashboard.C("metrics.conf")'
   ```
3. **Inspect outputs** in `20250928/out/`:
   - `metrics_*.csv`, `*_perrun.csv`
   - `metric_*_perrun.{png,pdf}`
   - `dashboard_NxM.{png,pdf}` (auto-sized grid)
4. If something looks off (e.g., NaNs or flat axes), check **metric definitions and extraction code** in `REALDATA_QA_Code_Documentation_20251106.md` and the macros it references (e.g., `extract_quick.C`, `aggregate_per_run_v2.C`, `plot_dashboard.C`).

### B. Generate/refresh changelog
Use the Makefile automation:
```bash
# From repo root
make generate-changelog      # writes docs/CHANGELOG_REALDATA_<timestamp>.md
make nan-check               # appends NaN summary + affected files/runs to the changelog
make full                    # run-qa + changelog + nan-check
```

---

## 3) Naming conventions & where things live
- **Docs live in:** `20250928/docs/`  
- **Outputs live in:** `20250928/out/`  
- **Time‑stamped files:** use `YYYYMMDDHHMM` to match the repo convention (e.g., `CHANGELOG_REALDATA_202511060436.md`).  
- The **“UPDATED”** suffix indicates “latest output of the most recent session.” These are the best inputs for the **next** session’s handoff.  
- The **Makefile** in repo root provides targets used by automation. A **pinned copy** (`Makefile_PRODUCTION_20251106`) is kept here for reproducibility.

---

## 4) Quick decision guide

- **Running the pipeline quickly?** → Start with **Quickstart**.
- **Debugging or adding metrics/macros?** → Read **Code Documentation**.
- **Want the latest run notes fast?** → Open **CHANGELOG_LATEST.md**.
- **Need to restore working automation?** → Copy **Makefile_PRODUCTION_20251106** back to repo root.

---

## 5) Troubleshooting tips

- If the dashboard warns about missing per‑run CSVs or axes with zero range, regenerate per‑run metrics using a safe weighting method (`entries` or `mean`) in `aggregate_per_run_v2.C`.  
- If NaNs appear in CSVs, `make nan-check` will annotate the changelog with the **total count**, the **affected files**, and the **(if detectable) run numbers** to speed up triage.  
- When changing metrics, keep `metrics.conf` and the extraction logic in sync; the Code Documentation lists the histogram ↔ metric mappings and expected methods.

---

**That’s it.** Keep this file alongside the other docs so teammates can quickly see **what each file is** and **when to use it**.
