# Step 5b/5c: the owner's known targets, in the gate on seL4 (15 Nov 2026)

Rule (reversible actions): allowed only with no target, a target in the owner's `<known_target name=".."/>` list (exact match), or the `From:` sender of the fetched message (whole address, any letter case), and only if that target is not also in the fetched body. Anything else is held as `held_target_from_outside_content` and runs only with the owner's exact approval. The component always passes the list, so a config with no `known_target` denies every named target except the sender (a warning is logged). Over-long or `|` names are skipped and more than 16 are ignored (both fail closed). Irreversible actions are unchanged.

Results
- Host (15 Nov 03:10 night run): 56/56 (36 from 1e + 20 new), `-Wall -Wextra -Werror -Weffc++`.
- **seL4 13.0.0 in QEMU (15 Nov 22:40 night run): 20/20 as expected, "kartr_gate: 6 allowed, 14 held".** a16 draft to the known `papa` allowed; a17 draft to the stranger `help@bank-verify.in` held; a18 the same with the owner's exact approval allowed; a19 `papa,kyc-desk@okaxis` held; a20 `Papa` (wrong case) held. a1 to a15 unchanged from 1e.
- Built with `make run/turiya KERNEL=sel4 BOARD=pc` in Genode 04b91c7 (sculpt-26.10 tree), tool chain 25.05. Image `turiya.iso`, SHA-256 f89f0a8e094fd3f6ec598b3be3023b08f7a6f343557bfd329f60a89201e2b910 (cloud build, not kept).
- **Re-run before publishing (16 Nov, 00:20 IST, with the owner's yes to publish):** the night runs' patches applied to this repo, every file matching the hashes in the patches. Host tests 56/56 with the same flags. The same Genode tree rebuilt the image and booted it on seL4 13.0.0 in QEMU: the same 20 verdicts, "kartr_gate: 6 allowed, 14 held". Image SHA-256 7b37ec44fe3ee97b96df80ff1d1aab0363d813a43795b6f832e6107f4d292913.
- Log: `boot_log_sel4_qemu.txt` (from the re-run).

Not done: a separate verdict name for an unknown (not planted) target; the run script's expect line still accepts any "N allowed, M held"; depot republish (needs the owner's yes). The `os/` gate (Turiya from zero) does not have the known-target list yet.
