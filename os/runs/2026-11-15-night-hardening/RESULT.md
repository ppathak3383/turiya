# Night of 15 Nov 2026: the storage organs proved read-only, the readers fuzzed, healing before telling

Bundle **j** (`USAS-2026-11-15j.EFI`; image `turiya-usas-2026-11-15j.img`, hashes in the laptop's WORKLOG). It
supersedes bundle i, which had everything here except the descriptor walk (row 6 of the findings). Emulator: QEMU 8.2, OVMF, `-machine pc -cpu Skylake-Client`.

## Changes

| Part | Change |
|---|---|
| Kosa | the command path moved to `kosa_cmd.h`. One gate (`allowed`) checks every command twice, the second time in place before the doorbell. Reads are bounded by division, so no 32-bit wrap. |
| Nadi | the stick's commands moved to `nadi_scsi.h`. The READ(10) length must equal what the page expects. Block sizes that are not a power of two are refused. |
| Smrti | the reader moved to `smrti_fs.h`. A budget of 1500 blocks per request; past it, code 24, said plainly. |
| Vibhaga | Sakshi's partition-table reader moved to `vibhaga.h`. The GPT entry array is checked against the device size; devices under 2 blocks are refused. |
| Nadi (descriptor) | the configuration-descriptor walk moved to `nadi_desc.h` and fuzzed. A short answer leaves zeros, not the previous device's bytes; a keyboard's interval is clamped to 15. |
| Sakshi | **heal first, tell after**: a fallen organ is re-made and running before the report is written. Sakshi never shows a half answer from Smrti. |

Proofs and fuzzing: `os/organs/proofs/RESULT.md`.

## Emulator runs

| Run | Devices | What was typed | Result |
|---|---|---|---|
| K | NVMe (GPT) | `disk`, `crash kosa`, `disk`, `organs` | GPT read through the new gate; healed, reads again |
| S | FAT32 stick | `usb`, `stick`, `files`, `files Photos`, `read …`, `crash smrti`, `files` | all as before |
| E | exFAT stick | `files`, `files docs`, `read docs/…`, 3 × `crash smrti`, `files` | all as before |
| L | **a looping FAT32 stick** (from the fuzz tests) | `files` twice | "the stick asks for too much reading; Smrti stopped after 1500 blocks", both times; Smrti still answers (`serial_L.txt`) |
| A | NVMe + FAT32 stick | each organ falls once; `stop hrdaya`; then `files`, `disk` | every organ healed; files and disk work after (`serial_A.txt`) |
| T | exFAT stick | screen filled, `crash smrti`, `clear`, `crash smrti` | 1.1 ms and 0.47 ms (`serial_T.txt`) |
| C | 2 USB controllers, stick on the 2nd, NVMe | `check` | one screen, as before (`serial_C.txt`) |
| U | stick, then a USB keyboard plugged in while running | `usb`, `stick`, `files`, typed on the USB keyboard | keyboard found and listened to; 23 keys typed through Nadi (`serial_U.txt`) |

## Healing, before and after "heal first, tell after"

Before, the time from the fault to the organ's "ready" included Sakshi writing four lines about the fall. When
the screen was full, each line scrolled a 2048 × 2048 framebuffer in the emulator.

| Organ | Before | After |
|---|---|---|
| Smrti, screen full | 74.1 ms (also seen: 120.7, 126.4 ms) | 1.0–1.1 ms |
| Smrti, screen not full | 1.5–3.4 ms | 0.44–0.86 ms |
| Tvak | 2.5–4.7 ms | 1.0 ms |
| Kosa | 3.3–4.5 ms | 2.0 ms |
| Hrdaya (fell) | 1.8 ms | 0.49 ms |
| Hrdaya (went silent, seen by the breath) | 1.1 s to notice + heal | 55.7 ms heal after 2 s of silence (unchanged path) |
| Nadi | 125 ms | 104 ms (the controller reset itself waits 100 ms) |

The report reads exactly as before, just written once the organ is back:
"fell: … (ip …, address …)", "seL4 stopped it at that instruction…", "re-made from its seed…", "healed: awake
again in … microseconds". If an organ stays silent for two seconds after being re-made, the breath writes the
report anyway.
