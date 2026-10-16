"""Verify real Eco discovery and binary replacement in an isolated deployment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', default='build/release')
parser.add_argument('--report', default='acom-integration.json')
options = parser.parse_args()
build = root / options.build_dir
results = root / 'build/results'
results.mkdir(parents=True, exist_ok=True)
cases = []
cid = ['C345FB78707144109821A6EF25310070', 'C345FB78707144109821A6EF25310071']
environment = dict(os.environ)
for key in ('ECO_FRAMEWORK_RT', 'LD_LIBRARY_PATH', 'LD_PRELOAD'):
    environment.pop(key, None)


def check(name, ok, detail=''):
    cases.append({'case': name, 'status': 'PASS' if ok else 'FAIL'})
    if not ok:
        print('FAIL', name, detail)


def run(executable, cwd, *args, env=None):
    return subprocess.run([str(executable), *args], cwd=cwd, env=env or environment,
                          input='1,0\n2,0\n3,0\n4,0\n', text=True,
                          capture_output=True, timeout=30)


for name in ('eco_fft', 'eco_fft_example', 'eco_fft_acom'):
    symbols = subprocess.check_output(['nm', '--defined-only', str(build / name)], text=True)
    forbidden = ('eco_dsp_create', 'eco_fft_kernel_', 'dsp_transform', 'factory_alloc',
                 'GetEcoFFT', 'GetIEcoComponentFactoryPtr_C345')
    check(f'{name}: no linked FFT implementation', not any(x in symbols for x in forbidden))
    private_deps = [p for p in (build / f'CMakeFiles/{name}.dir').rglob('*.o')]
    check(f'{name}: client object files exist', bool(private_deps))
    imports = subprocess.check_output(['nm', '-u', *map(str, private_deps)], text=True)
    check(f'{name}: no client loader calls', not any(' U ' + x in imports for x in ('dlopen', 'dlsym', 'dlclose')))

with tempfile.TemporaryDirectory(prefix='acom-', dir=results) as directory:
    deployment = Path(directory)
    binary_dir = deployment / 'components with spaces'
    binary_dir.mkdir()
    for name in ('eco_fft', 'eco_fft_example', 'eco_fft_acom'):
        shutil.copy2(build / name, binary_dir / name)
    executable = binary_dir / 'eco_fft'
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    # A real consumer must fail when no implementation is deployed.
    p = run(binary_dir / 'eco_fft_example', binary_dir)
    check('independent example fails without .so', p.returncode != 0 and not p.stdout, p.stdout + p.stderr)
    p = run(executable, deployment, '--fft', '--input=-')
    check('CLI fails without .so and has no static fallback', p.returncode != 0 and not p.stdout, p.stderr)
    for item in cid:
        shutil.copy2(build / (item + '.so'), binary_dir / (item + '.so'))
    p = run(binary_dir / 'eco_fft_acom', binary_dir)
    (results / ('acom-contract-sanitizer.log' if 'sanitize' in str(build) else 'acom-contract.log')).write_text(p.stdout + p.stderr)
    check('official bus, QI laws, lifetime, allocator selection', p.returncode == 0 and 'status=PASS' in p.stdout, p.stdout + p.stderr)
    p = run(binary_dir / 'eco_fft_example', binary_dir)
    check('independent example works after deployment', p.returncode == 0 and '0,10,0,1,0' in p.stdout, p.stderr)
    p = run(executable, deployment, '--fft', '--input=-')
    check('CLI locates modules beside executable and restores cwd', p.returncode == 0 and p.stdout.startswith('real,imag\n'), p.stderr)
    p = run(executable, deployment, '--demo', '--algorithm=dit')
    check('DIT implementation selected by CID', p.returncode == 0 and 'Component: EcoFFT DIT' in p.stdout, p.stderr)
    # Replace the provider of the same CID without recompiling or relinking
    # the consumer. The alternate implementation has the same public ABI.
    shutil.copyfile(build / (cid[1] + '.so'), binary_dir / (cid[0] + '.so'))
    p = run(executable, deployment, '--demo', '--algorithm=dit')
    check('replace provider under the same CID without relinking', p.returncode == 0 and 'Component: EcoFFT DIF' in p.stdout, p.stdout + p.stderr)
    check('consumer binary unchanged during replacement', hashlib.sha256(executable.read_bytes()).hexdigest() == digest)
    (binary_dir / (cid[0] + '.so')).write_bytes(b'not an ELF shared object\n')
    p = run(executable, deployment, '--fft', '--input=-')
    check('invalid .so is rejected by the actual bus', p.returncode != 0 and not p.stdout and 'QueryComponent failed' in p.stderr, p.stderr)
    shutil.copyfile(build / (cid[0] + '.so'), binary_dir / (cid[0] + '.so'))
    trace_env = dict(environment, LD_DEBUG='files,bindings')
    p = run(binary_dir / 'eco_fft_example', binary_dir, env=trace_env)
    relevant = '\n'.join(line for line in p.stderr.splitlines()
                         if cid[0] in line or 'GetIEcoComponentFactoryPtr' in line)
    check('ELF loader confirms standard Eco factory resolution', p.returncode == 0 and
          cid[0] + '.so' in relevant and 'GetIEcoComponentFactoryPtr' in relevant, relevant)
    trace = 'acom-loader-sanitizer.log' if 'sanitize' in str(build) else 'acom-loader.log'
    (results / trace).write_text(relevant + '\n')

failed = sum(item['status'] != 'PASS' for item in cases)
(results / options.report).write_text(json.dumps(cases, indent=2) + '\n')
print(f'ACOM_INTEGRATION checks={len(cases)} failures={failed} status={"FAIL" if failed else "PASS"}')
raise SystemExit(bool(failed))
