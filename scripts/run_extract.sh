#!/usr/bin/env bash
# Move to repo root and then into 20250928 directory
cd "$(git rev-parse --show-toplevel)" || exit 1
cd 20250928 || exit 1
mkdir -p lists out data
# Always generate a mock input root file
root -l -b -q 'macros/make_mock_inputs.C("data/smoke.root")'
# Write absolute path of mock file to lists/files.txt
echo "$(pwd)/data/smoke.root" > lists/files.txt
# Run the metrics extraction macro
root -l -b -q 'macros/extract_metrics_v2.C("lists/files.txt","metrics.conf")'
