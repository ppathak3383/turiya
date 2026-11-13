# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for keyex.py. Run: PYTHONDONTWRITEBYTECODE=1 python pramana/tests/test_keyex.py"""
import json, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src"))
from pramana import Identity, Keyring, Ledger, describe
from keyex import card_payload, parse_card, qr_png, decode_qr, confirm_code, accept

try:
    import cv2  # noqa
    HAVE_CV = True
except ImportError:
    HAVE_CV = False


def setup():
    me = Identity("Arjun", "person")
    cam = Identity("Arjun's phone camera", "device", alg="p256")
    kr, L = Keyring(), Ledger()
    kr.meet(cam)
    return me, cam, kr, L


def snap(cam, L, payload):
    """Arjun's own camera photographs a QR on the other screen."""
    photo = qr_png(payload)
    L.add(cam.sign(photo, "captured", sensor="camera"))
    return photo


def code(me, other):
    return confirm_code(me.pub, other.pub)


def t1_in_person_adds_key():
    me, cam, kr, L = setup(); papa = Identity("Papa", "person")
    p = card_payload(papa); photo = snap(cam, L, p)
    r = accept(photo, L, kr, me, code(me, papa), payload=None if HAVE_CV else p)
    assert r["ok"] and kr.name(papa.pub) == "Papa", r


def t2_real_qr_roundtrip():
    if not HAVE_CV:
        return "skipped (no OpenCV)"
    for alg in ("ed25519", "p256"):
        p = card_payload(Identity("Mummy", "person", alg=alg))
        assert decode_qr(qr_png(p)) == p, alg


def t3_forwarded_qr_refused():
    me, cam, kr, L = setup(); papa = Identity("Papa", "person")
    p = card_payload(papa); photo = qr_png(p)           # arrived on WhatsApp: no capture record
    r = accept(photo, L, kr, me, code(me, papa), payload=p)
    assert not r["ok"] and "own cameras" in r["reason"] and kr.name(papa.pub) is None, r


def t4_unknown_camera_refused():
    me, cam, kr, L = setup(); papa = Identity("Papa", "person")
    stranger_cam = Identity("some phone camera", "device")       # signs 'captured', but not your device
    p = card_payload(papa); photo = qr_png(p); L.add(stranger_cam.sign(photo, "captured"))
    assert not accept(photo, L, kr, me, code(me, papa), payload=p)["ok"]


def t5_generated_image_refused():
    me, cam, kr, L = setup(); papa = Identity("Papa", "person"); model = Identity("image model", "model")
    kr.meet(model)                                                 # even a known model is not a camera
    p = card_payload(papa); photo = qr_png(p); L.add(model.sign(photo, "generated", model="x"))
    assert not accept(photo, L, kr, me, code(me, papa), payload=p)["ok"]


def t6_altered_card_refused():
    me, cam, kr, L = setup(); evil = Identity("Evil", "person")
    d = json.loads(card_payload(evil)); d["n"] = "Papa"                # relabel own key as Papa
    p = json.dumps(d); photo = snap(cam, L, p)
    r = accept(photo, L, kr, me, code(me, evil), payload=p)
    assert not r["ok"] and "signature invalid" in r["reason"], r


def t7_borrowed_pubkey_refused():
    me, cam, kr, L = setup(); papa = Identity("Papa", "person"); evil = Identity("Papa", "person")
    d = json.loads(card_payload(evil)); d["k"] = papa.pub               # offer Papa's key with evil's signature
    p = json.dumps(d); photo = snap(cam, L, p)
    r = accept(photo, L, kr, me, code(me, papa), payload=p)
    assert not r["ok"] and "signature invalid" in r["reason"], r


def t8_wrong_code_refused():
    me, cam, kr, L = setup(); papa = Identity("Papa", "person")
    p = card_payload(papa); photo = snap(cam, L, p)
    good = code(me, papa); bad = f"{(int(good) + 1) % 1_000_000:06d}"
    assert not accept(photo, L, kr, me, bad, payload=p)["ok"]
    assert code(me, papa) == confirm_code(papa.pub, me.pub) and len(good) == 6


def t9_name_clash_needs_replace():
    me, cam, kr, L = setup(); papa_old = Identity("Papa", "person"); papa_new = Identity("Papa", "person", alg="p256")
    kr.meet(papa_old)
    note = b"Beta, call me"; L.add(papa_old.sign(note, "testified"))
    p = card_payload(papa_new); photo = snap(cam, L, p)
    r = accept(photo, L, kr, me, code(me, papa_new), payload=p)
    assert not r["ok"] and "conflict" in r["reason"], r
    r = accept(photo, L, kr, me, code(me, papa_new), payload=p, replace=True)
    assert r["ok"] and kr.name(papa_new.pub) == "Papa" and kr.name(papa_old.pub) == "Papa (old key)", r
    assert describe(note, L, kr)["label"] == "sent by Papa (old key)"


def t10_not_a_card_and_own_key():
    me, cam, kr, L = setup()
    for junk in ("hello", "{}", json.dumps({"tk": 1, "n": "Papa"})):
        assert parse_card(junk)[0] is None
    p = card_payload(me); photo = snap(cam, L, p)
    assert not accept(photo, L, kr, me, code(me, me), payload=p)["ok"]


def t11_end_to_end_then_describe():
    """After the exchange, Papa's signed note shows 'sent by Papa'; an impostor key still doesn't."""
    me, cam, kr, L = setup(); papa = Identity("Papa", "person"); fake = Identity("Papa", "person")
    p = card_payload(papa); photo = snap(cam, L, p)
    assert accept(photo, L, kr, me, code(me, papa), payload=None if HAVE_CV else p)["ok"]
    a, b = b"Rs 5000 for the electrician", b"Rs 50000, urgent, new account"
    L.add(papa.sign(a, "testified")); L.add(fake.sign(b, "testified"))
    assert describe(a, L, kr)["label"] == "sent by Papa"
    assert describe(b, L, kr)["label"] == "sent by an unknown key claiming to be 'Papa'"


if __name__ == "__main__":
    tests = [v for k, v in sorted(globals().items(), key=lambda kv: int(kv[0][1:].split("_")[0]) if kv[0][:1] == "t" and kv[0][1:2].isdigit() else 0) if k[:1] == "t" and k[1:2].isdigit()]
    ok = 0
    for t in tests:
        try:
            note = t(); ok += 1; print("PASS", t.__name__, note or "")
        except Exception as e:
            print("FAIL", t.__name__, repr(e))
    print(f"{ok}/{len(tests)} pass")
    sys.exit(0 if ok == len(tests) else 1)
