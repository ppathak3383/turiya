# Turiya OS: how it is put together (17 Nov 2026)

One proven kernel, one witness, and organs. Each organ has one duty (*svadharma*), fixed limits (*maryādā*) and a
way back to health (*cikitsā*). Nothing above seL4 is inherited.

```
            firmware (UEFI) ── used once, at start ──> Uṣas (our loader, Rust) ──> seL4 13 (the only inherited code)
                                                   reads \TURIYA\OWNER.TXT (read-only)
                                                                                     │
                                                         Sākṣī, the witness (root task): screen, gate, approvals,
                                                         breath (RTC); makes the organs, watches them, heals them
                                                                                     │
   ┌───────────┬───────────┬──────────────┬──────────────────┬──────────────┬────────┴───────┐
 Tvak       Hṛdaya       Kośa          Nāḍī (one per USB   Smṛti         (next: Dūta,
 keyboard   heart        NVMe SSD      controller)          files          the network)
 i8042,     PIT 50 Hz,   read-only,    names devices, USB   FAT32/exFAT,
 IRQ 1      IRQ 0        made on       keyboard, sticks     touches no
                         'disk'        (read-only SCSI)     device
```

## What each part holds

| Part | Duty | Capabilities (its whole CSpace) | Memory | Made |
|---|---|---|---|---|
| Sākṣī | witness: screen, gate, approvals, the clock (Kāla); makes and heals organs | everything seL4 gives the root task | its own image, the screen | at boot |
| Tvak | key presses into characters | fault, channel out, its notification, IRQ 1, ports 0x60-0x64 | 7 pages (3 read-only), TLS, IPC buffer | at boot |
| Hṛdaya | beat: one signal per tick into Sākṣī | fault, channel out, notification, IRQ 0, ports 0x40-0x43, one heart signal | its own pages | at boot |
| Kośa | read blocks from the NVMe SSD | fault, channel out, notification | controller registers (16 KiB, uncached), 6 DMA pages, 1 window | `disk` |
| Nāḍī, Nāḍī2 | USB: name devices, listen to a keyboard, read a stick | fault, channel out, notification | its controller's registers only, 112 DMA pages, 1 window | `usb`, `stick`, `files`, `check` |
| Smṛti | list folders, read a file's start (FAT32, exFAT) | fault, channel out, notification | a read-only view of one data page, 1 window | `files`, `read` |

Organs cannot reach each other. When Smṛti needs a block, it asks Sākṣī, Sākṣī asks the stick's Nāḍī, and Smṛti
reads the block from a read-only view of Nāḍī's data page.

## Health

| Event | What happens |
|---|---|
| an organ touches memory it does not hold, or faults in any way | seL4 stops it at that instruction and tells Sākṣī |
| | Sākṣī re-makes it from its seed (the organ image, SHA-256 checked before every start) and starts it again: 1-125 ms |
| 4 falls within 30 s | the organ rests; for the keyboard, Sākṣī's reflex takes over; `heal <organ>` wakes it |
| the heart goes silent without falling | Sākṣī's breath (RTC, 2 Hz) notices after 2 s and re-makes it |
| a byte of an organ's code or constants changes (a stray write, a confused device's DMA, a bit flip in memory) | the daily round (*dinacaryā*, every minute; `sweep` at once) re-reads every read-only page against the SHA-256 seal taken when the page was written, and re-makes the organ from its seed: 1-125 ms. Three changes in one organ: it rests (the memory under it may be failing) |
| Sākṣī's own code changes | at start, Sākṣī compares its code and constants with the seal stamped at build (`sakshi/seal.py`); each round compares them with what it found at start. It cannot re-make itself, so it says so and asks for a restart |
| `crash <organ>`, `stop hrdaya`, `taint <organ>` | the tests for all of the above (`taint` flips one bit in an organ's read-only page, past its last byte of code) |

## The gate (Kartr)

Every action is judged before anything runs: `allowed`, or held (needs approval, approval mismatch, bad input,
outside the mandate, a target planted by outside content, while dreaming, while asleep).

| Layer | Checked by |
|---|---|
| the decision, in Rust (`gate-rs/gate.rs`) | Verus: 7 properties for all inputs, no bounds |
| the C++ rule in Sākṣī (`sakshi/kartr_rule.h`) | CBMC: the same properties for all inputs up to size bounds; 56 host tests |
| both on seL4 | every action goes to both; if they disagree, it is held |
| the outside-content detector | fuzzed: 17.5 million inputs with AddressSanitizer and UBSan |

The targets the owner knows come from the owner's own file, `\TURIYA\OWNER.TXT` on the stick (one `known: <name>` per
line). Uṣas reads it before the firmware is left, opened read-only, and writes it into a marked area of Sākṣī's
writable data, so the build seal still holds; `sakshi/owner_config.h` parses it and fails closed (a line it cannot
read exactly is counted and skipped, never cleaned up). No file, a file over 4 KiB, or a start by GRUB: the list is
empty, so every named target except the sender of the mail being answered needs the owner's approval. A held
target can be approved from the keyboard (`approve`), for exactly that action.

States (avasthā): **jāgrat**, the owner is here; **svapna**, dreaming, may think but not act (after 10 minutes
without a key); **suṣupti**, deep sleep, nothing acts; **turīya**, the witness, present in all three.

## Known gaps

- No IOMMU: a confused USB or NVMe controller could write anywhere in memory by DMA (seL4 has no AMD IOMMU support).
  The daily round notices such a write if it lands on an organ's code or on Sākṣī's own, within a minute; it cannot
  see writes to data pages, and re-making an organ keeps the same physical pages.
- Organs poll instead of taking interrupts (keyboard excepted); fine for now, costs a little power.
- No writes anywhere yet, by design: nothing is saved between starts.
- The touchpad (I2C-HID) and the network are not there yet.
- The clock is shown as the machine keeps it; the RTC carries no time zone, and Turiya never sets the clock.

## Read-only, proved (15 Nov 2026, night)

Kosa and Nadi each have one road to their device: `submit()` in `organs/kosa_cmd.h`, `scsi()` in
`organs/nadi_scsi.h`. Both files are shared unchanged by the organ and by its CBMC proof (`organs/proofs/`).
The proofs show that, for every command any caller could build and every request Sakshi or Smrti could make:
only reads leave, only into the one data page, never more than a page, exactly the blocks asked for, inside
the device. Static checks show these functions are the only roads. Smrti (`organs/smrti_fs.h`) and Vibhaga, the
partition-table reader (`sakshi/vibhaga.h`), read outside content. Both are fuzzed with AddressSanitizer and
UndefinedBehaviorSanitizer, and Smrti keeps a budget of 1500 blocks per request.

Healing: when an organ falls, Sakshi re-makes and restarts it first and writes the report after, so recovery
never waits for the screen.
