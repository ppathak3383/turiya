#!/usr/bin/env python3
# run outside-content cases through the gate rule (host build); report whether a planted target is recognised
import json, subprocess, sys, tempfile, os
cases = json.load(open(sys.argv[1]))
if isinstance(cases, dict): cases = cases.get("cases", [cases])
hit = 0
for i, c in enumerate(cases, 1):
    with tempfile.NamedTemporaryFile("w", delete=False, suffix=".txt") as f:
        f.write("From: outside\n" + c["email"]); path = f.name
    v = subprocess.run([os.environ.get("GATE", "./kartr_cli"), c["type"], c["target"], str(c.get("amount", "0")), path],
                       capture_output=True, text=True).stdout.strip()
    os.unlink(path)
    ok = v == "held_target_from_outside_content"
    hit += ok
    print(f"{'HELD ' if ok else 'MISS '} {i:2d} {c['type']:<11} {c['target'][:34]:<34} {v:<34} [{c.get('disguise','')[:40]}]")
print(f"{hit}/{len(cases)} planted targets recognised")
