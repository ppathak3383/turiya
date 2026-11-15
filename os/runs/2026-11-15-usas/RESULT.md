# Usas replaces GRUB; Sakshi takes the keyboard (15 Nov 2026)

**Usas** (the dawn), `os/usas/`: Turiya's own UEFI start-up, Rust, no crates, ~400 lines. It carries seL4 and
Sakshi inside one EFI file and:
1. picks the largest direct-colour screen mode (GOP),
2. finds the ACPI RSDP,
3. lays out seL4 at its physical address and Sakshi as a boot module in free memory above it,
4. converts the firmware's memory map to multiboot2 (sorted, merged, at most 15 free regions: seL4 accepts 16),
5. leaves the firmware (ExitBootServices) and never calls it again,
6. in a 2-page trampoline below 4 GiB: 64-bit → 32-bit compatibility mode, paging off, EFER.LME and CR4
   cleared, copies seL4 and Sakshi into place, enters seL4 with the multiboot2 magic.

No padding module any more: Sakshi sits above the kernel, where seL4 expects it.

**Sakshi** now holds three things: the screen, the keyboard (i8042, IRQ 1) and a heart (PIT, IRQ 0, 50 Hz).
The keyboard is also read on every heartbeat, so it works even where a laptop wires IRQ 1 unusually. First
command line: `help`, `about`, `gate`, `mail`, any action through the gate, `approve` (only from this keyboard:
HMAC-SHA256 of type|target|amount), `log` (witness log), `clear`, `reboot`.

Emulator (QEMU + OVMF UEFI firmware), `serial_log.txt`:
- Usas → seL4 reads our boot information: module at 0x1780000, 2048x2048 screen, 6 free memory regions, ACPI.
- Sakshi: gate 15/15, "4 allowed, 11 held"; `pay ramesh.kumar@ybl 15000` held; `approve` → allowed with
  approval, approval code 9a98f640…5a31, identical to the approval used in every earlier gate test.
- Keyboard by interrupt; heart 50 Hz; `reboot` restarts the machine (Usas ran a second time).

Stick: `EFI/BOOT/BOOTX64.EFI` = Usas; fallback `EFI/GRUB/GRUBX64.EFI` (GRUB + seL4 + Sakshi), selectable from
the firmware's "boot from EFI file" menu.
