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
