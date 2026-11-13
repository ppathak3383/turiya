# Phase 2 step 2: Turiya on seL4 in VirtualBox (13 Nov 2026, ~17:05 IST)

Result: PASSED. Both Turiya parts ran on seL4 13.0.0 inside VirtualBox on the HP Pavilion (AMD Ryzen).

- Boot chain: GRUB2 -> Bender -> seL4 13.0.0 -> Genode core -> init
- `[init -> turiya_hello] Turiya: Sakshi is awake. I hold no capabilities except this log.`
- `[init -> kartr_gate] kartr_gate: 3 allowed, 5 held` (same 8/8 as QEMU and Sculpt)

## Two fixes on the way
1. First boot halted: "XSAVE buffer set set to 576, but needs to be at least 832".
   The CPU reports a larger FPU save area than seL4's default. Raised CONFIG_XSAVE_SIZE to 832.
2. Second boot halted: "XSAVEOPT requested, but not supported". VirtualBox does not pass
   XSAVEOPT through to the guest. Switched seL4 to plain XSAVE.

Both are edits to `repos/base-sel4/lib/mk/spec/x86_64/kernel-sel4-pc.mk` (extra sed lines on gen_config.h).
QEMU still gives 8/8 after both.

Image: `phase2/images/turiya-sel4-xsave.iso`
SHA-256 0332fe5a17c77fd7e2f82933453197ae3d7c269ef9924e5f22d05bbc065b715d
