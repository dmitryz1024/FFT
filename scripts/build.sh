#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
mkdir -p bin lib
cp build/release/eco_fft bin/eco_fft
chmod +x bin/eco_fft
cp build/release/C345*.so bin/
cp build/release/libC345*.a lib/
echo "Built bin/eco_fft, two shared components in bin/, and two static libraries in lib/."
