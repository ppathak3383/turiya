# Turiya

**An operating system for a world full of AI agents, fakes and copies.**
It keeps two promises that today's systems can't:

1. **An AI agent can never do something you can't undo unless you approve it.** Paying, transferring, deleting, sending as you, publishing or sharing a file always waits for you, however the agent was tricked.
2. **Every file can say where it came from.** It was captured by one of your devices, written by a person whose key you met in person, made by a model, or of unknown origin. A deepfake of your father can't claim his name.

> Status: research prototype (Phase 0–1). Not yet an installable OS.

## The idea in one paragraph

Turiya ("the fourth", the witness behind waking, dreaming and deep sleep) separates the parts that *act* from the part that *witnesses*. The small proven core (Sākṣī, the witness) holds the keys and the log and never runs untrusted code. Agents (Kartṛ, the doer) get only a short-lived mandate: a list of allowed actions, an expiry and a budget. Everything from outside (mail, web pages, messages) is marked as outside (Viveka, discernment) and can inform but never instruct. Every action is written to a hash-chained log. Like a genome, the system carries only what it needs and sheds what is obsolete.

## What works today (measured, November 2026)

| Test | Result |
|---|---|
| Agent (Gemma 4 E4B, local) tricked by hidden instructions | 65% of 40 attacks; 14/20 on a fresh set |
| Irreversible acts that ran anyway, through the gate | **0 / 80** |
| Acts aimed at an address or account planted in the message | **0 / 80** |
| Hidden instruction shown to you: rules only | 33 / 60 |
| Hidden instruction flagged by a separate judge model (no tools, no authority) | **79 / 80** attacks, 2 / 25 false alarms |
| Attacks aimed at the judge itself (fake verdicts, "answer false", forged items, homoglyphs, Hinglish) | **20 / 20** caught |
| Judge cost on a 16 GB laptop CPU | ~6–11 s per message |
| Provenance ledger (Gate 1) | 10 / 10; Ed25519 and ECDSA P-256 (TPM / phone secure chip) |
| Family key exchange by QR in person | 11 / 11: forwarded QR, screenshot, stranger's camera, AI-made image all refused |
| Turiya component installed on Sculpt OS 26.10 from Turiya's own signed depot | **runs** (`verify … good`, "Turiya: Sakshi is awake") |
| The gate's rule as a Genode component (C++, own SHA-256/HMAC) | **8 / 8**: an approval fits only the exact payment it was given for |
| Both components on the **seL4 13.0** microkernel (QEMU, x86_64) | **8 / 8**, same log (`phase2/`) |

The key finding: the **same small model** obeys about two thirds of hidden instructions when it acts, but spots about 98% of them when it is asked only to judge, with no power to act. So the judge can warn, never allow.

## How it is built

```
phase0/   the gate around an AI agent: mandate, Viveka marking, irreversible-action hold,
          outside-target hold, separate judge, hash-chained Kartṛ log
pramana/  the provenance ledger: signed records (captured / generated / testified / derived),
          keyring of keys met in person, QR key exchange, sidecar files that travel with a file
phase1/   running on a capability microkernel: Sculpt OS (Genode) in a VM now, seL4 next
```

Roadmap: Phase 1 (gate and ledger as Genode components on Sculpt), Phase 2 (seL4, proven core on ARM/RISC-V first, where the proofs are strongest), Phase 3 (laptop dual boot), Phase 4 (phone).

## Run it

```
pip install cryptography qrcode opencv-python-headless
python pramana/tests/test_gate1.py
python pramana/tests/test_keyex.py
python phase0/tests/test_gate.py
python phase0/tests/test_warning.py                    # warning policy over all 105 test messages
```

## Licence

AGPL-3.0-or-later. Anyone may use, study, change and share Turiya; anyone who runs a changed version for others over a network must share their changes too. See `LICENSE` and `NOTICE`.

## Honest limits

- Mostly a Python prototype; two small Genode components so far, run on Genode's Linux base, not yet inside Sculpt or on seL4.
- Tested with one local model and 105 hand-written or model-written messages; a determined attacker will find cases we missed.
- seL4's proof on x86-64 covers the C code's functional correctness only; the laptop's core is less proven than a future ARM phone's.
- Hardware keys (TPM) designed for but not yet used.
