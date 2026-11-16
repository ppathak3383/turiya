# Dinacaryā, the daily round (16 Nov 2026, emulator)

Every minute Sākṣī re-reads what must never change: each organ's read-only pages (its code and constants) and
its own code. A page that differs from the seal taken when it was written is healed at once: the organ is
re-made from its SHA-256-checked seed and started again, and the report is written after it is awake. Sākṣī
cannot re-make itself; if its own code changes, it says so and asks for a restart. At start it also checks that
what was loaded is what was built: `sakshi/seal.py` stamps the SHA-256 of its code and constants into it after
linking.

| What | Result |
|---|---|
| Seal tests (`sakshi/tests/test_seal.py`) | 9/9: a one-bit change anywhere in code or constants (1,582 places tried) changes the seal; a change in writable data does not; a file with no mark, two marks or a mark in read-only memory is refused |
| A build changed by one byte after sealing (`serial_tampered_build.txt`) | caught at start: "own code 153033 bytes, DIFFERS from what was built: write the stick again" |
| One-bit changes in organs' read-only pages (`taint`) | 8 of 8 found by the round (`sweep` or the minute's round); 7 re-made from their seeds, the third change in one organ made it rest |
| False alarms | 0 in 19 rounds, including an 8-minute run with five organs (keyboard, heart, disk, USB, files) reading the disk and the stick |
| Healing after a change | Tvak 1.2-1.3 ms, Kośa 2.9 ms, Smṛti 1.3 ms, Hṛdaya 0.9 ms, Nāḍī 104-125 ms (its controller is reset) |
| After healing | the disk's partitions and the stick's files read again; a USB keyboard types again after Nāḍī is re-made |
| A round's cost | 18 read-only pages and Sākṣī's own 153 KB re-read in 4.2-5.8 ms in the emulator (software SHA-256, emulated processor; the HP will be faster) |
| Regression (`serial_regression.txt`) | `crash tvak` healed in 1.6 ms, `crash hrdaya` 0.7 ms, `stop hrdaya` 0.9 ms; gate 4 allowed, 11 held; the two judges agreed 30 of 30 |

Commands: `sweep` runs the round now; `taint <organ>` flips one bit in an organ's read-only page, past its last
byte of code, so it runs on unaware until the round finds it. `organs` and `check` show the rounds and changes.

Why: no IOMMU is set up, so a confused USB or NVMe controller could write anywhere by DMA, and laptop memory has
no error correction. seL4 already keeps organs from writing their own code; the round covers everything else
that could. It does not see changes to data pages, and re-making an organ keeps the same physical pages: if one
keeps changing, the organ rests after the third change.

Logs: `serial_three_changes.txt` (three changes in Tvak: healed, healed, rests; `heal tvak` wakes it),
`serial_storage_and_soak.txt` (Kośa, Nāḍī and Smṛti changed and healed, then 8 minutes with disk and stick reads),
`serial_heart.txt` (Hṛdaya changed and healed), `serial_regression.txt`, `serial_tampered_build.txt`, `test_seal.txt`. The first two ran a build that differed
from the final one only in the wording of one summary line.
