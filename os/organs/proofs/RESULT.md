# The storage organs can only read: proved, mutation-tested and fuzzed (15 Nov 2026, night)

Turiya's maryādā for storage says: **Kosa (the NVMe SSD) and Nadi (USB sticks) can only read, and only into
the page they were given.** Until tonight that rested on two hand-written checks. Now it rests on a
machine-checked proof of the code that runs, plus a static check that the proved function is the only road.

## What is proved (CBMC 5.95.1, bounded model checking)

The proofs include the organs' own code (`kosa_cmd.h`, `nadi_scsi.h`, shared unchanged with `kosa.cc` and
`nadi.cc`) and replace only the hardware. The rule is restated in each harness, independently of the code,
in 64-bit arithmetic, and checked at every doorbell and every transfer.

| Theorem | For every… | Checks | Result |
|---|---|---|---|
| A, Kosa | command any caller could build, any queue state, any doorbell stride | 22 | verified, 37 s |
| B, Kosa | request Sakshi could make (any block, count, disk geometry) | 24 | verified, 34 s |
| C, Nadi | SCSI command block any caller could build | 18 | verified, no loop cut |
| D, Nadi | stick read Sakshi or Smrti could request | 19 | verified, no loop cut |

**A.**
- Every doorbell releases exactly one entry: the one just checked.
- I/O queue: READ (0x02) only, into the data page, at most 4096 bytes, no FUA/limited-retry/protection flags, nothing fused, PRP1 alone.
- Admin queue: Identify (into the identify page) or creation of the one I/O queue pair (in their own pages, physically contiguous, no larger than a page).
- No other register is written.

**B.** What is released reads exactly the blocks asked for, all inside the disk. A reasonable request is still read.

**C.**
- One 31-byte command block per command.
- Only TEST UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY and READ(10).
- Direction IN or none: nothing is ever written to the stick.
- Data comes in only to the data page, ≤ 4096 bytes, exactly as announced. A READ(10) announces exactly blocks × block size.

**D.** Every request becomes a READ(10) of exactly the blocks asked for, inside the stick. A reasonable request is still read.

## The proofs have teeth

Each proof also runs against a **vacuity check**, which must fail; a failure shows the checked paths are reached.
It also runs against **mutants**: deliberately broken copies of the code, each of which must fail.

| Run | Must | Result | Caught by |
|---|---|---|---|
| vacuity A, B, C, D | fail | 4 / 4 failed | paths reached |
| **M1 Kosa code before tonight** | fail | **failed** | count−1 spills into CDW12's flag bits |
| M2 page check off by one | fail | failed | bytes ≤ 4096 |
| M3 doorbell releases two entries | fail | failed | exactly one entry |
| M4 admin accepts Set Features | fail | failed | admin opcodes |
| M5 fused commands let through | fail | failed | CDW0 bits 15:8 |
| M6 last block + 1 | fail | failed | inside the disk |
| M7 queue not physically contiguous | fail | failed | PC bit |
| **N1 Nadi code before tonight** | fail | **failed** | READ(10) length wraps |
| N2 WRITE(10) let through | fail | failed | opcode list |
| N3 data direction OUT | fail | failed | flags |
| N4 READ length ≠ announced | fail | failed | blocks × size |
| N5 no end-of-stick check | fail | failed | LBA truncated |
| N6 more than a page | fail | failed | ≤ 4096 |
| K1–K4 either check alone (Kosa, Nadi) | verify | 4 / 4 verified | defence in depth: each of the two checks holds by itself |

Files: `kosa_mutants_2026-11-15.txt`, `nadi_mutants_2026-11-15.txt`, `final_proofs_2026-11-15.txt`.

## Found tonight, fixed

| # | Where | What | Reachable before? | Fix |
|---|---|---|---|---|
| 1 | Kosa `read_blocks` | `count × block_size` in 32 bits could wrap and pass the one-page check. The controller would then DMA far past the data page: with PRP2 = 0, a multi-page read takes physical page 0 as its page list. | Not with Sakshi's current requests (1 block, or 4096 / block size) | Bound by division; `allowed()` re-checks every command in place |
| 2 | Nadi `storage_read` | the same wrap; the READ(10) length could disagree with what the page was told to expect | Not from Sakshi; possible from a bad request | Bound by division; the length must match exactly |
| 3 | Smrti | a crafted stick (looping FAT chain, 128 or 65536 sectors per cluster) made one `files` cost 33,026 block reads. Sakshi stopped at 2,000 and **showed the half listing as if complete** | Yes, with a crafted stick | Smrti's budget (1500 blocks per request, then code 24, said plainly). Sakshi never shows a half answer. |
| 4 | Vibhaga (partition table) | GPT entry-array LBA from the disk was added without an overflow check; block 1 was read on a 1-block device | Harmless (the organs refuse), but untidy | Checked against the device's size first |
| 5 | Nadi | a stick reporting a block size that is not a power of two (e.g. 1000) was accepted | Odd devices only | Refused (status 0x3000) |

## Fuzzing (libFuzzer + AddressSanitizer + UndefinedBehaviorSanitizer)

| Target | Inputs | Time | Crashes | Timeouts | Notes |
|---|---|---|---|---|---|
| Smrti (FAT32, exFAT; bare, MBR, GPT) | 38.4 M | 1231 s, 2 workers | 0 | 0 | each block in a fresh allocation of exactly its size; budget checked |
| Vibhaga (MBR, GPT, 8 block sizes) | 7.2 M + 0.3 M | 630 s + 300 s | 0 after fix 4 | 0 | unread bytes poisoned |

Seeds are made by `smrti_seeds.py` and `../../sakshi/proofs/vibhaga_seeds.py`. `smrti_show.cc` prints what Smrti
makes of any input.

## What is not proved

- The compiler: the proof is about the C++, not the machine code.
- The controller's firmware: a confused NVMe or xHCI controller could still DMA anywhere. Without an IOMMU
  (seL4 has no AMD IOMMU support yet), that is the remaining gap.
- Kosa's `init()` order, the registers it writes, and that `submit()` is the only road: checked syntactically by
  `kosa_static_check.py`, not by the model checker.

## Run it

```
cbmc kosa_proof.cc -I.. -DTHEOREM_A --unwind 2       # and -DTHEOREM_B; add -DVACUITY: must FAIL
cbmc nadi_proof.cc -I.. -DTHEOREM_C --unwind 33 --unwinding-assertions   # and -DTHEOREM_D
python3 run_kosa_mutants.py; python3 run_nadi_mutants.py
python3 kosa_static_check.py ..; python3 nadi_static_check.py ..
clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address,undefined -I.. smrti_fuzz.cc -o smrti_fuzz
```

Kosa's harness cuts its one unbounded loop (polling for completion) after one pass. That is sound here: every
check is made before the loop, and the loop changes nothing the checks read. Nadi's harness cuts no loop.
