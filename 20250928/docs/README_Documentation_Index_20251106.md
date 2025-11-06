# sPHENIX Real‑Data QA — Documentation & Handoff Guide (20251106)

This file explains **what each document is for** and **how to use them together** during a normal QA run and at the **end of a ChatGPT session**.

> **Folder:** `20250928/docs/`  
> **Branch:** `results/intt-metrics`

---

## 1) What’s in this folder (document map)

| File | Role | Primary audience | Use it when | Notes |
|---|---|---|---|---|
| **REALDATA_QA_User_Quickstart_20251106.md** | Short, task‑oriented guide to run the pipeline and read outputs. | Analysts & shifters | You just want the commands to run the QA and see the plots/CSVs. | Minimal theory; points to full docs when needed. |
| **REALDATA_QA_Code_Documentation_20251106.md** | Full **code‑level documentation**: macro behavior, metrics definitions, file layout, Makefile targets, and troubleshooting. | Developers & maintainers | You need to understand or change the code, add metrics, or debug. | Includes inline code snippets and advanced sections. |
| **handoff_for_new_agent_UPDATED.txt** | **State‑of‑project overview** to bootstrap the next ChatGPT Agent session. | Anyone ending a session | You’ve finished a session and want the **next session** to pick up seamlessly. | Replace prior handoff file each session. |
| **user_prelaunch_instructions_UPDATED.txt** | Checklist for **local environment and repo prep** before running Agent Mode again. | The repo owner / local runner | Before starting a new Agent session or re‑running the pipeline locally. | Keep this current with any environment changes. |
| **meta_session_handoff_prompt_UPDATED.txt** | The **prompt to paste** into ChatGPT at end‑of‑session to generate the new handoff + prelaunch updates (and keep docs fresh). | Anyone ending a session | At the **very end** of a session. | Paste this prompt and attach the “UPDATED” files + the session summary. |
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
   root -l -b -q 'macros/extract_quick.C("lists/files.txt")'
   root -l -b -q 'macros/aggregate_per_run_v2.C("metrics.conf","entries")'
   root -l -b -q 'macros/plot_dashboard.C()'
   ```
3. **Inspect outputs** in `20250928/out/`:
   - `metrics_*.csv`, `*_perrun.csv`
   - `metric_*_perrun.{png,pdf}`
   - `dashboard_intt_2x2.{png,pdf}`
4. If something looks off (e.g., NaNs or flat axes), check **metric definitions and extraction code** in `REALDATA_QA_Code_Documentation_20251106.md` and the macros it references (e.g., `extract_quick.C`, `aggregate_per_run_v2.C`, `plot_dashboard.C`).

### B. End‑of‑session handoff (what to upload to ChatGPT)
1. Prepare the following files from this folder:
   - `handoff_for_new_agent_UPDATED.txt`
   - `user_prelaunch_instructions_UPDATED.txt`
   - `REALDATA_QA_Code_Documentation_20251106.md` (acts as the “project documentation” attachment)
   - The current session’s summary (e.g., `session_update_summary_YYYYMMDD.txt`), if you keep one
2. Open ChatGPT and **paste the entire contents** of `meta_session_handoff_prompt_UPDATED.txt` into a new message.
3. Attach the files listed above. The assistant will generate fresh **UPDATED** handoff and prelaunch files and, if needed, refresh the docs.  
4. Download the new outputs and commit them back to `20250928/docs/` (commands below).

### C. Generate/refresh changelog and push
Use the Makefile automation you pinned this session:
```bash
# From repo root
make generate-changelog      # writes docs/CHANGELOG_REALDATA_<timestamp>.md
make nan-check               # appends NaN summary + affected files/runs to the changelog
make update-latest           # copies the latest timestamped changelog to CHANGELOG_LATEST.md
make full-push               # run-qa + changelog + nan-check + update-latest + push
```

> If you prefer explicit Git commands for the docs themselves:
```bash
cd ~/20251028-p_p-QA-G-v1
git add 20250928/docs/REALDATA_QA_Code_Documentation_20251106.md         20250928/docs/REALDATA_QA_User_Quickstart_20251106.md         20250928/docs/README_Documentation_Index_20251106.md
git commit -m "Docs: add/refresh code docs, quickstart, and index (20251106)"
git push origin results/intt-metrics
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
- **Ending a ChatGPT session?** → Use **Meta Session Handoff Prompt**, attach **handoff + prelaunch (UPDATED)** and **Code Documentation**.  
- **Want the latest run notes fast?** → Open **CHANGELOG_LATEST.md**.  
- **Need to restore working automation?** → Copy **Makefile_PRODUCTION_20251106** back to repo root.

---

## 5) Troubleshooting tips

- If the dashboard warns about missing per‑run CSVs or axes with zero range, regenerate per‑run metrics using a safe weighting method (`entries` or `mean`) in `aggregate_per_run_v2.C`.  
- If NaNs appear in CSVs, `make nan-check` will annotate the changelog with the **total count**, the **affected files**, and the **(if detectable) run numbers** to speed up triage.  
- When changing metrics, keep `metrics.conf` and the extraction logic in sync; the Code Documentation lists the histogram ↔ metric mappings and expected methods.

---

**That’s it.** Keep this file alongside the other docs so teammates can quickly see **what each file is** and **when to use it**.
