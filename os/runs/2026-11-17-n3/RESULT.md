# 17 Nov 2026, morning: the owner's file and the clock in Sakshi; Turiya builds from an empty folder

The two night items, re-checked and joined in: N1 (`owner_config.h`, the owner's own list of known targets) and
N2 (`kala.h`, the clock). Then the build made reproducible from public sources, with a test suite for the emulator.

## The night patches, re-checked before use
Both patches were re-created file by file and every file matched its patch's blob id; they are in the repo as the
night runs wrote them (6ed9cfd, e4eab39). Re-run here:

| Check | N1, owner's list | N2, clock |
|---|---|---|
| Host tests, g++ 13 `-Wall -Wextra -Werror -Weffc++` | 42/42 | 35/35 |
| Same, clang with ASan + UBSan | 42/42 | 35/35 |
| CBMC 5.95.1 | Q1-Q5 1,116/1,116 (733 s); Q1-Q6 1,116/1,116 (697 s) | proof_decode 622/622; proof_read 691/691 |
| libFuzzer, ASan + UBSan | (6.9 M in the night run) | 17.0 M more in 150 s, 0 failures |

## What was joined in
- **The owner's file.** Uṣas reads `\TURIYA\OWNER.TXT` from the device it was started from (opened read-only, nothing
  else opened) and writes it into a marked area of Sākṣī's writable data in its staged copy, before seL4 starts:
  `usas/src/owner.rs` (pure; host tests 6/6: finds the one area, refuses two marks, no mark, no room or a used area,
  writes nothing outside it, a full 4096-byte file fits and one byte more does not). The build seal covers only
  Sākṣī's read-only segments, so it still holds. Sākṣī parses the area with `owner_config.h`. States: read, no
  file, over 4096 bytes, unreadable, untouched (a start by GRUB). Anything but a read file gives an empty list:
  every named target other than the sender of the mail being answered needs the owner (default-deny).
  New: `owner` (the names, every line counted), a boot line, a `check` line; a sample `stick/OWNER.TXT` with the
  names the 15 test actions use, so the boot verdicts stay "4 allowed, 11 held".
- **Approving an unknown target.** `explain()` already said "Type 'approve'", but only irreversible holds could be
  approved. Now a target that is not known can be approved too, for exactly that action (the rule always allowed it).
- **The clock.** `kala_host.h` reads the CMOS clock through the breath's own ports once a heartbeat (registers
  0x00-0x0b and the FADT's century register; no clock register is written), for the status bar
  ("Tue 17 Nov 2026 02:31"), `time` and `check`. QEMU: BCD, 24-hour, century register 0x32 read as 20; 18 register
  reads per time; the date matched the host's UTC date.
- **C++ constructors.** The owner's list is an object with a constructor; GCC put it in `.init_array`, which
  Sākṣī never ran (harmless here, since all it did was zero memory that was already zero). Sākṣī now runs
  `.init_array` at start, as a C runtime would.

## The kernel, from public sources
The kernel tree used until now was seL4 13.0.0 as Genode's seL4 port leaves it: 12 Genode patches plus Turiya's
two. For this x86-64 configuration only two Genode changes reach the compiled kernel (the image at 2 MiB; edge
interrupts not masked). They are now `kernel/sel4-carried-from-first-tree.patch`, and `kernel/turiya_x64.cmake`
holds all 63 options. Both trees, built in one directory, give the same preprocessed kernel once file paths and
assertion line numbers are set aside (six blank lines differ). Image `m` uses the kernel built from upstream.

## Tools
- `tools/bootstrap.sh <work dir>`: tools (apt-get, rustup), seL4 13.0.0 fetched from GitHub at cd6d3b8 and
  patched, the kernel (its configuration compared option by option), the organs, the proved gate, Sākṣī (sealed),
  Uṣas and the image, then the host tests and the emulator suite. Run from an empty folder on 17 Nov: built in minutes, host tests 79 + 42 + 35 + 9 + 6 passed, emulator suite
  91/91, about 8 minutes in all.
- `tools/emulator_suite.sh <image> [dir] [scenarios]`: 10 fresh boots in QEMU with OVMF (boot, states, falls,
  round, storage, four owner's-file cases, poweroff); disks attached with `snapshot=on` or `readonly=on`.

## Not done
- CBMC on the owner's list at its real size (16 names of 63 bytes) still runs out of memory (7 GB here).
- On the HP: the clock and the owner's file have not run on real hardware yet (bundle m).
- The time zone: the clock is shown as the machine keeps it (Windows keeps local time); Turiya never sets it.
