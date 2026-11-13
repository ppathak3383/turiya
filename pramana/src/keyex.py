# SPDX-License-Identifier: AGPL-3.0-or-later
"""Turiya - family key exchange (prototype, 2026-11-13).

The keyring is the root of all trust in Pramana: a name is shown only for keys met in person. This module is
the ONLY way a person's key enters the keyring, and it enforces "in person" mechanically:

  1. Each device shows a key card as a QR code: name, role, public key, signed by that same key
     (a Pramana "testified" record over the card bytes), so whoever shows it holds the private key.
  2. The other device's OWN camera photographs the QR. The photo must carry a valid "captured" record from a
     camera key already in the keyring as one of your devices. A QR forwarded on WhatsApp, a screenshot, a file
     from email or an image a model made has no such record, so it is refused.
  3. Both screens show a 6-digit code computed from both public keys; the two people read it aloud and the
     receiver types it. A QR swapped or replayed from elsewhere gives a different code.
  4. A name already in the keyring is never silently re-pointed at a new key ("Papa got a new phone" is a
     conflict that needs replace=True, i.e. a deliberate choice by the owner).

Any other route (an email, a sidecar, a contact card, a record's claims_name) never adds a key.
"""
import hashlib, io, json
import qrcode
from pramana import Identity, Keyring, Ledger, verify, sha, _canon

CARD_V = 1


def _card(name, role, pub):
    return {"turiya-key": CARD_V, "name": name, "role": role, "pub": pub}


def _record(card, t, sig):
    """Rebuild the full self-signature record from the compact QR fields."""
    return {"v": 1, "sha256": sha(_canon(card)), "kind": "testified", "who": card["pub"], "claims_name": card["name"],
            "role": card["role"], "parents": [], "meta": {"purpose": "key-card"}, "time": t, "sig": sig}


def card_payload(ident: Identity) -> str:
    """What the QR encodes: name, role, public key, signing time and a self-signature over the card.
    Kept compact (~300 chars, QR version ~11) so a phone camera reads it at arm's length."""
    rec = ident.sign(_canon(_card(ident.name, ident.role, ident.pub)), "testified", purpose="key-card")
    return json.dumps({"tk": CARD_V, "n": ident.name, "r": ident.role, "k": ident.pub, "t": rec["time"],
                       "s": rec["sig"]}, separators=(",", ":"), ensure_ascii=False)


def parse_card(payload: str):
    """Returns (card, None) if well-formed and self-signed by the key it names, else (None, reason).
    The signature covers name, role and key, so relabelling a card or swapping in someone else's key fails."""
    try:
        d = json.loads(payload)
        if d["tk"] != CARD_V or not all(isinstance(d[k], str) for k in ("n", "r", "k", "s")):
            raise ValueError
        card = _card(d["n"], d["r"], d["k"]); rec = _record(card, d["t"], d["s"])
    except (ValueError, KeyError, TypeError):
        return None, "not a Turiya key card"
    if not verify(rec):
        return None, "card signature invalid (altered, or signed by a different key than the one it offers)"
    return card, None


def qr_png(payload: str) -> bytes:
    img = qrcode.make(payload, error_correction=qrcode.constants.ERROR_CORRECT_M)
    buf = io.BytesIO(); img.save(buf, format="PNG")
    return buf.getvalue()


def decode_qr(png: bytes):
    """Decode a QR photo. Uses OpenCV if present (the camera stack's job on a real device)."""
    import numpy as np, cv2
    img = cv2.imdecode(np.frombuffer(png, np.uint8), cv2.IMREAD_GRAYSCALE)
    # The ArUco-based detector read 60/60 test cards; the classic one only 49/60 (13 Nov), so try it first.
    for det in (cv2.QRCodeDetectorAruco(), cv2.QRCodeDetector()):
        text = det.detectAndDecode(img)[0]
        if text:
            return text
    return None


def confirm_code(pub_a: str, pub_b: str) -> str:
    """6 digits both screens show; order-independent."""
    h = hashlib.sha256("|".join(sorted([pub_a, pub_b])).encode()).digest()
    return f"{int.from_bytes(h[:4], 'big') % 1_000_000:06d}"


def own_camera(photo: bytes, ledger: Ledger, keyring: Keyring):
    """Name of your own camera that captured these bytes, or None."""
    for r in ledger.for_content(photo):
        if r["kind"] == "captured" and verify(r) and r["sha256"] == sha(photo):
            name, role = keyring.known.get(r["who"], (None, None))
            if role == "device":
                return name
    return None


def accept(photo: bytes, ledger: Ledger, keyring: Keyring, me: Identity, spoken_code: str,
           replace=False, payload=None) -> dict:
    """Try to add the key in a photographed QR to the keyring. Returns {'ok': bool, 'reason': str, ...}.
    `payload` lets tests skip the image decoder; it must equal what the photo decodes to when a decoder exists."""
    cam = own_camera(photo, ledger, keyring)
    if not cam:
        return {"ok": False, "reason": "refused: the QR photo was not taken by one of your own cameras"}
    text = decode_qr(photo) if payload is None else payload
    if not text:
        return {"ok": False, "reason": "refused: no QR code found in the photo"}
    card, why = parse_card(text)
    if not card:
        return {"ok": False, "reason": f"refused: {why}"}
    if card["pub"] == me.pub:
        return {"ok": False, "reason": "refused: that is your own key"}
    if spoken_code != confirm_code(me.pub, card["pub"]):
        return {"ok": False, "reason": "refused: the 6-digit code does not match the other screen"}
    if card["pub"] in keyring.known:
        return {"ok": True, "reason": "already known", "name": keyring.name(card["pub"])}
    clash = [p for p, (n, _) in keyring.known.items() if n == card["name"]]
    if clash and not replace:
        return {"ok": False, "reason": f"conflict: '{card['name']}' is already a different key; "
                                       "confirm deliberately with replace=True"}
    for p in clash:   # keep the old key, renamed, so things it signed still show who signed them
        keyring.known[p] = (f"{card['name']} (old key)", keyring.known[p][1])
    keyring.known[card["pub"]] = (card["name"], card["role"])
    return {"ok": True, "reason": f"added via {cam}", "name": card["name"], "replaced": len(clash)}
