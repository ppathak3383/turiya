# Turiya OS: written from zero on seL4

Only one inherited piece: the seL4 microkernel (`kernel/`). Everything else here is Turiya's own code.

- `sakshi/`: the first program (root task). Own start code, TLS and IPC buffer set-up, maps the UEFI screen
  from device memory, own font renderer, runs the Kartr gate rule (`kartr_rule.h`, same code as the 36 host
  tests in `phase1/goa/kartr_gate/tests/`). No C library, no runtime, ~27 KB.
- `stick/`: builds a UEFI USB image (GPT + EFI partition). GRUB is the temporary loader; it is replaced by
  Turiya's own start-up code in the next step. The 16 MB `pad` module works around seL4's check that
  boot modules end above the kernel; our own loader will place modules properly.

First light (15 Nov 2026), emulator with UEFI firmware (OVMF): seL4 starts, Sakshi draws its log on screen,
gate 15/15 as expected, "4 allowed, 11 held".
