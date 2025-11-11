#!/bin/bash
set -e
fail=0
for f in out/metrics_*_perrun.csv; do
  if [ ! -f "$f" ]; then
    continue
  fi
  header=$(head -n 1 "$f")
  if [[ "$header" == *"neighbors_median"* && "$header" == *"neighbors_mad"* && "$header" == *"z_local"* && "$header" == *"is_outlier_weak"* && "$header" == *"is_outlier_strong"* ]]; then
    echo "[OK] $f has robust columns."
  else
    echo "[FAIL] $f missing robust columns."
    fail=$((fail+1))
  fi
done
if [ $fail -eq 0 ]; then
  echo "All per-run CSVs have robust columns."
  exit 0
else
  echo "$fail files missing robust columns."
  exit 1
fi
