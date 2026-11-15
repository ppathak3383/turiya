# Uṣas on the HP (15 Nov 2026, afternoon)

HP Pavilion 14, AMD Ryzen 7 5825U, 16 GB, UEFI, Secure Boot off, USB stick. Started from the firmware's own
file picker: F9 → Boot From EFI File → TURIYA → EFI → TURIYA → USAS.EFI. No GRUB anywhere.

**Uṣas** (Turiya's UEFI start-up, Rust, no crates) → **seL4 13.0.0** → **Sākṣī**.

| | First light (GRUB, morning) | Uṣas (afternoon) |
|---|---|---|
| screen | 1024 x 768 | **1920 x 1080**, the panel's native mode |
| memory handed to Sākṣī | 15,695 MiB | 15,692 MiB |
| untyped regions | 154 | 156 |
| keyboard | none | i8042, **by interrupt** (IRQ 1) |
| heart | none | PIT at 50 Hz, one beat a second on the status bar |
| gate, 15 test actions | 4 allowed, 11 held | 4 allowed, 11 held |

Typed on the laptop's own keyboard:

| typed | result |
|---|---|
| `help` | command list |
| `pay <payee> 15000` | `held_needs_approval` (payee blurred in the photo) |
| `approve` | `allowed_with_approval`; the HMAC-SHA256 on screen (`234b8661…93f8d6`) equals an independent Python computation over the same `type\|target\|amount` with the test key |
| `reboot` (video) | the screen went dark at once: the machine reset |

Status bar in the photo: heart 72 beats, keyboard by interrupt, keys 39.

Files: `hp_usas_screen.jpg`, `boot_frames.jpg` (six frames of the boot video: HP file picker → USAS.EFI → Sākṣī).
