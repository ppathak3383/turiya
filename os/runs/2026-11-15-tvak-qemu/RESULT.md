# Tvak, the first organ, and Sharira, the body survey (15 Nov 2026, emulator)

QEMU with UEFI firmware (OVMF), booted through Uṣas, as on the HP. Not yet run on the HP.

**Tvak (touch)** is the keyboard, moved out of Sākṣī into its own compartment:

| | Tvak |
|---|---|
| duty (svadharma) | key presses into characters, sent to Sākṣī |
| holds (maryādā) | its own CSpace with 5 capabilities: fault channel, send-only channel to Sākṣī, its notification, IRQ 1, ports 0x60-0x64. Its own VSpace: 7 pages (3 read-only code), TLS page, IPC buffer |
| does not hold | the screen, memory to allocate, the gate, any other device |
| recovery (cikitsā) | on a fault Sākṣī re-makes it from its seed (the 9,280-byte image, SHA-256 checked before every start) and restarts it |
| limit on recovery | 4 falls within 30 s: it rests, and Sākṣī feels the keyboard itself (reflex) until `heal tvak` |

`crash tvak` asks Tvak to write to 0x8000000000, where Sākṣī keeps the screen. Tvak has no page there:

| step | result |
|---|---|
| `crash tvak` | `Tvak fell: it touched memory it does not hold (ip 0x4012b9, address 0x8000000000)`; screen untouched |
| heal | re-made from its seed and awake again in 2.5-4.7 ms (emulator), generation 2, 3, 4 |
| 4th fall within 30 s | `Tvak rests`; typing still works through Sākṣī's reflex |
| `heal tvak` | awake again, generation 5 |

**Sharira (the body)**, `body`: a read-only survey. PCI configuration space through ports 0xcf8/0xcfc (5 functions in
the emulator), ACPI tables through a read-only window on physical memory (XSDT, 5 tables). The `\_S5` sleep values
are read from the DSDT as data; no ACPI code runs. `poweroff` writes the sleep type to the PM1a control port
(0xb004 here): the emulator switched off.

**A flaw in the inherited code**: seL4 13's x86-64 root-task loader zeroed the first 8 bytes of `.data` (it treated
the `PT_TLS` header as a segment). Fixed in `kernel/sel4-elf-load-only-pt-load.patch`.

Files: `crash_heal_rest.png` (screen after four falls), `serial_crash_body.txt` (serial log of crash, heal, body).
