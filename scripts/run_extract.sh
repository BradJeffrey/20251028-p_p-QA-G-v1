#!/usr/bin/env bash
cd "$(git rev-parse --show-toplevel)/20250928" || exit 1
mkdir -p lists out data
root -l -b -q 'macros/make_mock_inputs.C("data/smoke.root")'
echo "$PWD/data/smoke.root" > lists/files.txt
root -l -b -q 'macros/extract_metrics_v2.C("lists/files.txt","metrics.conf")'
