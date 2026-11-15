# First light on real hardware (15 Nov 2026)

HP Pavilion 14 laptop, **AMD Ryzen 7 5825U**, 16 GB, UEFI firmware, Secure Boot off, booted from a USB stick.

GRUB (temporary loader) → **seL4 13.0.0** → **Sakshi**, Turiya's own first program, written from zero. On the
laptop's own screen:

- processor: AMD Ryzen 7 5825U with Radeon Graphics
- memory: 15,695 MiB handed to Sakshi
- screen: 1024 x 768 at 0xd0000000 (the firmware's GOP framebuffer)
- 154 untyped memory regions
- Kartr gate: all 15 test actions gave the expected verdict, **"4 allowed, 11 held"**

The same machine reset straight after GRUB with Sculpt OS 26.10 (Genode). With nothing above seL4 except our
own 27 KB program, it boots and runs. Photo: `hp_screen.jpg`.
