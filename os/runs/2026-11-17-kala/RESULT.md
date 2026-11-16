# Kala, the clock: the CMOS real-time clock read as data (host part)

Night item N2, 17 Nov 2026. Host only; Sakshi does not use it yet (the status-bar date and a `time` command are
the integration step).

## What was built
`os/sakshi/kala.h`: pure and freestanding (no library, no allocation, no global state; the freestanding -O3 object
has no undefined symbols). Sakshi hands it the bytes it read from ports 0x70/0x71; the header only decides what they
mean.

- `decode(Regs)`: seconds, minutes, hours, day, month, year in the format status B names (bit 2 binary or BCD,
  bit 1 24- or 12-hour with bit 7 of the hour byte = PM; 12 AM = 0, 12 PM = 12). Unknown, with the first bad field
  as the reason, for: a BCD digit above 9, a value out of range, a 12-hour value of 0 or above 12, a PM bit in
  24-hour mode, a day past its month's end (29 February only in leap years), status B's SET bit (the clock is being
  set), a century register (when the FADT names one) that is not 20. Years 2000-2099 only. The weekday register is
  never used; the weekday is computed from the date. A leap second (60) is not taken.
- `read(port, century_reg, tries = 8, uip_tries = 1000)`: a snapshot starts only after status A says no update is
  in progress; a time is given only when two adjacent snapshots agree byte for byte (an attempt whose wait runs out
  breaks the pair). Every loop is bounded: at most tries x (uip_tries + 8) register reads. Otherwise unknown
  (`NO_STABLE_READ`, `ALWAYS_UPDATING`).
- `seconds_since_2000`, `format` ("2026-11-17 03:10:05" or "unknown"), `why_name`.

The RTC says nothing about time zones; the clock is taken as it is set (on the target laptop, Windows keeps it in
local time). That choice belongs to the integration step.

## Checked
| Check | Result |
|---|---|
| Host tests, g++ 13 `-std=c++20 -Wall -Wextra -Werror -Weffc++` | **35/35** (0.6 s) |
| Same, clang 18 with ASan + UBSan | 35/35 |
| Every hour byte, 4 formats (BCD/binary x 12/24 h) vs a reference built by encoding | 1,024 bytes, 96 valid, all match |
| Every seconds and minutes byte, 4 formats | 2,048, all match |
| Every day x month x year byte triple, BCD and binary, vs std::chrono | **33,554,432**, 73,050 valid dates, all match; weekday matches on every one |
| Every date 2000-2099 x every hour x 4 formats: round trip, seconds count vs std::chrono, text vs snprintf | **3,506,400**, all match |
| read() with a scripted clock: across an update, never agreeing, always updating (exactly 8,000 asks), a failed wait between two equal snapshots, stable but invalid, century register, one try | all as intended |
| libFuzzer 10 min, ASan + UBSan: decode vs an independent reference, and read() against a clock that answers with fuzz bytes (the snapshots re-derived from the log of reads) | **70,490,323 inputs, 0 failures** |
| CBMC 5.95.1, `proof_decode`: K1-K4 for all 2^64+1 inputs (fixed size, no bound needed) | **622 checks, 0 failed** (0.6 s) |
| CBMC 5.95.1, `proof_read`: K1-K6, any clock behaviour, 3 tries x 2 asks | **691 checks, 0 failed** (2.1 s) |

Properties: K1 memory safety and no overflow; K2 a known time is in range (day within its month, 29 Feb only in a
leap year, weekday 0-6); K3 no guessing: re-encoding a known time in status B's format gives back exactly the bytes
read; K4 never while being set, century 20; K5 bounded reads, and a known time is the decode of the last two
completed snapshots, which are equal; K6 each snapshot starts right after status A said no update.

Mutants (9): a BCD digit above 9 allowed, 12 AM not 0, no leap year, PM bit ignored in 24-hour mode, SET bit
ignored, the first snapshot trusted, the update-in-progress wait skipped, century not checked, seconds up to 60.
**9/9 caught by the host tests, 9/9 by CBMC.**

## Run it
```
cd os/sakshi/tests && g++ -std=c++20 -O2 -Wall -Wextra -Werror -Weffc++ -I.. test_kala.cc -o test_kala && ./test_kala
cd os/sakshi/proofs && clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -I.. kala_fuzz.cc -o kala_fuzz && ./kala_fuzz -max_total_time=600 -max_len=300
F="--bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --conversion-check --unwind 101 --unwinding-assertions"
cbmc kala_proof.cc -I.. -DKALA_DECODE_ONLY --function proof_decode $F
cbmc kala_proof.cc -I.. --function proof_read $F
```

## Not done
- Not in Sakshi yet: the date on the status bar, a `time` command, the century index from the FADT. Sakshi already
  owns ports 0x70/0x71 (the breath, 2 Hz periodic interrupt); reading the time must not change registers A/B.
- `read()` is proved for 3 tries x 2 asks; the default (8 x 1000) is covered by the tests and the fuzzer.
- Time zone and setting the clock: not in scope (reading only).
