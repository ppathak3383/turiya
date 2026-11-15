# Two judges on seL4: the C++ rule and the Verus-proved decision (15 Nov 2026, emulator)

Every action Sakshi decides now goes to two judges, and they must agree:

| Judge | Checked by | Runs as |
|---|---|---|
| the C++ rule, `kartr_rule.h` | CBMC (7 properties, all inputs up to size bounds), 56 host tests | C++ inside Sakshi |
| the decision in `gate-rs/gate.rs` | Verus (the same properties, all inputs, no bounds) | Rust `no_std` library linked into Sakshi |

`strip_verus.py` removes only the proof annotations from `gate.rs` (requires/ensures, invariants, spec functions,
proof asserts) and refuses to run if any Verus syntax is left; every executable line is kept as it is. The C++
side classifies the action (well formed, irreversible, in the mandate, planted, approval hex) and computes the
expected MAC; both judges decide from that. If they ever disagree the action is held, whatever either said.

| Test | Result |
|---|---|
| the 15 test actions at boot | "4 allowed, 11 held; two judges agreed 15 of 15" |
| `state svapna`, `pay ravi@ybl 100` | held_while_dreaming (both judges) |
| `state jagrat`, `pay ravi@ybl 100`, `approve` | held_needs_approval, then allowed_with_approval (both judges) |

The proved decision adds 4.4 KB to Sakshi.
