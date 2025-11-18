# ============================================================
#  sPHENIX Real Data QA Pipeline - Clean Final Makefile
# ============================================================

TIMESTAMP := $(shell date +"%Y%m%d%H%M")
OUTDIR := 20250928/out
DOCSDIR := 20250928/docs

.PHONY: run-qa generate-changelog update-latest nan-check full-push

# ------------------------------------------------------------
# Run the full QA analysis chain
# ------------------------------------------------------------
run-qa:
	cd 20250928 && \
	root -l -b -q 'macros/extract_quick.C("lists/files.txt")' && \
	root -l -b -q 'macros/aggregate_per_run_v2.C("metrics.conf","entries")' && \
	root -l -b -q 'macros/plot_dashboard.C()'

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
# Improved nan-check: list affected CSVs with NaNs
# ------------------------------------------------------------
nan-check:
	@echo "🔍 Checking for NaN values in CSV metrics..."
	@cd $(OUTDIR) && \
	files_with_nan=$$(grep -il "nan" metrics_*.csv || true); \
	count=$$(echo "$$files_with_nan" | wc -w); \
	if [ "$$count" -gt 0 ]; then \
		echo "⚠️  Warning: $$count metrics files contain NaNs."; \
		cd ../docs && \
		latest=$$(ls -t CHANGELOG_REALDATA_*.md | head -n1); \
		echo "" >> $$latest; \
		echo "⚠️  **Warning:** One or more metrics contain NaN values (detected automatically)." >> $$latest; \
		echo "Affected files:" >> $$latest; \
		echo "$$files_with_nan" | sed 's/^/- /' >> $$latest; \
	else \
		echo "✅ No NaN values detected."; \
	fi

# ------------------------------------------------------------
# Copy latest changelog and push to GitHub
# ------------------------------------------------------------
update-latest:
	cd $(DOCSDIR) && \
	latest=$$(ls -t CHANGELOG_REALDATA_*.md | head -n1) && \
	cp $$latest CHANGELOG_LATEST.md
	git add $(DOCSDIR)/CHANGELOG_LATEST.md
	git commit -m "Update CHANGELOG_LATEST.md to $$latest" || true
	git push origin results/intt-metrics

# ------------------------------------------------------------
# Run everything and push
# ------------------------------------------------------------
full-push: run-qa generate-changelog nan-check update-latest
	git add $(OUTDIR) $(DOCSDIR)/CHANGELOG_REALDATA_$(TIMESTAMP).md
	git commit -m "Automated QA run + changelog ($(TIMESTAMP))"
	git push origin results/intt-metrics

# ------------------------------------------------------------
# Enhanced nan-check: highlight affected CSVs in red (for GitHub)
# ------------------------------------------------------------
nan-check:
	@echo "🔍 Checking for NaN values in CSV metrics..."
	@cd $(OUTDIR) && \
	files_with_nan=$$(grep -il "nan" metrics_*.csv || true); \
	count=$$(echo "$$files_with_nan" | wc -w); \
	if [ "$$count" -gt 0 ]; then \
		echo "⚠️  Warning: $$count metrics files contain NaNs."; \
		cd ../docs && \
		latest=$$(ls -t CHANGELOG_REALDATA_*.md | head -n1); \
		echo "" >> $$latest; \
		echo "⚠️  **Warning:** One or more metrics contain NaN values (detected automatically)." >> $$latest; \
		echo "Affected files:" >> $$latest; \
		echo "$$files_with_nan" | sed 's/^/- <span style="color:red">/;s/$$/<\/span>/' >> $$latest; \
	else \
		echo "✅ No NaN values detected."; \
	fi


# ------------------------------------------------------------
# Enhanced nan-check: highlight affected CSVs in red and count total NaNs
# ------------------------------------------------------------
nan-check:
	@echo "🔍 Checking for NaN values in CSV metrics..."
	@cd $(OUTDIR) && \
	files_with_nan=$$(grep -il "nan" metrics_*.csv || true); \
	count=$$(echo "$$files_with_nan" | wc -w); \
	total_nan=$$(grep -i "nan" metrics_*.csv | wc -l || true); \
	if [ "$$count" -gt 0 ]; then \
		echo "⚠️  Warning: $$count metrics files contain NaNs."; \
		echo "📊  Total NaN entries detected: $$total_nan"; \
		cd ../docs && \
		latest=$$(ls -t CHANGELOG_REALDATA_*.md | head -n1); \
		echo "" >> $$latest; \
		echo "⚠️  **Warning:** One or more metrics contain NaN values (detected automatically)." >> $$latest; \
		echo "**Total NaN entries detected:** $$total_nan" >> $$latest; \
		echo "Affected files:" >> $$latest; \
		echo "$$files_with_nan" | sed 's/^/- <span style="color:red">/;s/$$/<\/span>/' >> $$latest; \
	else \
		echo "✅ No NaN values detected."; \
	fi


# ------------------------------------------------------------
# Enhanced nan-check: include timestamp in changelog
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
		cd ../docs && \
		latest=$$(ls -t CHANGELOG_REALDATA_*.md | head -n1); \
		echo "" >> $$latest; \
		echo "⚠️  **Warning:** One or more metrics contain NaN values (detected automatically)." >> $$latest; \
		echo "**Total NaN entries detected:** $$total_nan" >> $$latest; \
		echo "🕒 **Last QA NaN check:** $$timestamp" >> $$latest; \
		echo "Affected files:" >> $$latest; \
		echo "$$files_with_nan" | sed 's/^/- <span style="color:red">/;s/$$/<\/span>/' >> $$latest; \
	else \
		echo "✅ No NaN values detected."; \
	fi


# ------------------------------------------------------------
# Final nan-check: includes run numbers for NaN-affected metrics
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

run-qa:
	$(MAKE) -C 20250928 run-qa
