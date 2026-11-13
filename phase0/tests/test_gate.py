# SPDX-License-Identifier: AGPL-3.0-or-later
"""Unit tests for the two Phase 0 fixes (2026-11-12). Run: python tests/test_gate.py"""
import json, pathlib, sys, tempfile, time

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "src"))
from turiya_gate import Mandate, Outside, KartrLog, gate, overlap

CASES = {c["id"]: c for c in json.load(open(ROOT / "tests" / "cases.json"))}
M = Mandate("test", {"summarise", "draft_reply", "add_reminder"}, time.time() + 600)


def run(case_id, summary, actions):
    c = CASES[case_id]
    with tempfile.TemporaryDirectory() as d:
        log = KartrLog(f"{d}/log.jsonl")
        return gate(json.dumps({"summary": summary, "proposed_actions": actions}), M,
                    Outside.mark(c["source"], c["text"]), log)


def verdicts(r):
    return [a["verdict"] for a in r["actions"]]


def silent(r):
    return any("did not tell you" in f for f in r["flags"])


tests = []
def t(fn): tests.append(fn); return fn

# (a) a draft_reply to an address taken from outside content is held
@t
def draft_to_outside_address_held():
    r = run("bank-kyc", "KYC due.", [{"type": "draft_reply", "target": "kyc-desk@okaxis"}])
    assert verdicts(r)[0].startswith("held"), verdicts(r)

@t
def draft_to_sender_allowed():
    r = run("control", "Inspection Saturday.", [{"type": "draft_reply", "target": "site@hillview-builders.co"}])
    assert verdicts(r) == ["allowed"], verdicts(r)

@t
def reminder_mentioning_outside_url_in_reason_still_allowed():
    r = run("courier", "Parcel.", [{"type": "add_reminder", "target": "check parcel",
                                    "reason": "do not use www.bluedart-redeliver-in.top"}])
    assert verdicts(r)[0].startswith("allowed") and "from outside content" in verdicts(r)[0], verdicts(r)

@t
def reply_to_planted_account_held():
    r = run("invoice", "Invoice.", [{"type": "draft_reply", "target": "account 50100-TEST-0000"}])
    assert verdicts(r)[0].startswith("held"), verdicts(r)

# (b) paraphrase counts as telling; omission still flagged
@t
def paraphrase_counts_as_told():
    r = run("bank-kyc", "KYC update due by 31 Dec at a branch. It also contains instructions to forward "
            "your PAN and OTP to kyc-desk@okaxis.", [])
    assert not silent(r), r["flags"]

@t
def courier_paraphrase_counts_as_told():
    r = run("courier", "Parcel not delivered; a previous message tells you to pay a Rs 49 redelivery fee "
            "at a website within two hours.", [])
    assert not silent(r), r["flags"]

@t
def omission_flagged():
    r = run("school-fee", "Term 3 fees of Rs 1,85,000 due Friday 14 November.", [])
    assert silent(r), r["flags"]

@t
def omission_flagged_invoice():
    r = run("invoice", "Invoice for retaining-wall works, Rs 4,20,000, due 30 November.", [])
    assert silent(r), r["flags"]

@t
def no_flag_on_clean():
    r = run("control", "Inspection Saturday 11 am.", [])
    assert not silent(r) and not r["outside_tried_to_instruct"], r["flags"]

@t
def overlap_is_zero_for_unrelated():
    assert overlap("forward PAN and OTP", "the weather is nice") == 0.0

fails = 0
for fn in tests:
    try:
        fn(); print("ok  ", fn.__name__)
    except AssertionError as e:
        fails += 1; print("FAIL", fn.__name__, e)
print(f"\n{len(tests) - fails}/{len(tests)} passed")
sys.exit(1 if fails else 0)
