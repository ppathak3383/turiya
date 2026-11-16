# The gate's decision in Rust, proved with Verus for all inputs (15 Nov 2026)

`gate.rs` is the decision logic of `decide()` in `../sakshi/kartr_rule.h`, step by step, in Rust. What a string
is (irreversible type, in the mandate, well formed, planted by outside content) arrives classified in `Facts`;
the MAC arrives as two 32-byte arrays. Verus (commit 530783b, Rust 1.99.1, Z3 4.16.0) proves, with **no size
bounds**:

| Property | |
|---|---|
| P1 an irreversible act is allowed only with the exact approval | proved |
| P1b every act allowed with an approval carried the exact approval | proved |
| P2 an act on a planted target is allowed only with the exact approval | proved |
| P3 deep sleep allows nothing; dreaming allows only thinking | proved |
| P4 malformed or truncated input is held as bad input | proved |
| P5 allowed without approval only if reversible, in the mandate, well formed and not planted | proved |
| `ct_eq` is exact, and looks at all 32 bytes every time (constant time) | proved |

`verification results:: 5 verified, 0 errors` in under a second.

Three broken versions are rejected: a planted reversible act allowed (postcondition fails), the dreaming rule
removed (postcondition fails), the compare keeping only the last byte (loop invariant fails).

**Same verdicts as the C++ rule on seL4.** `facts_cli.cc` enumerates 1,296 inputs to the C++ rule (6 types,
3 targets, with and without outside content, 3 approvals, with and without a key, truncated or not, 3 states)
and prints the classified facts with the C++ verdict; `gate_diff.rs` (gate.rs plus a reader) decides the same
facts with the proved Rust code: **1,296 the same, 0 differ**.

How the pieces fit:

| Layer | Checked by |
|---|---|
| the decision (this file) | Verus, all inputs |
| the C++ rule that runs in Sakshi | CBMC, all inputs up to size bounds (`../sakshi/proofs`) |
| C++ and Rust agree | 1,296-case differential run |
| the outside-content detector | fuzzed, 17.5 million inputs, ASan and UBSan |

Next: Sakshi calls this Rust decision directly (no_std), so the code on seL4 is the proved code.

Run: `verus gate.rs`; differential: `g++ -std=c++20 -I. facts_cli.cc -o facts_cli && verus gate_diff.rs --compile && ./facts_cli | ./gate_diff`.

## Step 5d (16 Nov 2026): the owner's known targets

`Facts` has a new fact `unknown_target` (a target neither in the owner's list, byte for byte, nor the sender's
whole address) and a new verdict `HeldUnknownTarget` (last, so the numbers still match the C++ enum). New
postcondition **P7**: an act on an unknown target is allowed only with the exact approval; P5 now also needs
`!unknown_target`. Sakshi passes the fact as flag bit 9.

**Re-checked 16 Nov, 07:00 IST (Claude, before publishing with the owner's yes):** `verus gate.rs`: **5 verified, 0
errors** (19.6 s), so P1-P7 hold for every input. A copy that ignores the list (`if !f.planted`) fails P7 and P5.
The differential through Verus (`verus gate_diff.rs --compile`, 4 verified): **5,832 the same, 0 differ**. Sakshi
with this decision booted on seL4 in QEMU: the two judges agreed 15 of 15 at boot and 30 of 30 after `gate`
(`os/runs/2026-11-16-5d/`). The night run had no Verus (its session could not download it) and ran the
differential on the stripped executable code instead.
