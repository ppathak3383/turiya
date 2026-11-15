# Smrti, memory: the organ that reads files on a USB stick (15 Nov 2026, emulator)

A file system on a stick is outside content, so it is not read by Sakshi. **Smrti** reads it, and it cannot
reach any device: when it needs a block it asks Sakshi, Sakshi asks the stick's Nadi, and Smrti reads the block
from a read-only view of Nadi's data page.

| | Smrti |
|---|---|
| duty (svadharma) | find the FAT32 partition (MBR or GPT basic data, or none) and list the files of its top folder, long names included |
| holds (maryada) | 3 capabilities (fault channel, send-only channel to Sakshi, its notification); a read-only view of one 4 KiB data page; 1 window |
| limits in its code | at most 256 clusters of the folder walked, 56 entries kept, names cut at 47 characters; every offset checked against the block |
| made | only when `files` is typed |

| Test (stick attached read-only; FAT32 made with mtools) | Result |
|---|---|
| `files` | TURIYATEST, FAT32 at block 2048, 3 entries: HELLO.TXT 21 bytes, Photos (folder), "Notes for later.txt" 21 bytes (a long name) |
| `crash smrti`, then `files` | fell at ip 0x4011ff touching 0x8000000000; healed in 2.0 ms; the same listing |

## Folders and reading a file

`files <folder>` lists any folder (a path of up to 8 names, any letter case, spaces allowed); `read <file>`
shows the first 1,024 bytes of a file as text (anything not printable shown as a dot).

| Test | Result |
|---|---|
| `files Photos` | 1 entry: "A long file name.jpg", 123 KB |
| `read Notes for later.txt` | 21 bytes: "hello from the stick" |
| `read nothing.txt` | "there is no such name" |

## exFAT

Most sticks larger than 32 GB come formatted as exFAT. Smrti now reads it too: entry sets (file, stream
extension, name pieces), the volume label, folders kept as one contiguous run of clusters or as a FAT chain.

| Test (a 63 MiB exFAT volume made with mkfs.exfat; files placed by hand) | Result |
|---|---|
| `files` | TURIYAEX, exFAT, 2 entries: Readme.txt 28 bytes, Docs (folder) |
| `files docs` (any letter case) | "A rather long document name.txt", 36 bytes |
| `read docs/A rather long document name.txt` | "second file, inside the Docs folder" |
| FAT32 stick afterwards | `files Photos` and `read hello.txt` unchanged |

## The stick Turiya starts from

GPT sticks with only an EFI system partition (FAT32) are read too, so on the HP `files` can list the very stick
Turiya booted from. Emulator, on a copy of Turiya's own stick image: top folder `EFI`; `files efi/boot`:
`BOOTX64.EFI`, 1 MB (that is Uṣas).
