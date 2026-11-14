# Step 1e: the gate holds targets planted by outside content (14 Nov 2026)

Phase 0's third rule, now in the Genode gate (`kartr_rule.h`, `target_from_outside`).
- New input: an `outside` ROM, the text the agent read (mail, page, message). It is handed over by the component that fetched it, not by the agent. Line 1 is the source line ("From: …"); the rest is the body.
- Any target that looks like an address, account or link (has `@`, `.`, `/` or 6+ digits) and appears in the body is planted (case-insensitive match). The action is held, even a reversible draft, unless it carries the owner's exact approval: `held_target_from_outside_content`.
- Replying to the sender (address only on the source line) is still allowed. Plain words such as "inbox" never count.

Results
- Host: **36/36** (24 earlier + 12 new) with `g++ -std=c++20 -Wall -Wextra -Werror`.
- seL4 13.0.0 in QEMU: **15/15 as expected, "kartr_gate: 4 allowed, 11 held"**. a13 draft to the sender was allowed; a14 draft to the planted `kyc-desk@okaxis` was held; a15 pay to the planted `9876543210@ybl` was held.

Not yet: republishing kartr_gate in the depot with this rule (needs the owner's yes). The USB stick image of 14 Nov carries 2026-11-13b, without this rule.
