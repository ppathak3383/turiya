# SPDX-License-Identifier: AGPL-3.0-or-later
"""Turiya - Pramana ledger (prototype). Every object carries a signed record of how it is known.

  pratyaksa (perception)  -> kind "captured":  a sensor signed the bytes at capture (camera, microphone)
  anumana   (inference)   -> kind "generated": a model made it; the record names the model
  sabda     (testimony)   -> kind "testified":  a person signed it with their own key
  + "derived": an edit or re-encode, signed by whoever made it, pointing at its parent(s)

Records are keyed by the content's SHA-256, so a byte-identical copy keeps its lineage for free,
and any change to the bytes leaves the new bytes with no lineage until someone signs for them.
Trust comes only from keys met in person (the keyring). A name inside a record is a claim, never proof.
"""
import hashlib, json, time
from dataclasses import dataclass, field
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization, hashes
from cryptography.exceptions import InvalidSignature

KINDS = {"captured", "generated", "testified", "derived"}


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


# Two signature schemes (2026-11-12): Ed25519 for software keys, ECDSA P-256 for keys held in hardware.
# Windows TPMs (Platform Crypto Provider) offer RSA and ECDSA P-256/384/521 but not Ed25519, and phone
# secure elements (StrongBox) are not expected to offer Ed25519 either, so P-256 is the common hardware curve.
ALGS = {"ed25519", "p256"}


def _pub_hex(pub) -> str:
    if isinstance(pub, ec.EllipticCurvePublicKey):
        return "p256:" + pub.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.CompressedPoint).hex()
    return pub.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw).hex()


def _load_pub(who: str):
    if who.startswith("p256:"):
        return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), bytes.fromhex(who[5:]))
    return Ed25519PublicKey.from_public_bytes(bytes.fromhex(who))


