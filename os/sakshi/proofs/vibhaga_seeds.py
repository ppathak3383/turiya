#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - valid partition tables (MBR with four partitions; GPT with five, at 512 and 4096 bytes a block)
# to start Vibhaga's fuzzer from. Each file: 2 setting bytes, then the device from block 0.
import os, struct, sys
BS = 512
le32 = lambda v: struct.pack('<I', v)
le64 = lambda v: struct.pack('<Q', v)
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
m = bytearray(BS)
for k, (t, start, n) in enumerate([(0x0c, 2048, 100000), (0x07, 102048, 5000000), (0x83, 6000000, 1000), (0xef, 1, 2047)]):
    e = bytearray(16); e[0] = 0x80 if k == 0 else 0; e[4] = t; e[8:12] = le32(start); e[12:16] = le32(n)
    m[446 + 16 * k:462 + 16 * k] = e
m[510:512] = b'\x55\xaa'
open(f'{out}/mbr', 'wb').write(bytes([0, 0x80]) + bytes(m))
g = bytearray(BS * 40)
p = bytearray(BS); e = bytearray(16); e[4] = 0xee; e[8:12] = le32(1); e[12:16] = le32(0xffffffff)
p[446:462] = e; p[510:512] = b'\x55\xaa'; g[0:BS] = p
h = bytearray(BS); h[0:8] = b'EFI PART'; h[72:80] = le64(2); h[80:84] = le32(40); h[84:88] = le32(128); g[BS:2 * BS] = h
types = [bytes.fromhex('28732ac11ff8d211ba4b00a0c93ec93b'), bytes.fromhex('16e3c9e35c0bb84d817df92df00215ae'),
         bytes.fromhex('a2a0d0ebe5b9334487c068b6b72699c7'), bytes.fromhex('a4bb94ded106404da16abfd50179d6ac'),
         bytes.fromhex('af3dc60f838472478e793d69d8477de4')]
names = ['EFI system partition', 'Microsoft reserved', 'Basic data', 'Recovery', 'Linux root']
for k in range(5):
    ent = bytearray(128); ent[0:16] = types[k]; ent[32:40] = le64(2048 + k * 1000); ent[40:48] = le64(2048 + k * 1000 + 999)
    for j, c in enumerate(names[k]): ent[56 + 2 * j:58 + 2 * j] = struct.pack('<H', ord(c))
    g[2 * BS + k * 128:2 * BS + k * 128 + 128] = ent
open(f'{out}/gpt', 'wb').write(bytes([0, 0x80]) + bytes(g))
open(f'{out}/gpt-4k', 'wb').write(bytes([3, 0x80]) + bytes(g))
print('3 seeds in', out)
