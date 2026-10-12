from datetime import datetime, timezone
import json
import platform
import shutil
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
def run(*command):
    return subprocess.check_output(command, text=True).strip()
cpu = Path('/proc/cpuinfo').read_text()
model = next((line.split(':', 1)[1].strip() for line in cpu.splitlines() if line.startswith('model name')), 'unknown')
data = {'timestamp_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
        'architecture': platform.machine(), 'cpu': model, 'compiler': run('cc', '--version').splitlines()[0],
        'cmake': run('cmake', '--version').splitlines()[0], 'python': platform.python_version(),
        'libc': platform.libc_ver(), 'long_double': 'x86_64 System V, 16-byte storage, 64-bit mantissa (checked by client)',
        'benchmark': {'clock': 'CLOCK_MONOTONIC', 'samples': 7, 'warmup_batches': 1,
                      'batch': '32 for N<=256; 8 for N<=4096; 2 otherwise; DFT one',
                      'statistic': 'median microseconds per operation',
                      'soa': 'complete allocation + planning + transform + cleanup',
                      'ooa': 'Load + FFT/IFFT + Store; existing plan and vectors',
                      'setup': 'CreatePlan + Release',
                      'reference_fft': 'independent recursive Cooley-Tukey with sinl/cosl, power-of-two sizes only',
                      'reference_dft': 'direct complex DFT with sinl/cosl; measured only for N<=1024',
                      'limitations': 'Shared host, no CPU pinning; no universal speedup claim'}}
reference_log = root / 'build/results/fftw-comparison.log'
if reference_log.exists():
    version_line = next((line for line in reference_log.read_text().splitlines()
                         if line.startswith('FFTW_REFERENCE ')), 'not recorded')
    data['fftw_comparison'] = {
        'library': version_line,
        'client': 'optional eco_fft_reference; core and normal eco_fft do not link FFTW',
        'precision': 'long double for both implementations; FFTW_BACKWARD divided by N',
        'planner': 'FFTW_ESTIMATE, one thread, no imported wisdom; warm in-process planner cache',
        'clock': 'CLOCK_MONOTONIC', 'samples': 7,
        'warmup': 'calibrate each metric by doubling batch size to >=10 ms, maximum 65536 operations',
        'statistic': 'median microseconds per operation; batch sizes included in CSV',
        'eco_soa': 'allocation + plan + FFT/IFFT + cleanup',
        'fftw_full': 'allocation + plan + input copy + FFT/IFFT + normalization/output copy + cleanup',
        'eco_ooa': 'Load + FFT/IFFT + Store with existing plan and vectors',
        'fftw_reuse': 'input copy + FFT/IFFT + normalization/output copy with existing plan and buffers',
        'eco_setup': 'CreatePlan + Release, including internal state and buffers',
        'fftw_setup': 'create/destroy one directional plan; external I/O buffers already allocated',
        'accuracy': '18 lengths, 5 signals, 2 directions, 2 algorithms, SOA and OOA; 720 comparisons',
        'limitations': 'Shared host, no CPU pinning; ESTIMATE is not a claim about peak FFTW performance'
    }
(root / 'build/results/environment.json').write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
if shutil.which('dpkg-query'):
    (root / 'build/results/debian-packages.tsv').write_text(run('dpkg-query', '-W', '-f=${Package}\t${Version}\n') + '\n', encoding='utf-8')
print('Environment recorded:', data['compiler'], data['cpu'])
