"""Exercise the public CLI with actual CSV data and an independent DFT oracle."""
import argparse
import cmath
import csv
import io
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--exe', default='bin/eco_fft')
parser.add_argument('--static-only', action='store_true')
options = parser.parse_args()
exe = root / options.exe
results_dir = root / 'build/results'
results_dir.mkdir(parents=True, exist_ok=True)
results = []


def run(arguments, data=None):
    return subprocess.run([str(exe), *arguments], cwd=root, input=data, text=True,
                          capture_output=True, timeout=60)


def check(name, passed, detail=''):
    results.append({'case': name, 'status': 'PASS' if passed else 'FAIL'})
    if not passed:
        print(f'FAIL {name}: {detail}')


def csv_text(values):
    return 'real,imag\n' + ''.join(f'{z.real:.17g},{z.imag:.17g}\n' for z in values)


def read_csv(text):
    rows = list(csv.reader(io.StringIO(text)))
    if not rows or rows[0] != ['real', 'imag'] or any(len(row) != 2 for row in rows[1:]):
        raise ValueError('Invalid CSV output')
    return [complex(float(row[0]), float(row[1])) for row in rows[1:]]


def close(actual, expected):
    return len(actual) == len(expected) and all(abs(a - b) <= 2e-11 * max(1, abs(b)) for a, b in zip(actual, expected))


def dft(values, inverse=False):
    n = len(values)
    sign = 1 if inverse else -1
    return [sum(z * cmath.exp(sign * 2j * cmath.pi * k * j / n) for j, z in enumerate(values)) /
            (n if inverse else 1) for k in range(n)]


def expect_values(name, result, expected):
    try:
        ok = result.returncode == 0 and not result.stderr and close(read_csv(result.stdout), expected)
    except (ValueError, OverflowError):
        ok = False
    check(name, ok, result.stderr + result.stdout[:200])


for arguments, code, marker in [([], 0, 'Usage:'), (['--help'], 0, 'Usage:'),
    (['--demo', '--algorithm=dit'], 0, 'Component: EcoFFT DIT'),
    (['--algorithm=invalid'], 1, 'Algorithm must be'),
    (['--unknown-option'], 1, 'Unknown option:'),
    (['--fft'], 1, '--input=FILE'),
    (['--fft', '--ifft', '--input=-'], 1, 'exactly one'),
    (['--fft', '--input=-', '--algorithm=both'], 1, 'one algorithm'),
    (['--fft', '--input=-', '--architecture=bad'], 1, 'Architecture must be'),
    (['--test', '--input=-'], 1, 'require --fft'),
    (['--fft', '--input=-', '--output='], 1, 'must not be empty')]:
    result = run(arguments)
    check('arguments ' + ' '.join(arguments), result.returncode == code and marker in result.stdout + result.stderr)

signals = [[1+0j, 2+0j, 3+0j, 4+0j], [1+.5j, -2+1j, -1j, 3+0j, .25+2j, -1-.5j, 2+1.5j], [3-4j]]
link_modes = ['static'] if options.static_only else ['static', 'dynamic']
for linking in link_modes:
    for algorithm in ('dit', 'dif'):
        for architecture in ('service', 'object'):
            extra = [f'--algorithm={algorithm}', f'--architecture={architecture}']
            if linking == 'dynamic':
                extra.append('--dynamic=bin')
            for values in signals:
                tag = f'{linking}/{algorithm}/{architecture}/N={len(values)}'
                forward = run(['--fft', '--input=-', *extra], csv_text(values))
                expect_values(tag + ' FFT', forward, dft(values))
                expect_values(tag + ' IFFT', run(['--ifft', '--input=-', *extra], csv_text(values)), dft(values, True))
                if forward.returncode == 0:
                    expect_values(tag + ' roundtrip', run(['--ifft', '--input=-', *extra], forward.stdout), values)

with tempfile.TemporaryDirectory(prefix='cli-', dir=results_dir) as directory:
    temp = Path(directory)
    source = temp / 'input with spaces.csv'
    output = temp / 'output with spaces.csv'
    source.write_bytes(b'\xef\xbb\xbf# a comment\r\nreal,imag\r\n\r\n 1 , 0 \r\n2,0\r\n3,0\r\n4,0\r\n')
    result = run(['--fft', f'--input={source}', f'--output={output}'])
    check('BOM CRLF comments and file paths with spaces', result.returncode == 0 and result.stdout == '' and
          output.exists() and close(read_csv(output.read_text()), [10, -2+2j, -2, -2-2j]), result.stderr)
    result = run(['--ifft', f'--input={output}', f'--output={output}', '--architecture=object'])
    check('input and output may share a path', result.returncode == 0 and close(read_csv(output.read_text()), signals[0]))
    invalid = ['', 'real,imag\n', 'NaN,0\n', '0,Inf\n', '1e101,0\n', '1e9999,0\n',
               '1,2,3\n', '1\n', 'a,0\n', ',1\n', '1,\n', '1,2junk\n',
               '0,' + '1' * 1100 + '\n', '0,0\n' * 65537]
    for index, content in enumerate(invalid):
        output.write_text('keep-existing-data', encoding='utf-8')
        result = run(['--fft', '--input=-', f'--output={output}'], content)
        check(f'invalid input {index}: existing output preserved', result.returncode != 0 and bool(result.stderr) and
              output.read_text() == 'keep-existing-data' and not result.stdout)
    missing = temp / 'missing.csv'
    result = run(['--fft', f'--input={missing}', f'--output={missing}'])
    check('missing input does not create output', result.returncode != 0 and not missing.exists())
    result = run(['--fft', '--input=-', f'--output={temp}'], '1,0\n')
    check('output open error propagates', result.returncode != 0 and bool(result.stderr))
    result = run(['--fft', '--input=-', '--output=/dev/full'], '1,0\n')
    check('output write error propagates', result.returncode != 0 and bool(result.stderr))

for architecture in ('service', 'object'):
    # A maximum-size impulse has a known spectrum at every bin.
    result = run(['--fft', '--input=-', f'--architecture={architecture}'], '1,0\n' + '0,0\n' * 65535)
    expect_values('maximum N ' + architecture, result, [1+0j] * 65536)

if not options.static_only:
    result = run(['--fft', '--input=-', '--dynamic=build/missing'], '1,0\n')
    check('missing dynamic library error', result.returncode != 0 and 'dlopen:' in result.stderr and not result.stdout)
failed = sum(item['status'] != 'PASS' for item in results)
name = 'cli-sanitizer.json' if options.static_only else 'cli-checks.json'
(results_dir / name).write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
print(f'CLI_RESULT checks={len(results)} failures={failed} status={"FAIL" if failed else "PASS"}')
raise SystemExit(bool(failed))
