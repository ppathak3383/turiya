# Nadi, the channel: the USB host controller as an organ (15 Nov 2026, emulator)

QEMU with UEFI firmware, booted through Uṣas, with a USB 3 controller (qemu-xhci) and a USB stick. Not yet run on
the HP.

| | Nadi |
|---|---|
| duty (svadharma) | find what is plugged into USB, name it, and listen to a USB keyboard (boot protocol), passing characters to Sakshi as Tvak does |
| holds (maryada) | 3 capabilities (fault channel, send-only channel to Sakshi, its notification); the controller's registers (64 KiB, uncached); 48 DMA pages; 1 window shared with Sakshi; no IRQ (it polls on Sakshi's 50 Hz pulse) |
| what it sends to devices | only the standard set-up requests (set configuration, set protocol, set idle), which carry no data; it never touches a storage device's data |
| made | only when `usb` is typed |
| firmware | takes the controller from the firmware once (USB legacy hand-off), then never asks it again |

| Test | Result |
|---|---|
| `usb` | xHCI 1.0, 8 ports, 64 slots; port 1: storage, 46f4:0001, super speed |
| a USB keyboard plugged in while running | "USB port 6: keyboard plugged in - listening: type on it" |
| typing on the USB keyboard | reaches Sakshi through Nadi (`abc` -> the gate's verdict) |
| `crash nadi` | fell at ip 0x402650 touching 0x8000000000; healed in 125 ms (controller reset, ports reset, both devices found again) |
| after the heal | the USB keyboard types again: 22 keys counted through Nadi |

Known gap: as with Kosa, without an IOMMU a confused controller could DMA anywhere.

## Later the same night: USB sticks, read-only

Nadi now also reads a USB stick (mass storage, bulk-only transport, SCSI), **read-only by construction**: the
only commands it can send are TEST UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY(10) and READ(10), checked
before the command block is sent and again in place. Anything else makes Nadi fall instead of sending.
`stick` lists the stick's partitions; the partition reader is now shared with `disk` and understands MBR and GPT.

| Test (stick attached read-only to the emulator) | Result |
|---|---|
| `stick` | QEMU HARDDISK, 67 MB (131,072 blocks of 512 B); MBR: 1 FAT32 41 MB bootable, 2 Linux 24 MB |
| `disk` afterwards (NVMe, GPT) | the same four partitions as before |
