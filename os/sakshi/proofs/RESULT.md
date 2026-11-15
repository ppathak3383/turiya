# The Kartr gate, machine-checked (15 Nov 2026)

`kartr_rule.h` is the rule that runs inside Sākṣī on seL4. CBMC (bounded model checking) explored **every**
input up to the stated sizes: every byte value of the action type (up to 12 bytes), target (8), amount (4) and
approval (65), every state (waking, dreaming, deep sleep), with and without a key, truncated or not.

| Property | Result |
|---|---|
| P1 an irreversible act is allowed only with the exact approval for that type, target and amount | proved |
| P1b every act allowed with an approval carried the exact approval | proved |
| P2 an act on a target planted by outside content is allowed only with the exact approval | proved |
| P3 deep sleep allows nothing; dreaming allows only thinking | proved |
| P4 truncated input is always held as bad input | proved |
| P5 allowed without approval only if reversible, in the mandate and not planted | proved |
| P6 no out-of-bounds access, no null dereference, unwinding complete | proved (588 checks, 3.5 s) |

Two things are left open on purpose, so the proof does not lean on them: the MAC is an unknown function of
the message (SHA-256 is not part of the argument), and the outside-content detector is an unknown predicate
(P2 and P5 hold whatever it answers). The header has two `#ifdef TURIYA_PROOF` lines for exactly these.

**The proof has teeth.** Two deliberately broken copies of the rule were caught:

| Broken copy | CBMC |
|---|---|
| a reversible act on a planted target allowed without approval | P2 and P5 fail |
| the dreaming rule removed | P3b fails |

**The detector itself** (target_from_outside, the 1f skeleton match) was too large for the model checker at
useful sizes (7.5 minutes of symbolic execution for a 4-byte target and 6-byte text, then out of objects). It
was fuzzed instead: 17.5 million random and mutated inputs in 3 minutes with AddressSanitizer and UBSan, checking
the same properties as the proof plus "a target written as it is in the mail is always detected": no failure.

Run: `cbmc gate_proof.cc -I.. --unwind 70 --unwinding-assertions --bounds-check --pointer-check`
(CBMC 5.95.1); fuzz: `clang++ -std=c++20 -fsanitize=fuzzer,address,undefined -I.. detector_fuzz.cc`.

Next: the same rule in Rust, with Verus proofs over all inputs (not just bounded sizes).
