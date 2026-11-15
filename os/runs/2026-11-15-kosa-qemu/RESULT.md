# Kosa, the storehouse: an NVMe organ that can only read (15 Nov 2026, emulator)

QEMU with UEFI firmware, booted through Uṣas, with an emulated NVMe SSD (256 MiB, GPT with 4 partitions).
Not run on the HP: it will touch the HP's SSD only after its owner says yes, even though it only reads.

| | Kosa |
|---|---|
| duty (svadharma) | read blocks for Sakshi into one shared page |
| holds (maryada) | 3 capabilities (fault channel, send-only channel to Sakshi, its notification); the controller's first 16 KiB of registers, uncached; 6 DMA pages; 1 window page shared with Sakshi; no IRQ (it polls, with time limits) |
| read-only by construction | the only I/O command it can build is Read (0x02), checked twice before the doorbell; the admin queue only takes Identify (0x06) and its own queue creation (0x05, 0x01). No write, format, sanitize, firmware or feature command exists in the program |
| made | only when `disk` is typed: nothing touches a disk at boot |
| recovery | like Tvak: re-made from its SHA-256-checked seed; it resets the controller and starts again |

| Test | Result |
|---|---|
| `disk` | `QEMU NVMe Ctrl`, 268 MB (524,288 blocks of 512 B); GPT: EFI system 104 MB, Microsoft reserved 16 MB, basic data 124 MB, recovery 20 MB |
| `crash kosa`, then `disk` | fell at ip 0x401650 touching 0x8000000000; healed in 4.5 ms (controller reset, identify, queues); `disk` works again |
| disk attached **read-only** to the emulator | `disk` works the same; the image's SHA-256 is unchanged |

Known gap: without an IOMMU a confused controller could write anywhere in memory by DMA. seL4 does not support
AMD's IOMMU yet; recorded for later.
