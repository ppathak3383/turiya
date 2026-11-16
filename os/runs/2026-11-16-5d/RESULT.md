# Step 5d: the owner's known targets in Sakshi's own gate (16 Nov 2026)

Written by the 03:10 IST night run (commit 2ed3209); re-checked and published at 07:00 IST with the owner's yes.

A reversible act may name no target, a target in the owner's list (byte for byte) or the whole address on the
fetched message's `From:` line. Anything else is held as `held_target_not_known` until the owner approves that exact
act. Look-alike folding is used only to hold a target, never to allow one. The list in `sakshi.cc` (`OWNER_KNOWN`)
stands in for the owner's own signed config (step 5e).

| Check | Night run | Re-check (07:00) |
|---|---|---|
| Patch applies; files match its hashes | made it | 8 of 8 match |
| Host tests, `-Wall -Wextra -Werror -Weffc++` | 79/79 | 79/79 |
| CBMC, P1-P7 | 669/669, 4.1 s | 669/669, 5.1 s |
| Verus, `gate.rs` | not run (no Verus in that session) | **5 verified, 0 errors**; the list-ignoring mutant fails P5 and P7 |
| C++ rule = Verus-proved Rust decision | 5,832 same (stripped code, rustc) | 5,832 same (compiled through Verus) |
| seL4 13.0 in QEMU (Usas + Sakshi + organs) | not run | two judges agreed 15/15 at boot, 30/30 after `gate`; "4 allowed, 11 held" as before (`serial_qemu.txt`) |
