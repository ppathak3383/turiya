# Hrdaya, the heart, as an organ; Prana, Sakshi's breath, watches it (15 Nov 2026, emulator)

The timer left Sakshi. **Hrdaya** runs the PIT at 50 Hz and on every tick raises one bit in Sakshi's
notification; Sakshi counts ticks into beats and passes pulses on to the other organs. Hrdaya holds 6
capabilities: fault channel, channel to Sakshi, its notification, IRQ 0 (IOAPIC pin 2), ports 0x40-0x43, and one
signal into Sakshi that can only raise the heart bit.

A heart that falls is caught by seL4 and reported. A heart that goes silent is not a fault, so Sakshi keeps a
second, slower clock of its own: **Prana**, the breath, the RTC's periodic interrupt at 2 Hz (IRQ 8). Four
breaths without a tick, and Sakshi stops the heart, re-makes it from its seed and starts it again.

| Test | Result |
|---|---|
| boot | Hrdaya awake; breath counting (7 breaths at 3 beats) |
| `crash hrdaya` | fell at ip 0x401248 touching 0x8000000000; healed in 1.8 ms, generation 2 |
| `stop hrdaya` (alive but silent) | "no beat for 2 seconds, seen by the breath"; re-made, healed in 1.1 ms, generation 3 |
| `organs` afterwards | heart: 2 falls or stops, 2 heals; 51 breaths |

If Hrdaya cannot be made, Sakshi keeps the timer itself, as before.
