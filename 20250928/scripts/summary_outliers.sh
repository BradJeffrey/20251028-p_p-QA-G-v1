#!/bin/bash
set -e
for f in out/metrics_*_perrun.csv; do
  [ -f "$f" ] || continue
  strong=$(awk -F, 'NR>1 && $9==1 {c++} END {print c+0}' "$f")
  weak=$(awk -F, 'NR>1 && $8==1 {c++} END {print c+0}' "$f")
  echo "$(basename "$f"): strong=$strong weak=$weak"
done
