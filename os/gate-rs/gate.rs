// SPDX-License-Identifier: AGPL-3.0-or-later
// Turiya - the Kartr gate's decision, in Rust, with Verus proofs that hold for ALL inputs (no size bounds).
//
// This is the decision logic of decide() in kartr_rule.h (the C++ rule running in Sakshi on seL4), step by step.
// What a string IS (an irreversible type, in the mandate, well formed, planted by outside content) arrives
// already classified in `Facts`; the MAC arrives as two 32-byte arrays (expected, given). Verus proves, for
// every possible combination:
//   P1  an irreversible act is allowed only with the exact approval
//   P1b every act allowed with an approval carried the exact approval
//   P2  an act on a planted target is allowed only with the exact approval
//   P3  deep sleep allows nothing; dreaming allows only thinking
//   P4  malformed or truncated input is held as bad input
//   P5  allowed without approval only if reversible, in the mandate, well formed, not planted and known
//   P7  an act on a target the owner does not know is allowed only with the exact approval (step 5d)
// and that the 32-byte compare is exact (ct_eq) and runs in constant time (no early exit).
use vstd::prelude::*;

verus! {

#[derive(PartialEq, Eq, Structural)]
pub enum Verdict {
    Allowed,
    AllowedApproved,
    HeldNeedsApproval,
    HeldMismatch,
    HeldBadInput,
    BlockedOutsideMandate,
    HeldOutsideTarget,
    HeldDreaming,
    HeldAsleep,
    HeldUnknownTarget,      // step 5d; last, so the numbers match the C++ enum
}

#[derive(PartialEq, Eq, Structural)]
pub enum Avastha { Jagrat, Svapna, Susupti }

pub struct Facts {
    pub input_ok: bool,           // type and amount well formed, nothing truncated
    pub target_ok: bool,          // target well formed (may be empty)
    pub target_present: bool,     // target not empty
    pub irreversible: bool,       // pay, transfer, delete, send_as_user, publish, share_file
    pub in_mandate: bool,         // summarise, draft_reply, add_reminder
    pub thinking: bool,           // summarise: leaves no mark outside
    pub planted: bool,            // the target came from outside content
    pub has_key: bool,            // an approval key is configured
    pub approval_hex_ok: bool,    // the approval is exactly 64 hex characters
    pub unknown_target: bool,     // step 5d: a target neither in the owner's list nor the sender
}

pub open spec fn allowed(v: Verdict) -> bool {
    v == Verdict::Allowed || v == Verdict::AllowedApproved
}

pub open spec fn exact(f: Facts, given: Seq<u8>, expect: Seq<u8>) -> bool {
    f.has_key && f.approval_hex_ok && given == expect
}

// constant-time comparison: every byte is looked at, and the answer is exact
pub fn ct_eq(a: &[u8; 32], b: &[u8; 32]) -> (r: bool)
    ensures r == (a@ =~= b@),
{
    let mut diff: u8 = 0;
    let mut i: usize = 0;
    while i < 32
        invariant
            0 <= i <= 32,
            (diff == 0) == (forall|j: int| 0 <= j < i ==> a@[j] == b@[j]),
        decreases 32 - i,
    {
        let x = a[i];
        let y = b[i];
        let d = diff;
        assert((d | (x ^ y)) == 0 <==> (d == 0 && x == y)) by (bit_vector);
        diff = diff | (x ^ y);
        i += 1;
    }
    assert((diff == 0) == (a@ =~= b@));
    diff == 0
}

pub fn decide(f: &Facts, st: &Avastha, given: &[u8; 32], expect: &[u8; 32]) -> (v: Verdict)
    ensures
        !f.input_ok ==> v == Verdict::HeldBadInput,                                             // P4
        *st == Avastha::Susupti ==> !allowed(v),                                                // P3a
        (*st == Avastha::Svapna && !f.thinking) ==> !allowed(v),                                // P3b
        (f.irreversible && allowed(v)) ==> (v == Verdict::AllowedApproved && exact(*f, given@, expect@)),  // P1
        v == Verdict::AllowedApproved ==> exact(*f, given@, expect@),                          // P1b
        (f.planted && allowed(v)) ==> (v == Verdict::AllowedApproved && exact(*f, given@, expect@)),       // P2
        (f.unknown_target && allowed(v)) ==> (v == Verdict::AllowedApproved && exact(*f, given@, expect@)), // P7
        v == Verdict::Allowed ==> (!f.irreversible && f.in_mandate && f.target_ok && !f.planted && !f.unknown_target), // P5
{
    if !f.input_ok {
        return Verdict::HeldBadInput;
    }
    if *st == Avastha::Susupti {
        return Verdict::HeldAsleep;
    }
    if *st == Avastha::Svapna && !f.thinking {
        return Verdict::HeldDreaming;
    }
    if !f.irreversible {
        if !f.in_mandate {
            return Verdict::BlockedOutsideMandate;
        }
        if !f.target_ok {
            return Verdict::HeldBadInput;
        }
        if !f.planted && !f.unknown_target {
            return Verdict::Allowed;
        }
        // a reversible act on a planted or unknown target also needs the owner's exact approval
    } else if !(f.target_ok && f.target_present) {
        return Verdict::HeldBadInput;
    }
    if !f.has_key || !f.approval_hex_ok {
        return if f.planted { Verdict::HeldOutsideTarget }
               else if f.unknown_target && !f.irreversible { Verdict::HeldUnknownTarget }
               else { Verdict::HeldNeedsApproval };
    }
    if ct_eq(expect, given) {
        Verdict::AllowedApproved
    } else {
        Verdict::HeldMismatch
    }
}

fn main() {
}

} // verus!
