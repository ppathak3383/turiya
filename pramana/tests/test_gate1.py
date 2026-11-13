# SPDX-License-Identifier: AGPL-3.0-or-later
"""Gate 1 (in Python, ahead of Phase 1): does a file's lineage survive copy and send, and resist forgery?
Run: python tests/test_gate1.py"""
import json, pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "src"))
from pramana import Identity, Keyring, Ledger, describe, sha

# Keys met in person (the family ceremony) and devices Arjun owns
arjun = Identity("Arjun", "person"); papa_phone_cam = Identity("Arjun's phone camera", "device")
laptop = Identity("Arjun's laptop", "device"); papa = Identity("Papa", "person")
gemma = Identity("Gemma on Arjun's laptop", "model")
kr = Keyring()
for i in (arjun, papa_phone_cam, laptop, papa, gemma):
    kr.meet(i)
stranger = Identity("Papa", "person")            # an impostor using the same name, different key

L = Ledger()
photo = b"\xff\xd8 JPEG bytes of the C-1 retaining wall \xff\xd9"
L.add(papa_phone_cam.sign(photo, "captured", device="phone", lens="main"))

results = []
def check(name, cond, detail=""):
    results.append((name, bool(cond), detail)); print(("PASS " if cond else "FAIL ") + name, detail)

d = describe(photo, L, kr)
check("1 captured photo is labelled by the device that captured it", d["label"] == "captured by Arjun's phone camera", d["label"])

copy = bytes(photo)                               # sent over WhatsApp/email unchanged
check("2 byte-identical copy keeps its lineage", describe(copy, L, kr)["label"] == d["label"])

reencoded = photo.replace(b"JPEG", b"JPEG-q80")    # recompressed by a messenger
check("3 re-encoded copy with no new record shows origin unknown", describe(reencoded, L, kr)["label"] == "origin unknown")

L.add(laptop.sign(reencoded, "derived", parents=[sha(photo)], edit="resize 50%"))
d4 = describe(reencoded, L, kr)
check("4 a signed edit links back to the captured original", "edited by Arjun's laptop from: captured by Arjun's phone camera" == d4["label"], d4["label"])

fake_img = b"PNG bytes of a deepfake wall crack"
gen = gemma.sign(fake_img, "generated", model="image-model-x")
L.add(gen)
tampered = dict(gen); tampered["kind"] = "captured"   # try to relabel generated as captured
L.add(tampered)
d5 = describe(fake_img, L, kr)
check("5 relabelling 'generated' as 'captured' breaks the signature and is ignored",
      d5["kind"] == "generated" and any("failed verification" in f for f in d5["flags"]), d5)

forged = papa.sign(b"voice note", "testified"); forged["sig"] = stranger.sign(b"voice note", "testified")["sig"]
L2 = Ledger(); L2.add(forged)
check("6 a forged signature fails and the note shows origin unknown", describe(b"voice note", L2, kr)["label"] == "origin unknown")

voice = b"OPUS bytes: beta, I'm at the station, please pick me up"
L.add(papa.sign(voice, "testified", channel="whatsapp"))
check("7 a voice note signed with Papa's key (met in person) shows as sent by Papa", describe(voice, L, kr)["label"] == "sent by Papa")

scam = b"OPUS bytes: beta, I'm stuck, send Rs 25,000 now"
L.add(stranger.sign(scam, "testified", channel="whatsapp"))
d8 = describe(scam, L, kr)
check("8 a voice note from a different key using Papa's name is not shown as Papa",
      "unknown key claiming to be 'Papa'" in d8["label"] and any("not met" in f for f in d8["flags"]), d8["label"])

L.add(papa_phone_cam.sign(fake_img, "captured"))  # a trusted device later signs the generated image as captured
d9 = describe(fake_img, L, kr)
check("9 generated + captured on the same bytes: shown as generated, conflict flagged",
      d9["kind"] == "generated" and any("conflicting" in f for f in d9["flags"]), d9["flags"])

check("10 changing even one byte loses the original's lineage", describe(photo[:-1] + b"\x00", L, kr)["label"] == "origin unknown")

n = sum(ok for _, ok, _ in results)
print(f"\nGATE 1 (Python): {n}/{len(results)} pass")
out = pathlib.Path(__file__).resolve().parent.parent / "runs"
out.mkdir(exist_ok=True)
json.dump([{"test": t, "pass": ok, "detail": str(det)} for t, ok, det in results], open(out / "gate1_2026-11-12.json", "w"), indent=1)
sys.exit(0 if n == len(results) else 1)
