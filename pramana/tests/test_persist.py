# SPDX-License-Identifier: AGPL-3.0-or-later
"""Persistence: the ledger survives a restart, and lineage travels with a file via its sidecar.
Run: python tests/test_persist.py"""
import json, pathlib, subprocess, sys, tempfile
SRC = pathlib.Path(__file__).resolve().parent.parent / "src"
sys.path.insert(0, str(SRC))
from pramana import (Identity, Keyring, Ledger, describe, sha, save_record, load_ledger, save_keyring,
                     load_keyring, export_sidecar, import_sidecar)
cam, laptop, papa, stranger = (Identity("Arjun's phone camera", "device"), Identity("Arjun's laptop", "device"),
                               Identity("Papa", "person"), Identity("Papa", "person"))
fails = 0
def check(name, cond):
    global fails; fails += not cond; print(("PASS " if cond else "FAIL ") + name)
with tempfile.TemporaryDirectory() as d:
    d = pathlib.Path(d); store, krp = d / "store.jsonl", d / "keyring.json"
    kr = Keyring(); [kr.meet(i) for i in (cam, laptop, papa)]; save_keyring(kr, krp)
    photo = b"JPEG bytes of the slab"; small = b"JPEG bytes of the slab, resized"
    save_record(store, cam.sign(photo, "captured")); save_record(store, laptop.sign(small, "derived", parents=[sha(photo)]))
    L, K = load_ledger(store), load_keyring(krp)                      # "after a restart"
    check("ledger and keyring survive a restart", describe(photo, L, K)["label"] == "captured by Arjun's phone camera")
    check("the edit chain survives too", describe(small, L, K)["label"].startswith("edited by Arjun's laptop from: captured"))
    n = export_sidecar(small, L, d / "small.jpg.pramana.json")
    check("sidecar carries the edit and its original (2 records)", n == 2)
    fresh = Ledger(); m = import_sidecar(d / "small.jpg.pramana.json", fresh)   # a machine that never saw the store
    check("a new machine rebuilds the lineage from the sidecar", m == 2 and describe(small, fresh, K)["label"].startswith("edited by"))
    side = json.load(open(d / "small.jpg.pramana.json")); side[0]["kind"] = "captured"
    json.dump(side, open(d / "bad.pramana.json", "w")); fresh2 = Ledger()
    check("a tampered sidecar record is dropped on import", import_sidecar(d / "bad.pramana.json", fresh2) == 1)
    fake = stranger.sign(b"voice", "testified"); json.dump([fake], open(d / "v.pramana.json", "w")); f3 = Ledger()
    import_sidecar(d / "v.pramana.json", f3)
    check("an impostor's sidecar still shows the unknown key", "unknown key" in describe(b"voice", f3, K)["label"])
    (d / "photo.jpg").write_bytes(photo)
    out = subprocess.run([sys.executable, str(SRC / "pramana.py"), "describe", str(d / "photo.jpg"), str(store), str(krp)],
                         capture_output=True, text=True).stdout.strip()
    check("CLI describes a file", out.startswith("captured by Arjun's phone camera"))
print(f"\n{7 - fails}/7 pass"); sys.exit(1 if fails else 0)
