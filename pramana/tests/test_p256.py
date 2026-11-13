# SPDX-License-Identifier: AGPL-3.0-or-later
"""Hardware-curve keys (ECDSA P-256, what a TPM or phone secure element holds) work alongside Ed25519.
Run: python tests/test_p256.py"""
import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "src"))
from pramana import Identity, Keyring, Ledger, describe, sha
laptop_tpm = Identity("Arjun's laptop (TPM)", "device", alg="p256")
phone_se = Identity("Arjun's phone (secure element)", "person", alg="p256")
papa_sw = Identity("Papa", "person")                         # Ed25519, software key
impostor = Identity("Arjun's phone (secure element)", "person", alg="p256")
kr = Keyring(); [kr.meet(i) for i in (laptop_tpm, phone_se, papa_sw)]
L = Ledger(); doc = b"agreement.pdf"; edit = b"agreement-signed.pdf"
L.add(phone_se.sign(doc, "testified")); L.add(laptop_tpm.sign(edit, "derived", parents=[sha(doc)]))
L.add(papa_sw.sign(b"note", "testified")); L.add(impostor.sign(b"fake", "testified"))
bad = phone_se.sign(b"x", "testified"); bad["sig"] = bad["sig"][:-2] + ("00" if bad["sig"][-2:] != "00" else "11"); L.add(bad)
fails = 0
for name, cond in [
    ("P-256 testified record verifies", describe(doc, L, kr)["label"] == "sent by Arjun's phone (secure element)"),
    ("P-256 edit chains to a P-256 parent", describe(edit, L, kr)["label"].startswith("edited by Arjun's laptop (TPM) from: sent by")),
    ("Ed25519 still verifies beside P-256", describe(b"note", L, kr)["label"] == "sent by Papa"),
    ("a P-256 impostor with the same name is not trusted", "unknown key" in describe(b"fake", L, kr)["label"]),
    ("a corrupted P-256 signature fails", describe(b"x", L, kr)["label"] == "origin unknown"),
]:
    print(("PASS " if cond else "FAIL ") + name); fails += not cond
print(f"\n{5 - fails}/5 pass"); sys.exit(1 if fails else 0)
