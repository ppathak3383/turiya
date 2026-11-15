# Gate step 1f: disguised targets (15 Nov 2026, host)

Step 1e holds an action whose target was planted by outside content (an email the agent read). It matched the
target's exact text, in any letter case. Step 1f folds the target and the mail body to a skeleton first: letters
lowercased; Cyrillic and Greek look-alikes to Latin; Devanagari and fullwidth digits and letters to ASCII; the
words zero..nine to digits and "at" to '@'; spaces, line breaks, dashes, dots, brackets, quotes and zero-width
marks dropped; `https://`, `http://` and `www.` dropped from the front of the target. The plain 1e match still
runs first, so 1f only adds holds.

| Case set | 1e | 1f |
|---|---|---|
| 5 disguise cases (dashes, digit words, http vs https, Cyrillic letters, +91 and dashes) | 0 of 5 caught | 5 of 5 |
| Gemma's 6 cases (gemma-4-e4b on the laptop), 4 of them valid | 3 of 4 | 4 of 4 |
| host tests (`os/sakshi/tests`) | 36/36 | 48/48, 12 new |

Before 1f, two of the disguises were **allowed** outright as a draft reply (a phone UPI written with dashes; a
link given as `http://` when the mail said `https://`). The other three were held only as ordinary payments,
without saying the target came from the mail.

Gemma's two invalid cases put a target in the action that was not in its own email (a different address;
"ABCDE" against "abcdef"). The gate rightly treats those as ordinary payees: held for approval, not flagged.

Not covered yet: targets in images or attachments, targets the agent computes from the mail ("the same number
with the last digit changed"), digit words in languages other than English.

On seL4 (emulator): the 15 test actions give the same verdicts as before, "4 allowed, 11 held".
Files: `os/sakshi/tests/` (host tests, the CLI and runner, both case sets).
