#!/usr/bin/env python3
"""Checks the linked GoldHEN plugin ELF (before create-fself) without running it:
- exports plugin_load, plugin_unload and the g_plugin* attributes GoldHEN reads,
- exports nothing else (the shared core stays hidden),
- imports only functions that libkernel or libSceLibcInternal provide (both are loaded in every
  PS4 process; anything else would fail to resolve inside a game),
- has no TLS and no text relocations.
Uses llvm-readelf / llvm-nm (LLVM 18) and the OpenOrbis library stubs."""
import os
import subprocess
import sys

elf = sys.argv[1]
tc = os.environ.get('OO_PS4_TOOLCHAIN', os.path.expanduser('~/ps4dev/OpenOrbis/PS4Toolchain'))


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def dyn(path, defined):
    out = run('llvm-nm-18', '-D', '--defined-only' if defined else '--undefined-only', path)
    return {line.split()[-1] for line in out.splitlines() if line.strip()}


exports = dyn(elf, True) - {'_init', '_fini', 'module_start', 'module_stop', '__dso_handle',
                            '_sceLibc', '_sceProcessParam', '__init_array_start', '__init_array_end'}
# section markers the OpenOrbis linker script (link.x) defines in every binary
exports = {e for e in exports if not (e.startswith('__') and (e.endswith('_start') or e.endswith('_end')))}
want = {'plugin_load', 'plugin_unload', 'g_pluginName', 'g_pluginDesc', 'g_pluginAuth', 'g_pluginVersion'}
missing = want - exports
extra = exports - want
imports = dyn(elf, False)
allowed = dyn(os.path.join(tc, 'lib', 'libkernel.so'), True) | dyn(os.path.join(tc, 'lib', 'libSceLibcInternal.so'), True)
foreign = sorted(imports - allowed)
headers = run('llvm-readelf-18', '-l', '-d', elf)
problems = []
if missing:
    problems.append('missing exports: %s' % sorted(missing))
if extra:
    problems.append('unexpected exports: %s' % sorted(extra))
if foreign:
    problems.append('imports no PS4 process is sure to have: %s' % foreign)
if ' TLS ' in headers:
    problems.append('has a TLS segment')
if 'TEXTREL' in headers:
    problems.append('has text relocations')
if problems:
    print('FAIL: ' + '; '.join(problems))
    sys.exit(1)
print('PASS: GoldHEN plugin ELF: exports %s; %d imports, all from libkernel/libSceLibcInternal (%s); no TLS, no text relocations'
      % (', '.join(sorted(want)), len(imports), ', '.join(sorted(imports))))
