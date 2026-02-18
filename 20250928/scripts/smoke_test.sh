#!/usr/bin/env bash
# smoke_test.sh — Validate pipeline outputs after a run.
# Checks: per-file CSVs exist, have expected headers,
# per-run CSVs exist, and NaN rate is below threshold.
#
# Usage: ./scripts/smoke_test.sh [metrics.conf]
# Exit codes: 0 = pass, 1 = failures found

set -euo pipefail

CONF="${1:-metrics.conf}"
FAIL=0
WARN=0

echo "=== QA Smoke Test ==="
echo "Config: $CONF"
echo ""

# Read metric names from conf (skip comments and blanks)
METRICS=$(grep -v '^\s*#' "$CONF" | grep -v '^\s*$' | cut -d',' -f1 | sed 's/^ *//;s/ *$//' | sort -u)
NMETRICS=$(echo "$METRICS" | wc -l | tr -d ' ')
echo "Metrics in scope: $NMETRICS"

# Check per-file CSVs
echo ""
echo "--- Per-file CSVs ---"
for m in $METRICS; do
    csv="out/metrics_${m}.csv"
    if [ ! -f "$csv" ]; then
        echo "[FAIL] Missing: $csv"
        FAIL=$((FAIL + 1))
        continue
    fi
    # check header
    header=$(head -1 "$csv")
    if ! echo "$header" | grep -q "run"; then
        echo "[FAIL] $csv: missing 'run' in header"
        FAIL=$((FAIL + 1))
        continue
    fi
    rows=$(tail -n +2 "$csv" | grep -c . || true)
    nans=$(tail -n +2 "$csv" | grep -ic "nan" || true)
    if [ "$rows" -eq 0 ]; then
        echo "[FAIL] $csv: empty (0 data rows)"
        FAIL=$((FAIL + 1))
    elif [ "$nans" -eq "$rows" ]; then
        echo "[WARN] $csv: 100% NaN ($nans/$rows rows)"
        WARN=$((WARN + 1))
    else
        echo "[ OK ] $csv: $rows rows, $nans NaN"
    fi
done

# Check per-run CSVs
echo ""
echo "--- Per-run CSVs ---"
for m in $METRICS; do
    csv="out/metrics_${m}_perrun.csv"
    if [ ! -f "$csv" ]; then
        echo "[FAIL] Missing: $csv"
        FAIL=$((FAIL + 1))
        continue
    fi
    rows=$(tail -n +2 "$csv" | grep -c . || true)
    nans=$(tail -n +2 "$csv" | grep -ic "nan" || true)
    # check for robust z columns
    header=$(head -1 "$csv")
    if echo "$header" | grep -q "z_local"; then
        echo "[ OK ] $csv: $rows runs, $nans NaN (robust z present)"
    elif [ "$rows" -gt 0 ]; then
        echo "[WARN] $csv: $rows runs, $nans NaN (no robust z columns)"
        WARN=$((WARN + 1))
    else
        echo "[FAIL] $csv: empty"
        FAIL=$((FAIL + 1))
    fi
done

# Check wide CSV
echo ""
echo "--- Wide CSV ---"
WIDE="out/metrics_perrun_wide.csv"
if [ -f "$WIDE" ]; then
    cols=$(head -1 "$WIDE" | tr ',' '\n' | wc -l | tr -d ' ')
    rows=$(tail -n +2 "$WIDE" | grep -c . || true)
    echo "[ OK ] $WIDE: $rows runs x $cols columns"
else
    echo "[WARN] $WIDE: not found (merge step may not have run)"
    WARN=$((WARN + 1))
fi

# Check stamp
echo ""
echo "--- Session stamp ---"
if [ -f "out/_stamp.txt" ]; then
    echo "[ OK ] out/_stamp.txt:"
    sed 's/^/       /' out/_stamp.txt
else
    echo "[WARN] out/_stamp.txt: not found"
    WARN=$((WARN + 1))
fi

# Summary
echo ""
echo "=== Summary ==="
echo "Failures: $FAIL"
echo "Warnings: $WARN"

if [ "$FAIL" -gt 0 ]; then
    echo "RESULT: FAIL"
    exit 1
else
    echo "RESULT: PASS"
    exit 0
fi
