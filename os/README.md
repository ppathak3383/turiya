# Turiya OS: written from zero on seL4

Only one inherited piece: the seL4 microkernel (`kernel/`, two small boot-code patches). Everything else here is
Turiya's own code.

| Part | Name | What it is |
|---|---|---|
| `usas/` | Uṣas (dawn) | UEFI start-up in Rust, no crates: takes the firmware's screen and memory map, loads seL4 and Sākṣī, leaves the firmware behind |
| `sakshi/` | Sākṣī (witness) | the root task: screen, heart (50 Hz timer), the Kartr gate (`kartr_rule.h`, same code as the 36 host tests), makes and heals the organs |
| `sakshi/sharira.h` | Śarīra (body) | read-only hardware survey (PCI, ACPI tables) and power-off |
| `organs/tvak.cc` | Tvak (touch) | the keyboard, in its own compartment with 5 capabilities; re-made from its SHA-256-checked seed if it faults |
| `organs/kosa.cc` | Kośa (storehouse) | the NVMe SSD, read-only by construction; made only when `disk` is typed |
| `organs/nadi.cc` | Nāḍī (channel) | the USB 3 host controller: names what is plugged in, a USB keyboard types; made only when `usb` is typed |
| `organs/smrti.cc` | Smṛti (memory) | reads a FAT32 file system on a stick; touches no device, Sākṣī fetches its blocks |
| `organs/hrdaya.cc` | Hṛdaya (heart) | the 50 Hz timer, its own organ; Sākṣī's breath (RTC, 2 Hz) restarts it if it falls silent |
| `organs/kbd_i8042.h` | | keyboard tissue shared by Tvak and Sākṣī's reflex |
| `stick/` | | builds the UEFI USB image |

Build: `organs/build.sh` (the organs, their seeds, then Sākṣī), then `stick/build_usas.sh` (Uṣas and the image).

Runs: `runs/` (first light on the HP, Uṣas on the HP, Tvak in the emulator).
