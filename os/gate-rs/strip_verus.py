#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya: the executable part of gate.rs, with the Verus proof annotations removed and nothing else changed.
# Removes: the verus! wrapper, the vstd import, spec functions, requires/ensures/invariant/decreases clauses,
# assert(...) lines (proof steps). Every executable line is kept as it is. Fails loudly on anything unexpected.
import re, sys
src = open(sys.argv[1]).read()
out, i, lines = [], 0, src.split("\n")
skip_clause = False
depth_spec = 0
for ln in lines:
    s = ln.strip()
    if depth_spec:                                   # inside a spec fn body
        depth_spec += ln.count("{") - ln.count("}")
        continue
    if s.startswith("use vstd::prelude") or s == "verus! {" or s.startswith("} // verus!"):
        continue
    if re.match(r"pub (open |closed )?spec fn ", s):
        depth_spec = ln.count("{") - ln.count("}")
        if depth_spec == 0 and not s.endswith("}"): sys.exit("spec fn without body brace: " + s)
        continue
    if skip_clause:
        if s == "{":                                 # the body starts: clauses are over
            skip_clause = False
            out.append(ln)
        continue
    if s in ("ensures", "requires", "invariant", "decreases") or re.match(r"^(ensures|requires|invariant|decreases)\b", s):
        skip_clause = True
        continue
    if s.startswith("assert("):
        if not s.endswith(";"): sys.exit("multi-line assert: " + s)
        continue
    ln = ln.replace("Structural", "Clone, Copy")
    ln = re.sub(r"-> \((\w+): ([\w\[\]; ]+)\)", r"-> \2", ln)
    out.append(ln)
text = "\n".join(out)
for word in ("ensures", "invariant", "decreases", "spec fn", "@", "=~="):
    if word in text: sys.exit("left over Verus syntax: " + word)
print(text)
