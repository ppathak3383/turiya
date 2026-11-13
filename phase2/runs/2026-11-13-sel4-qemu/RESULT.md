# Phase 2, step 1: Turiya on seL4 (13 Nov 2026)

**Result: PASS.** Genode (sculpt-26.10 tree, commit 04b91c7) on the **seL4 13.0.0** microkernel, x86_64, booted in QEMU from a GRUB2 ISO.
Both Turiya components ran with the same results as on Genode's Linux base and inside Sculpt OS:

| Component | Result on seL4 |
|---|---|
| turiya_hello | "Turiya: Sakshi is awake. I hold no capabilities except this log." |
| kartr_gate | 8/8: "kartr_gate: 3 allowed, 5 held" (₹15,000 with the exact approval allowed; the same approval for ₹50,000 or another payee held; delete and forged share held) |

## Boot chain (from boot_log.txt)
GRUB2 → Bender → seL4 kernel ("Booting all finished, dropped to user space") → Genode core → init → turiya_hello, kartr_gate.
Boot modules: config 1,137 B, turiya_hello 10,008 B, kartr_gate 43,896 B, ld.lib.so, init.

## Files
- `turiya-sel4.iso` (5,963,776 bytes, SHA-256 8ac8e091e8d759f9449d25177ab7840d0fe67db94e9b533a21b8934b752b244d): bootable image; its log goes to the first serial port (COM1), not the screen.
- `turiya.run`: the Genode run script (config in Genode's human-readable format).
- `boot_log.txt`: the full serial log.

## How it was built
Genode source from codeberg.org/genodelabs/genode (04b91c7, 30 Oct 2026); ports prepared: sel4 (hash 54809b28…), sel4_tools, grub2. Tool chain 25.05.
`make run/turiya KERNEL=sel4 BOARD=pc` in an x86_64 build directory with `REPOSITORIES += repos/turiya`
(repos/turiya/src/app/{turiya_hello,kartr_gate}/{main.cc,target.mk}). Build + boot: ~4 minutes on 2 cores.

## Honest limits
- Emulated PC (QEMU), not yet real hardware.
- seL4's x86-64 proof covers the C code's functional correctness, not the compiled binary or the security properties. ARM has the stronger proofs.
- The approval key in kartr_gate's config is a test key.

## Next
Step 2: boot this ISO in VirtualBox on the laptop (serial output to a file). Step 3: real hardware.
