# Turiya OS: written from zero on seL4

Only one inherited piece: the seL4 microkernel (`kernel/`, two small boot-code patches). Everything else here is
Turiya's own code.

| Part | Name | What it is |
|---|---|---|
| `usas/` | Uṣas (dawn) | UEFI start-up in Rust, no crates: takes the firmware's screen and memory map, loads seL4 and Sākṣī, leaves the firmware behind |
| `sakshi/` | Sākṣī (witness) | the root task: screen, heart (50 Hz timer), the Kartr gate (`kartr_rule.h`, same code as the 36 host tests), makes and heals the organs |
| `sakshi/sharira.h` | Śarīra (body) | read-only hardware survey (PCI, ACPI tables) and power-off |
| `organs/tvak.cc` | Tvak (touch) | the keyboard, in its own compartment with 5 capabilities; re-made from its SHA-256-checked seed if it faults |
| `organs/kosa.cc` | Kośa (storehouse) | the NVMe SSD, read-only by construction and by proof (`kosa_cmd.h`); made only when `disk` is typed |
| `organs/nadi.cc` | Nāḍī (channel) | the USB 3 host controller: names what is plugged in, a USB keyboard types, a stick is read (`nadi_scsi.h`, proved read-only); made only when `usb` is typed |
| `organs/smrti.cc` | Smṛti (memory) | reads FAT32 and exFAT on a stick (`smrti_fs.h`); touches no device, Sākṣī fetches its blocks |
| `sakshi/vibhaga.h` | Vibhāga (division) | reads a partition table (GPT or MBR) from the SSD or a stick |
| `sakshi/dinacarya.h` | Dinacaryā (daily routine) | every minute re-reads each organ's read-only pages and Sākṣī's own code; an organ whose code changed is re-made from its seed. `sakshi/seal.py` stamps Sākṣī's build seal, checked at start |
| `organs/hrdaya.cc` | Hṛdaya (heart) | the 50 Hz timer, its own organ; Sākṣī's breath (RTC, 2 Hz) restarts it if it falls silent |
| `organs/kbd_i8042.h` | | keyboard tissue shared by Tvak and Sākṣī's reflex |
| `stick/` | | builds the UEFI USB image |

How it fits together: `ARCHITECTURE.md`.

## What is checked by machine

| Claim | How | Where |
|---|---|---|
| The gate's Rust decision meets its written specification | Verus proof; on seL4 it runs as a second judge beside the C++ rule, and the two agreed on all 1296 flag combinations | `gate-rs/`, `sakshi/proofs/` |
| The gate's C++ rule: 7 properties | CBMC, plus 17.5 M fuzzed inputs for the detector | `sakshi/proofs/` |
| Kośa can only read the SSD, only into its page, exactly the blocks asked | CBMC on the code that runs (theorems A, B); 7 mutants caught | `organs/proofs/` |
| Nāḍī can only read a stick: reading commands only, data only in | CBMC on the code that runs (theorems C, D); 6 mutants caught | `organs/proofs/` |
| Smṛti, Vibhāga and Nāḍī's descriptor walk survive any bytes a stick or device can hold | libFuzzer with ASan and UBSan: 38.4 M, 7.5 M and 167.5 M inputs, 0 crashes | `organs/proofs/`, `sakshi/proofs/` |
| What Sākṣī runs is what was built, and nothing changes an organ's code unseen | SHA-256 seal stamped after linking, checked at start; every read-only page re-read each minute against the seal taken when it was written; seal tests 9/9; emulator: 8 of 8 one-bit changes found, 0 false alarms in 19 rounds, a changed build caught at start | `sakshi/dinacarya.h`, `sakshi/seal.py`, `sakshi/tests/`, `runs/2026-11-16-dinacarya/` |

Not covered: the compiler, the devices' own firmware, and DMA by a confused controller (no IOMMU support on
AMD in seL4 yet).

Build: `organs/build.sh` (the organs, their seeds, then Sākṣī), then `stick/build_usas.sh` (Uṣas and the image).

Runs: `runs/`: first light and Uṣas on the HP; each organ in the emulator; the night of 15 Nov (proofs, fuzzing, healing first); the daily round (16 Nov).
