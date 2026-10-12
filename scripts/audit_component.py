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
    if ' GetIEcoComponentFactoryPtr\n' not in exported:
        violations.append(f'{path.name}: Eco dynamic factory missing')
    libraries.append({'file': path.relative_to(root).as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'undefined_symbols': undefined.splitlines(), 'has_needed': '(NEEDED)' in dynamic,
                      'exports': exported.splitlines()})
if len(libraries) != 2:
    violations.append('Both independently loadable components must exist')
# Ask the preprocessor which files were really included. In particular this
# catches libc indirectly included by the original SDK depend.h.
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
    for token in dependencies.replace('\\\n', '').split()[1:]:
        if token.endswith('.h'):
            headers.add(token)
            if token.startswith('/usr/'):
                violations.append(f'Unexpected system header: {token}')
result = {'status': 'PASS' if not violations else 'FAIL', 'authored_source_count': len(source_files),
          'libraries': libraries, 'component_headers': sorted(headers), 'violations': sorted(set(violations)),
          'scope': 'Authored component only; official Eco runtime and independent client references are external.'}
(root / 'build/results/audit.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps(result, indent=2))
raise SystemExit(bool(violations))
