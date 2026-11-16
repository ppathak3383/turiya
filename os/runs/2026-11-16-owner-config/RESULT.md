# N1 (step 5e, host part): the owner's own list of known targets — 16 Nov 2026

Until now Sakshi's list of known targets was compiled in (`OWNER_KNOWN` in `os/sakshi/sakshi.cc`). This step
adds the parser for a small text file the owner writes. Host only: the boot path is not touched; Sakshi reading
the file (from the stick, through Smrti) is the integration step (N3).

## What was built

- `os/sakshi/owner_config.h` — pure, freestanding, no allocation. `Owner_config::parse(bytes, len)` reads
  `known: <name>` lines and `#` comments and gives `known()`, the null-terminated list `decide()` takes.
  Fails closed: a name is 1-63 bytes of printable ASCII other than `|`, or bytes >= 0x80 (kept as they are,
  compared byte for byte, never folded); at most 16 names; a repeated name is ignored. Any other line (another
  keyword, `Known:`, a second word, a trailing comment, a control byte, NUL, `|`, an over-long or empty name)
  is skipped whole, never shortened or cleaned. Every line lands in exactly one count (comment, accepted,
  duplicate, over_limit, skipped) so Sakshi can tell the owner what was not taken. A file with no accepted
  name gives an empty list, which `decide()` treats as default-deny. One `\r` at the end of a line is dropped.
  Limits are template parameters so the proof can check a small instance; Sakshi uses `Owner_config`
  (16 names, 63 bytes).
- `os/sakshi/tests/test_owner_config.cc` — 42 host tests.
- `os/sakshi/proofs/owner_config_fuzz.cc` — libFuzzer + ASan + UBSan against an independent std::string
  reference model, for the real limits and a small instance (3 names of 5 bytes).
- `os/sakshi/proofs/owner_config_proof.cc` — CBMC harness, properties Q1-Q6 (see the file).

## Results

| Check | Result |
|---|---|
| Host tests, g++ 13 `-Wall -Wextra -Werror -Weffc++` | **42/42** |
| Same tests, clang 18 with ASan + UBSan | **42/42** |
| Parsed list vs the compiled-in list, through `decide()` (5 types x 9 targets x 3 states) | **135/135 the same verdict** |
| Existing gate tests (`test_kartr_rule.cc`) | 79/79 (unchanged) |
| libFuzzer, ASan + UBSan, 10 minutes, parser vs reference model | **6,881,295 inputs, 0 failures** (cov 255, ft 1511) |
| CBMC 5.95.1, small instance (1 name, 2 bytes, files <= 16 bytes), Q1-Q5 | **1,116 checks, 0 failed** (539 s) |
| CBMC, small instance, files <= 12 bytes, Q1-Q6 | **1,116 checks, 0 failed** (550 s) |
| CBMC, real instance (16 names, 63 bytes), files <= 40 bytes, Q1-Q5 | **not finished**: killed for memory (7 GB) after 199 s, also at 72 bytes |
| Mutants (no length check; `\|` allowed; no duplicate check; no 16-name limit) | **4/4 caught by the host tests**; the fuzzer caught the first three within 60 s each (the fourth was not recorded) |

Found on the way (by the fuzzer, in its first seconds): a last line without `\n` kept its `\r`, so
`known: papa\r` at the end of a file was skipped while the same line with `\n` was taken. Fixed: one `\r` is
dropped at the end of every line. Test added.

CBMC needed three changes to the header that do not change behaviour (its C++ front end): `counts()` returns a
copy, the character tests are namespace functions, `name()` uses an `if`. `at(i, j)` reads a name byte without
pointers, so the proof can read names (CBMC lost track of pointers into the object otherwise).

## Not done

- **CBMC on the real instance (16 names, 63 bytes) did not finish**: it ran out of the 7 GB this machine has,
  at 72 bytes and again at 40. The code is the same template; every limit path (full list, too-long name,
  duplicate, over the limit) is proved on the small instance, and the real one is fuzzed against a reference
  model. The day check should try it on a machine with more memory (or with `--slice-formula`, or a smaller
  unwind on the copy loop).
- Sakshi does not read the file yet; `OWNER_KNOWN` is still compiled in. The integration (where the file lives
  on the stick, `owner` showing the list and the counts, what happens when the file is missing: default-deny)
  is N3, day work.

## Run

```
cd os/sakshi/tests && g++ -std=c++20 -Wall -Wextra -Werror -Weffc++ -I.. test_owner_config.cc && ./a.out
cd os/sakshi/proofs && clang++ -std=c++20 -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
    -Wno-unknown-attributes -I.. owner_config_fuzz.cc -o f && ./f -max_total_time=600
cbmc owner_config_proof.cc -I.. --function check_small --unwind 18 --unwinding-assertions --bounds-check \
    --pointer-check --signed-overflow-check --unsigned-overflow-check   # also check_small_q6 (18), check_real (42)
```