def _canon(body: dict) -> bytes:
    return json.dumps(body, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()


@dataclass
class Identity:
    """A key holder: a person, a device's sensor, or a model/tool. The private key never leaves it."""
    name: str
    role: str                     # person | device | model
    alg: str = "ed25519"          # "p256" for a key that lives in a TPM / phone secure element
    _key: object = field(default=None, repr=False)

    def __post_init__(self):
        assert self.alg in ALGS
        if self._key is None:
            self._key = ec.generate_private_key(ec.SECP256R1()) if self.alg == "p256" else Ed25519PrivateKey.generate()

    @property
    def pub(self) -> str:
        return _pub_hex(self._key.public_key())

    def sign(self, content: bytes, kind: str, parents=(), **meta) -> dict:
        assert kind in KINDS
        body = {"v": 1, "sha256": sha(content), "kind": kind, "who": self.pub, "claims_name": self.name,
                "role": self.role, "parents": list(parents), "meta": meta, "time": round(time.time(), 3)}
        if self.alg == "p256":
            sig = self._key.sign(_canon(body), ec.ECDSA(hashes.SHA256()))
        else:
            sig = self._key.sign(_canon(body))
        return {**body, "sig": sig.hex()}


def verify(record: dict) -> bool:
    body = {k: v for k, v in record.items() if k != "sig"}
    try:
        pub = _load_pub(record["who"])
        if isinstance(pub, ec.EllipticCurvePublicKey):
            pub.verify(bytes.fromhex(record["sig"]), _canon(body), ec.ECDSA(hashes.SHA256()))
        else:
            pub.verify(bytes.fromhex(record["sig"]), _canon(body))
        return record.get("kind") in KINDS
    except (InvalidSignature, ValueError, KeyError):
        return False


class Keyring:
    """Keys you have met in person (or a device you own). public key -> (name, role)."""
    def __init__(self):
        self.known = {}

    def meet(self, ident: Identity):
        self.known[ident.pub] = (ident.name, ident.role)

    def name(self, pub):
        return self.known.get(pub, (None, None))[0]


class Ledger:
    """Content-addressed store of records: sha256 -> [records]. Append-only."""
    def __init__(self):
        self.records = {}

    def add(self, record: dict):
        self.records.setdefault(record["sha256"], []).append(record)

    def for_content(self, content: bytes):
        return self.records.get(sha(content), [])


def describe(content: bytes, ledger: Ledger, keyring: Keyring, _depth=0) -> dict:
    """What the human sees. Returns {'label': str, 'kind': str, 'flags': [...]}."""
    flags, valid = [], []
    for r in ledger.for_content(content):
        if r["sha256"] != sha(content) or not verify(r):
            flags.append("a record for this content failed verification (tampered or forged) and was ignored")
        else:
            valid.append(r)
    if not valid:
        return {"label": "origin unknown", "kind": "unknown", "flags": flags}

    def who(r):
        n = keyring.name(r["who"])
        return n if n else f"an unknown key claiming to be '{r['claims_name']}'"

    kinds = {r["kind"] for r in valid}
    # Weakest-link rule: if any valid record says generated, the content is generated, whatever else is claimed.
    if "generated" in kinds and ("captured" in kinds or "testified" in kinds):
        flags.append("conflicting records: generated and captured/testified on the same bytes; shown as generated")
    trusted = [r for r in valid if keyring.name(r["who"])]
    order = ["generated", "captured", "testified", "derived"]
    r = sorted(valid, key=lambda r: (order.index(r["kind"]), r not in trusted))[0]
    if r["kind"] == "generated":
        label = f"generated by {r['meta'].get('model', 'a model')}, signed by {who(r)}"
    elif r["kind"] == "captured":
        label = f"captured by {who(r)}"
    elif r["kind"] == "testified":
        label = f"sent by {who(r)}"
    else:
        parent = [p for p in r["parents"]]
        plabel = "unknown parent"
        if parent and _depth < 8:
            for blob_sha in parent:
                # parents are referenced by hash; look up their records directly
                prs = [x for x in ledger.records.get(blob_sha, []) if verify(x)]
                if prs:
                    pr = sorted(prs, key=lambda x: order.index(x["kind"]))[0]
                    plabel = describe_record(pr, ledger, keyring, _depth + 1)
        label = f"edited by {who(r)} from: {plabel}"
    if not keyring.name(r["who"]):
        flags.append("signed by a key you have not met; the name inside it is only a claim")
    return {"label": label, "kind": r["kind"], "flags": flags}


def describe_record(r, ledger, keyring, depth):
    n = keyring.name(r["who"]) or f"an unknown key claiming to be '{r['claims_name']}'"
    if r["kind"] == "generated":
        return f"generated by {r['meta'].get('model', 'a model')}"
    if r["kind"] == "captured":
        return f"captured by {n}"
    if r["kind"] == "testified":
        return f"sent by {n}"
    return f"edited by {n}"


# --- Persistence (2026-11-12 evening): append-only store, keyring file, sidecar export -----------------

def save_record(store_path, record: dict):
    """Append one record to the store (JSON Lines). Never rewrites: an old record can't be edited away."""
    with open(store_path, "a", encoding="utf-8") as f:
        f.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")


def load_ledger(store_path) -> Ledger:
    L = Ledger()
    try:
        with open(store_path, encoding="utf-8") as f:
            for line in f:
                if line.strip():
                    L.add(json.loads(line))
    except FileNotFoundError:
        pass
    return L


def save_keyring(kr: Keyring, path):
    json.dump({pub: {"name": n, "role": r} for pub, (n, r) in kr.known.items()}, open(path, "w", encoding="utf-8"),
              indent=1, ensure_ascii=False)


def load_keyring(path) -> Keyring:
    kr = Keyring()
    for pub, v in json.load(open(path, encoding="utf-8")).items():
        kr.known[pub] = (v["name"], v["role"])
    return kr


def export_sidecar(content: bytes, ledger: Ledger, path):
    """Write <file>.pramana.json: every record for these bytes and their ancestors, so lineage travels with
    the file to a machine that has never seen this ledger. Receivers verify; they never trust the sidecar."""
    out, todo, seen = [], [sha(content)], set()
    while todo:
        h = todo.pop()
        if h in seen:
            continue
        seen.add(h)
        for r in ledger.records.get(h, []):
            out.append(r)
            todo.extend(r.get("parents", []))
    json.dump(out, open(path, "w", encoding="utf-8"), indent=1, ensure_ascii=False)
    return len(out)


def import_sidecar(path, ledger: Ledger) -> int:
    n = 0
    for r in json.load(open(path, encoding="utf-8")):
        if verify(r):              # forged or tampered records are dropped on arrival
            ledger.add(r); n += 1
    return n


if __name__ == "__main__":
    # CLI: python pramana.py describe <file> <store.jsonl> <keyring.json>
    import sys
    if len(sys.argv) == 5 and sys.argv[1] == "describe":
        d = describe(open(sys.argv[2], "rb").read(), load_ledger(sys.argv[3]), load_keyring(sys.argv[4]))
        print(d["label"]); [print("  !", f) for f in d["flags"]]
    else:
        print(__doc__)
