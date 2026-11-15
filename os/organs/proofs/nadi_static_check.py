#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - the syntactic half of Nadi's read-only proof: the proved function is the only road to a stick,
# and nothing else Nadi sends can carry data out.
#   1. bulk() is called only from scsi() (whose every transfer the CBMC proof checks);
#   2. every control request whose direction is OUT carries no data, and is one of SET_CONFIGURATION,
#      SET_PROTOCOL or SET_IDLE; every other request is IN (bmRequestType bit 7 set);
#   3. control() itself only ever builds an IN data stage;
#   4. no Normal transfer is queued anywhere but bulk() (the stick) and the keyboard's IN endpoint.
import re, sys
org = sys.argv[1] if len(sys.argv) > 1 else '/root/turiya0/organs'

def strip_comments(t):
    out = list(t)
    for m in re.finditer(r'/\*.*?\*/|//[^\n]*', t, re.S):
        for i in range(m.start(), m.end()):
            if out[i] != '\n': out[i] = ' '
    return ''.join(out)

def functions(t):
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

src = {f: strip_comments(open(f'{org}/{f}').read()) for f in ('nadi.cc', 'nadi_scsi.h')}
bad = []
OUT_OK = {('0x00', '9'), ('0x21', '0x0b'), ('0x21', '0x0a')}      # set configuration, set protocol, set idle

for f, t in src.items():
    line = lambda p: t[:p].count('\n') + 1
    for m in re.finditer(r'\bbulk\(', t):
        fn = function_of(t, m.start())
        if fn in ('scsi',) or t[max(0, m.start() - 11):m.start()].endswith('static int '): continue
        bad.append(f'{f}:{line(m.start())}: bulk() called from {fn}()')
    for m in re.finditer(r'\bcontrol\(([^;]*?)\)\)?\s*[;)|{]', t):
        fn = function_of(t, m.start())
        if t[max(0, m.start() - 11):m.start()].endswith('static int '): continue   # the definition
        args = [a.strip() for a in m.group(1).split(',')]
        if len(args) < 6: bad.append(f'{f}:{line(m.start())}: control() with {len(args)} arguments'); continue
        rt, req, ln = args[1], args[2], args[5].rstrip(')')
        try: rtv = int(rt, 0)
        except ValueError: bad.append(f'{f}:{line(m.start())}: control() with a computed request type {rt}'); continue
        if rtv & 0x80: continue                                   # IN: the device answers, nothing goes out
        if ln != '0': bad.append(f'{f}:{line(m.start())}: an OUT request with data ({rt}, {req}, length {ln})')
        elif (rt, req) not in OUT_OK: bad.append(f'{f}:{line(m.start())}: an OUT request not on the list ({rt}, {req})')

t = src['nadi.cc']
c = t[t.index('static int control('):]
c = c[:c.index('\n}\n')]
if 'ring_push(r, buf.pa, len, 3u << 10 | 1u << 16);' not in c:
    bad.append('nadi.cc: control() data stage is not plainly IN')
if '(len ? 3u << 16 : 0)' not in c:
    bad.append('nadi.cc: control() set-up stage does not announce an IN data stage')
for f, tt in src.items():
    for m in re.finditer(r'ring_push\(([^,]+),[^;]*?1u << 10', tt):
        fn = function_of(tt, m.start())
        ring = m.group(1).strip()
        if (fn == 'bulk' and ring == 'r') or ring == 'kin': continue
        bad.append(f'{f}: a Normal transfer queued on {ring} in {fn}()')
if not re.search(r'kbd_dci = \(uint8_t\)\(kbd_ep \* 2 \+ 1\)', t):
    bad.append('nadi.cc: the keyboard endpoint is not plainly IN')

print('\n'.join(bad) if bad else 'static check: scsi() is the only road to a stick; nothing else carries data out (0 findings)')
sys.exit(1 if bad else 0)
