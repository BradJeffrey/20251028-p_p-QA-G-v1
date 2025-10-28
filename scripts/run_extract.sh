#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)/20250928"
mkdir -p lists out data
# Always generate mock input and update lists
root -l -b -q 'macros/make_mock_inputs.C("data/smoke.root")'
printf '%s\n' "$PWD/data/smoke.root" > lists/files.txt
root -l -b -q 'macros/extract_metrics_v2.C("lists/files.txt","metrics.conf")'
