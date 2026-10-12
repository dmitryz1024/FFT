#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/results
cmake -S . -B build/reference -G Ninja -DCMAKE_BUILD_TYPE=Release -DECO_FFTW_REFERENCE=ON
cmake --build build/reference --target eco_fft_reference
build/reference/eco_fft_reference 2>&1 | tee build/results/fftw-comparison.log
python3 scripts/record_environment.py
cp build/results/environment.json build/results/fftw-environment.json
