#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - the syntactic half of Kosa's read-only proof: the proved function is the only road.
#   1. every register write in kosa.cc and kosa_cmd.h names a fixed set-up register below the doorbells,
#      except the two doorbell writes inside submit();
#   2. sq_doorbell / cq_doorbell are called nowhere else;
#   3. queue memory is written nowhere but submit() (and zeroed once in init, before the controller runs);
#   4. no register is reached except through r32/w32 (no other use of R).
import re, sys
org = sys.argv[1] if len(sys.argv) > 1 else '/root/turiya0/organs'
src = {f: open(f'{org}/{f}').read() for f in ('kosa.cc', 'kosa_cmd.h')}
SETUP = {'0x14', '0x24', '0x28', '0x30'}          # CC, AQA, ASQ, ACQ
bad = []

def strip_comments(t):
    """comments and strings become spaces, so positions stay where they were"""
    out = list(t)
    for m in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"', t, re.S):
        for i in range(m.start(), m.end()):
            if out[i] != '\n': out[i] = ' '
    return ''.join(out)

def functions(t):
    """(name, start, end) of every top-level function body"""
    spans = []
    for m in re.finditer(r'^[A-Za-z_\[][^;{}\n]*?\b(\w+)\s*\([^;{}]*\)\s*\{', t, re.M):
        depth, i = 0, m.end() - 1
        while i < len(t):
            if t[i] == '{': depth += 1
            elif t[i] == '}':
                depth -= 1
                if depth == 0: break
            i += 1
        spans.append((m.group(1), m.start(), i))
    return spans

def function_of(t, pos):
    for name, a, b in functions(t):
        if a <= pos <= b: return name
    return None

src = {f: strip_comments(t) for f, t in src.items()}
for f, t in src.items():
    for m in re.finditer(r'\b(w32|w64)\(([^,]+),', t):
        line = t[:m.start()].count('\n') + 1
        arg = m.group(2).strip()
        fn = function_of(t, m.start())
        if fn in ('w32', 'w64'): continue                      # the definitions themselves
        if arg in SETUP: continue
        if fn == 'submit' and arg in ('sq_doorbell(q.qid)', 'cq_doorbell(q.qid)'): continue
        bad.append(f'{f}:{line}: register write to {arg} in {fn}()')
    for m in re.finditer(r'\b(sq_doorbell|cq_doorbell)\(', t):
        fn = function_of(t, m.start())
        line = t[:m.start()].count('\n') + 1
        if fn not in ('submit', 'sq_doorbell', 'cq_doorbell') and not t[m.start()-9:m.start()].endswith('unsigned '):
            bad.append(f'{f}:{line}: doorbell named in {fn}()')
    for m in re.finditer(r'(->sq\b|\.sq\b|\bs->)', t):
        fn = function_of(t, m.start())
        line = t[:m.start()].count('\n') + 1
        if fn in ('submit', 'snapshot', 'init'): continue
        bad.append(f'{f}:{line}: queue memory touched in {fn}()')
    for m in re.finditer(r'\bR\b', t):
        fn = function_of(t, m.start())
        line = t[:m.start()].count('\n') + 1
        if fn in ('r32', 'w32', 'init', None): continue        # None: the declaration
        bad.append(f'{f}:{line}: registers reached directly in {fn}()')

# init may only zero the queue pages and hand their addresses to the controller
init = src['kosa.cc'][src['kosa.cc'].index('static uint32_t init('):]
init = init[:init.index('\n}\n')]
for m in re.finditer(r'(->sq|\.sq)\b', init):
    bad.append('kosa.cc: init() uses a queue pointer directly')

print('\n'.join(bad) if bad else 'static check: submit() is the only road to the controller (0 findings)')
sys.exit(1 if bad else 0)
