# SPDX-License-Identifier: AGPL-3.0-or-later
"""Turiya - reveal hidden text before anyone judges it (13 Nov 2026).

Attackers hide instructions in base64, ROT13 or spaced-out letters so that filters see noise. Decoding is
deterministic and cheap, so the system does it itself and shows the result beside the raw text. Nothing here
decides anything: it only makes the hidden visible to the rule detector, the judge and the owner.
"""
import base64, binascii, codecs, re

B64 = re.compile(r"[A-Za-z0-9+/]{24,}={0,2}")
SPACED = re.compile(r"(?<!\S)(?:\S[ ]{1,2}){6,}\S(?!\S)")   # 'a s s i s t a n t' style, 7+ single characters
ROT_HINT = re.compile(r"\brot ?13\b", re.I)


def _printable(s: str) -> bool:
    return bool(s) and sum(c.isprintable() or c in "\n\t" for c in s) / len(s) > 0.95 and sum(c.isalpha() for c in s) / len(s) > 0.5


def reveal(text: str):
    """Return a list of (kind, decoded_text) found in text."""
    found = []
    for m in B64.finditer(text):
        blob = m.group(0)
        try:
            s = base64.b64decode(blob + "=" * (-len(blob) % 4), validate=True).decode("utf-8")
        except (binascii.Error, UnicodeDecodeError, ValueError):
            continue
        if _printable(s):
            found.append(("base64", s))
    for m in SPACED.finditer(text):
        s = re.sub(r"[ ]{2,}", "\x00", m.group(0)).replace(" ", "").replace("\x00", " ")
        found.append(("spaced letters", s))
    if ROT_HINT.search(text):
        found.append(("rot13", codecs.decode(text, "rot13")))
    return found


def with_revealed(text: str) -> str:
    """The text plus a visible line for each hidden layer, for the detector and the judge."""
    extra = [f"[decoded {k}: {s}]" for k, s in reveal(text)]
    return text if not extra else text + "\n" + "\n".join(extra)
