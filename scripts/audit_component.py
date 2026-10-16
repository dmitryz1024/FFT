"""Fail closed if authored component uses libc or has any dynamic imports."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1]
violations = []
source_files = sorted([*(root / 'src').glob('*.c'), *(root / 'src').glob('*.h'), *(root / 'include').glob('*.h')])
for path in source_files:
    text = path.read_text(encoding='utf-8-sig')
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    if re.search(r'#\s*include\s*<', text):
        violations.append(f'{path.name}: standard header include')
    prohibited = r'\b(malloc|calloc|realloc|free|memcpy|memmove|memset|memcmp|printf|fprintf|puts|sin|cos|sinf|cosf|sinl|cosl|sqrt|sqrtl|pow|powl|fabs|fabsl|fmod|fmodl|isfinite|isnan)\s*\('
    for match in re.finditer(prohibited, text):
        violations.append(f'{path.name}: forbidden call {match.group(1)}')
libraries = []
for path in sorted((root / 'build/release').glob('C345*.so')):
    undefined = subprocess.check_output(['nm', '-u', str(path)], text=True).strip()
    dynamic = subprocess.check_output(['readelf', '-d', str(path)], text=True)
    if undefined:
        violations.append(f'{path.name}: undefined symbols: {undefined}')
    if '(NEEDED)' in dynamic:
        violations.append(f'{path.name}: DT_NEEDED dependency')
    exported = subprocess.check_output(['nm', '-D', '--defined-only', str(path)], text=True)
    symbols = [line.split()[-1] for line in exported.splitlines()]
    if symbols != ['GetIEcoComponentFactoryPtr']:
        violations.append(f'{path.name}: expected only the standard Eco factory export, got {symbols}')
    libraries.append({'file': path.relative_to(root).as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'undefined_symbols': undefined.splitlines(), 'has_needed': '(NEEDED)' in dynamic,
                      'exports': exported.splitlines()})
if len(libraries) != 2:
    violations.append('Both independently loadable components must exist')
# Verify that the original SDK depend.h participates in every component build.
# Its system header declarations are allowed; the linked module must still
# contain no libc/libm imports. Never suppress or re-create SDK ABI definitions.
commands = json.loads((root / 'build/release/compile_commands.json').read_text())
import shlex
headers = set()
for command in commands:
    if Path(command['file']).name not in {p.name for p in (root / 'src').glob('*.c')}:
        continue
    args = shlex.split(command['command'])
    output = args.index('-o')
    del args[output:output + 2]
    args.remove('-c')
    args += ['-M']
    dependencies = subprocess.check_output(args, cwd=command['directory'], text=True)
    if str(root / 'vendor/eco/Eco.Core1/SharedFiles/depend.h') not in dependencies:
        violations.append(f'{command["file"]}: original SDK depend.h is missing')
    if Path(command['file']).name == 'eco_factory.c':
        if ('-DECO_DLL' in args) == ('-DECO_LIB' in args):
            violations.append('Factory must select exactly one of ECO_DLL and ECO_LIB')
    for token in dependencies.replace('\\\n', '').split()[1:]:
        if token.endswith('.h'):
            headers.add(token)
sdk = root / 'vendor/eco'
provenance = json.loads((sdk / 'provenance.json').read_text(encoding='utf-8-sig'))
for entry in provenance['files']:
    if hashlib.sha256((sdk / entry['path']).read_bytes()).hexdigest() != entry['sha256']:
        violations.append(f'Modified upstream SDK file: {entry["path"]}')
result = {'status': 'PASS' if not violations else 'FAIL', 'authored_source_count': len(source_files),
          'libraries': libraries, 'component_headers': sorted(headers), 'violations': sorted(set(violations)),
          'scope': 'Authored component only; official Eco runtime and independent client references are external.'}
(root / 'build/results/audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(f'AUDIT status={result["status"]} source_files={len(source_files)} components={len(libraries)} SDK_files={len(provenance["files"])}')
for violation in result['violations']:
    print(violation)
raise SystemExit(bool(violations))
