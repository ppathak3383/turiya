# Gate step 1g: the four states (avastha) (15 Nov 2026, host and emulator)

Whether the owner is present now decides what may act at all, before any other rule:

| State | When | What the gate does |
|---|---|---|
| jagrat (waking) | the owner is here (default; any key from the physical keyboard) | the rules as before |
| svapna (dreaming) | `state svapna`, or 10 minutes without a key | the agent may think (`summarise`); everything that leaves a mark outside is `held_while_dreaming`, even with a valid approval, and kept as a plan |
| susupti (deep sleep) | `state susupti` | nothing acts: `held_while_asleep` |
| turiya (the fourth) | always | not a mode: the witness, the gate and its log, present in all three |

Malformed input is still reported first in every state.

| Test | Result |
|---|---|
| host tests (`os/sakshi/tests`) | 56/56 (8 new) |
| emulator: `state svapna`, `draft_reply papa` | `held_while_dreaming` |
| emulator: dreaming, `summarise inbox` | `allowed` (thinking leaves no mark) |
| emulator: `state susupti`, `summarise inbox` | `held_while_asleep` |
| emulator: `state jagrat`, `draft_reply papa` | `allowed` |
| emulator, idle limit shortened to 6 s for the test | dreamed after 6 s; the next key woke it ("welcome back") |

The 15 test actions run in jagrat and give the same verdicts as before: 4 allowed, 11 held.
