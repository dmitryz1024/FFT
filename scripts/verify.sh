#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/results
bash scripts/build.sh 2>&1 | tee build/results/build.log
bin/eco_fft --test 2>&1 | tee build/results/tests.log
build/release/eco_fft_static --test 2>&1 | tee build/results/static-tests.log
build/release/eco_fft_faults 2>&1 | tee build/results/faults.log
python3 scripts/check_cli.py
python3 scripts/check_cli.py --exe=build/release/eco_fft_static --static-only --report=cli-static.json
python3 scripts/check_acom.py
(cd build/release && ./eco_fft_example) 2>&1 | tee build/results/example.log
bin/eco_fft --demo 2>&1 | tee build/results/demo.log
cmake -S . -B build/sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug -DECO_SANITIZE=ON 2>&1 | tee build/results/sanitize-build.log
cmake --build build/sanitize 2>&1 | tee -a build/results/sanitize-build.log
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
export UBSAN_OPTIONS=halt_on_error=1
build/sanitize/eco_fft --test 2>&1 | tee build/results/sanitizer.log
build/sanitize/eco_fft_faults 2>&1 | tee build/results/faults-sanitizer.log
python3 scripts/check_cli.py --exe=build/sanitize/eco_fft --report=cli-sanitizer.json
python3 scripts/check_acom.py --build-dir=build/sanitize --report=acom-sanitizer.json
python3 scripts/audit_component.py
python3 scripts/record_environment.py
echo "Verification passed. Logs and measurements: build/results/"
