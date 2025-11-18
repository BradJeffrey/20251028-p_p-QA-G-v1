# ---------- User-tunable variables ----------
ROOTCMD     ?= root -l -b -q
LIST        ?= lists/files.txt
CONF        ?= metrics.conf
MARKERS     ?= config/markers.csv
THRESH      ?= config/thresholds.csv
WEIGHTING   ?= ivar                       # ivar | entries | mean
WIDE        ?= out/metrics_perrun_wide.csv
ROBUST_W    ?= 5

# core vs full bundles
CORE_STEPS  = extract physqa aggregate robust merge analyze stamp
FULL_STEPS  = $(CORE_STEPS) derived segmentcv intthealth control pca report

DATE_TAG    := $(shell date +"%Y%m%d_%H%M%S")

.PHONY: all core full extract physqa aggregate robust merge analyze derived segmentcv intthealth control pca report stamp check list_runs clean clobber

all: full
core: $(CORE_STEPS)
full: $(FULL_STEPS)

# ---------- Steps ----------
extract:
	@mkdir -p out
	$(ROOTCMD) 'macros/extract_metrics_v2.C("$(LIST)","$(CONF)")'

physqa:
	@mkdir -p out
	$(ROOTCMD) 'macros/physqa_extract.C("$(LIST)")'

aggregate:
	@mkdir -p out
	$(ROOTCMD) 'macros/aggregate_per_run_v2.C("$(CONF)","$(WEIGHTING)")'

robust:
	@echo "[Makefile] Running robust z (W=$(ROBUST_W))..."
	$(ROOTCMD) 'macros/add_robust_z.C("$(CONF)",$(ROBUST_W))'

merge:
	@mkdir -p out
	$(ROOTCMD) 'macros/merge_per_run.C("$(CONF)","$(WIDE)")'

analyze:
	@mkdir -p out
	@if [ -f macros/analyze_consistency_v2.C ]; then \
	  $(ROOTCMD) 'macros/analyze_consistency_v2.C("$(CONF)","$(MARKERS)","$(THRESH)")'; \
	else \
	  echo "[INFO] macros/analyze_consistency_v2.C not found; skipping deep analysis"; \
	fi

derived:
	@mkdir -p out
	-$(ROOTCMD) 'macros/derive_metric_pair.C("intt_bco_full_peak","mvtx_bco_peak","diff","delta_bco_full")'
	-$(ROOTCMD) 'macros/derive_metric_pair.C("intt_nhit_mean","mvtx_nhits_l0_mean","ratio","ratio_nhits_intt_to_mvtx")'

segmentcv:
	@mkdir -p out
	-$(ROOTCMD) 'macros/segment_consistency.C("cluster_size_intt_mean")'
	-$(ROOTCMD) 'macros/segment_consistency.C("intt_adc_peak")'

intthealth:
	@mkdir -p out
	-$(ROOTCMD) 'macros/intt_ladder_health.C("$(LIST)",0.05,5.0)'

control:
	@mkdir -p out
	-$(ROOTCMD) 'macros/control_charts.C("intt_adc_landau_mpv",3.0,0.5,5.0)'
	-$(ROOTCMD) 'macros/control_charts.C("tpc_sector_adc_uniform_chi2",3.0,0.5,5.0)'

pca:
	@mkdir -p out
	-$(ROOTCMD) 'macros/pca_multimetric.C("$(WIDE)")'

report:
	@mkdir -p out
	@if [ -f macros/make_report.C ]; then \
	  $(ROOTCMD) 'macros/make_report.C("out/_stamp.txt")'; \
	else \
	  echo "[INFO] macros/make_report.C not found; skipping PDF report"; \
	fi

# ---------- Session stamp (date + run range) ----------
stamp:
	@mkdir -p out
	@RUNS=`sed -n 's/.*run\([0-9][0-9]*\).*/\1/p' $(LIST) | sort -n | uniq`; \
	MIN=$$(echo $$RUNS | awk '{print $$1}'); \
	MAX=$$(echo $$RUNS | awk '{print $$NF}'); \
	echo "date=$(DATE_TAG)"        >  out/_stamp.txt; \
	echo "run_min=$${MIN:-NA}"     >> out/_stamp.txt; \
	echo "run_max=$${MAX:-NA}"     >> out/_stamp.txt; \
	echo "list=$(LIST)"            >> out/_stamp.txt; \
	echo "conf=$(CONF)"            >> out/_stamp.txt; \
	echo "weighting=$(WEIGHTING)"  >> out/_stamp.txt; \
	echo "[STAMP] $$(cat out/_stamp.txt)"

# ---------- Helpers ----------
check:
	@echo "[CHECK] metrics files:"; ls -1 out/metrics_*.csv 2>/dev/null | wc -l || true
	@echo "[CHECK] per-run series:"; ls -1 out/metrics_*_perrun.csv 2>/dev/null | wc -l || true
	@echo "[CHECK] annotated plots:"; ls -1 out/*_perrun_annot.png 2>/dev/null | wc -l || true
	@echo "[CHECK] stamp:"; test -f out/_stamp.txt && cat out/_stamp.txt || echo "no stamp"

list_runs:
	@sed -n 's/.*run\([0-9][0-9]*\).*/\1/p' $(LIST) | sort -n | uniq

clean:
	@rm -f out/*.png out/*.pdf out/metrics_*_perrun.csv out/qa_pca_*.png out/qc_control_*.csv

clobber: clean
	@rm -f out/metrics_*.csv out/_stamp.txt out/metrics_perrun_wide.csv out/consistency_summary.csv
# ---------- QA extensions ----------
ROBUST_W ?= 5

robust:
	@echo "[Makefile] Running robust z (W=$(ROBUST_W))..."
	root -l -b -q 'macros/add_robust_z.C("metrics.conf",$(ROBUST_W))'

robust-aliases:
	@echo "[Makefile] Copy metrics_* -> metric_* per-run CSV aliases"
	@for f in out/metrics_*_perrun.csv; do \
	  dest=$${f/out\/metrics_/out\/metric_}; \
	  cp -f "$$f" "$$dest"; \
	done

run-qa:
	@echo "[Makefile] extract → aggregate → robust → alias → dashboard"
	root -l -b -q 'macros/extract_quick.C("lists/files.txt")'
	root -l -b -q 'macros/aggregate_per_run_v2.C("metrics.conf","entries")'
	$(MAKE) robust ROBUST_W=$(ROBUST_W)
	$(MAKE) robust-aliases
	root -l -b -q 'macros/plot_dashboard.C()'

check-robust:
	./scripts/validate_perrun_schema.sh

z-summary:
	./scripts/summary_outliers.sh
