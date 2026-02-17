# ============================================================
#  sPHENIX Real Data QA Pipeline — Makefile
# ============================================================

TIMESTAMP := $(shell date +"%Y%m%d%H%M")
OUTDIR := 20250928/out
DOCSDIR := 20250928/docs

.PHONY: run-qa generate-changelog nan-check full

# ------------------------------------------------------------
# Run the full QA analysis chain
# ------------------------------------------------------------
run-qa:
	$(MAKE) -C 20250928 run-qa

# ------------------------------------------------------------
# Generate a timestamped changelog
# ------------------------------------------------------------
generate-changelog:
	cd $(DOCSDIR) && \
	echo "# 🧠 sPHENIX QA Pipeline — Auto Changelog (Real Data)" > CHANGELOG_REALDATA_$(TIMESTAMP).md && \
	echo "**Generated:** $$(date)" >> CHANGELOG_REALDATA_$(TIMESTAMP).md && \
	echo "" >> CHANGELOG_REALDATA_$(TIMESTAMP).md && \
	echo "## Pipeline outputs in $(OUTDIR)" >> CHANGELOG_REALDATA_$(TIMESTAMP).md && \
	ls -1 ../out | grep metrics_ >> CHANGELOG_REALDATA_$(TIMESTAMP).md && \
	echo "" >> CHANGELOG_REALDATA_$(TIMESTAMP).md && \
	echo "## Commit Summary" >> CHANGELOG_REALDATA_$(TIMESTAMP).md && \
	git log -1 --oneline >> CHANGELOG_REALDATA_$(TIMESTAMP).md

# ------------------------------------------------------------
# NaN check: highlight affected CSVs, count totals, list runs
# ------------------------------------------------------------
nan-check:
	@echo "🔍 Checking for NaN values in CSV metrics..."
	@cd $(OUTDIR) && \
	files_with_nan=$$(grep -il "nan" metrics_*.csv || true); \
	count=$$(echo "$$files_with_nan" | wc -w); \
	total_nan=$$(grep -i "nan" metrics_*.csv | wc -l || true); \
	timestamp=$$(date +"%Y-%m-%d %H:%M:%S %Z"); \
	if [ "$$count" -gt 0 ]; then \
		echo "⚠️  Warning: $$count metrics files contain NaNs."; \
		echo "📊  Total NaN entries detected: $$total_nan"; \
		echo "🧾  Extracting run numbers..."; \
		run_list=$$(echo "$$files_with_nan" | grep -oE 'run[0-9]+' | sort -u | tr '\n' ' '); \
		if [ -z "$$run_list" ]; then run_list="(no explicit run numbers found)"; fi; \
		cd ../docs && \
		latest=$$(ls -t CHANGELOG_REALDATA_*.md | head -n1); \
		echo "" >> $$latest; \
		echo "⚠️  **Warning:** One or more metrics contain NaN values (detected automatically)." >> $$latest; \
		echo "**Total NaN entries detected:** $$total_nan" >> $$latest; \
		echo "🕒 **Last QA NaN check:** $$timestamp" >> $$latest; \
		echo "**Affected runs:** $$run_list" >> $$latest; \
		echo "Affected files:" >> $$latest; \
		echo "$$files_with_nan" | sed 's/^/- <span style="color:red">/;s/$$/<\/span>/' >> $$latest; \
	else \
		echo "✅ No NaN values detected."; \
	fi

# ------------------------------------------------------------
# Run everything: pipeline + changelog + NaN check
# ------------------------------------------------------------
full: run-qa generate-changelog nan-check
