#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - small, valid FAT32 and exFAT disks (bare, inside an MBR, inside a GPT) to start the fuzzer from.
import os, struct, sys

BS = 512
def le16(v): return struct.pack('<H', v)
def le32(v): return struct.pack('<I', v)
def le64(v): return struct.pack('<Q', v)

class Disk:
    def __init__(self): self.b = {}
    def put(self, lba, data, off=0):
        blk = bytearray(self.b.get(lba, bytes(BS)))
        blk[off:off + len(data)] = data
        self.b[lba] = bytes(blk)
    def image(self):
        n = max(self.b) + 1
        return b''.join(self.b.get(i, bytes(BS)) for i in range(n))

def lfn_entries(name, short11):
    chk = 0
    for c in short11: chk = (((chk & 1) << 7) + (chk >> 1) + c) & 0xff
    u = [ord(c) for c in name] + [0]
    while len(u) % 13: u.append(0xffff)
    parts = [u[i:i + 13] for i in range(0, len(u), 13)]
    out = []
    for seq in range(len(parts), 0, -1):
        ch = parts[seq - 1]
        e = bytearray(32)
        e[0] = seq | (0x40 if seq == len(parts) else 0)
        for j, at in enumerate((1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30)):
            e[at:at + 2] = le16(ch[j])
        e[11] = 0x0f; e[13] = chk
        out.append(bytes(e))
    return b''.join(out)

def short(name11, attr, cluster, size):
    e = bytearray(32)
    e[0:11] = name11; e[11] = attr
    e[20:22] = le16(cluster >> 16); e[26:28] = le16(cluster & 0xffff); e[28:32] = le32(size)
    return bytes(e)

def fat32(d, base):
    bs = bytearray(BS)
    bs[0:3] = b'\xeb\x58\x90'; bs[3:11] = b'MSWIN4.1'
    bs[11:13] = le16(BS); bs[13] = 1; bs[14:16] = le16(4); bs[16] = 1; bs[21] = 0xf8
    bs[32:36] = le32(64); bs[36:40] = le32(1); bs[44:48] = le32(2)
    bs[71:82] = b'TURIYASEED '; bs[82:90] = b'FAT32   '; bs[510:512] = b'\x55\xaa'
    d.put(base, bytes(bs))
    fat = [0x0ffffff8, 0x0fffffff, 0x0fffffff, 0x0fffffff, 0x0fffffff, 6, 0x0fffffff, 0x0fffffff]
    d.put(base + 4, b''.join(le32(v) for v in fat))
    data = base + 5
    root = (lfn_entries('A long name.txt', b'ALONGN~1TXT') + short(b'ALONGN~1TXT', 0x20, 5, 600)
            + short(b'HELLO   TXT', 0x20, 3, 12) + short(b'DOCS       ', 0x10, 4, 0)
            + short(b'TURIYASEED ', 0x08, 0, 0) + b'\xe5' + bytes(31))
    d.put(data + 0, root)
    d.put(data + 1, b'hello world\n')
    d.put(data + 2, short(b'.          ', 0x10, 4, 0) + short(b'..         ', 0x10, 0, 0) + short(b'NOTE    TXT', 0x20, 7, 5))
    d.put(data + 3, b'A' * 512); d.put(data + 4, b'B' * 88)
    d.put(data + 5, b'note\n')

def ex_set(name, attr, cluster, length, contig=True):
    f = bytearray(32); f[0] = 0x85; f[1] = 1 + (len(name) + 14) // 15; f[4:6] = le16(attr)
    s = bytearray(32); s[0] = 0xc0; s[1] = 3 if contig else 1; s[3] = len(name)
    s[20:24] = le32(cluster); s[24:32] = le64(length); s[8:16] = le64(length)
    out = bytes(f) + bytes(s)
    for i in range(0, len(name), 15):
        n = bytearray(32); n[0] = 0xc1
        for j, c in enumerate(name[i:i + 15]): n[2 + 2 * j:4 + 2 * j] = le16(ord(c))
        out += bytes(n)
    return out

def exfat(d, base):
    bs = bytearray(BS)
    bs[0:3] = b'\xeb\x76\x90'; bs[3:11] = b'EXFAT   '
    bs[80:84] = le32(4); bs[84:88] = le32(1); bs[88:92] = le32(6); bs[92:96] = le32(32); bs[96:100] = le32(2)
    bs[108] = 9; bs[109] = 0; bs[110] = 1; bs[510:512] = b'\x55\xaa'
    d.put(base, bytes(bs))
    d.put(base + 4, b''.join(le32(v) for v in [0xfffffff8, 0xffffffff] + [0xffffffff] * 6))
    heap = base + 6
    label = bytearray(32); label[0] = 0x83; label[1] = 7
    for j, c in enumerate('TURIYAX'): label[2 + 2 * j:4 + 2 * j] = le16(ord(c))
    d.put(heap + 0, bytes(label) + ex_set('Readme.txt', 0x20, 3, 20) + ex_set('Docs', 0x10, 4, 512))
    d.put(heap + 1, b'readme content here\n')
    d.put(heap + 2, ex_set('A long name.txt', 0x20, 5, 30, contig=False))
    d.put(heap + 3, b'a rather long document.......\n')

def mbr_around(d, start, ptype):
    m = bytearray(BS)
    e = bytearray(16); e[4] = ptype; e[8:12] = le32(start); e[12:16] = le32(64)
    m[446:462] = e; m[510:512] = b'\x55\xaa'
    d.put(0, bytes(m))

def gpt_around(d, start):
    m = bytearray(BS); e = bytearray(16); e[4] = 0xee; e[8:12] = le32(1); e[12:16] = le32(0xffffffff)
    m[446:462] = e; m[510:512] = b'\x55\xaa'
    d.put(0, bytes(m))
    h = bytearray(BS); h[0:8] = b'EFI PART'; h[72:80] = le64(2); h[80:84] = le32(4); h[84:88] = le32(128)
    d.put(1, bytes(h))
    basic = bytes([0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7])
    ent = bytearray(128); ent[0:16] = basic; ent[16:32] = bytes(range(16)); ent[32:40] = le64(start); ent[40:48] = le64(start + 63)
    for j, c in enumerate('Data'): ent[56 + 2 * j:58 + 2 * j] = le16(ord(c))
    d.put(2, bytes(ent))

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
disks = {}
d = Disk(); fat32(d, 0); disks['fat32'] = d.image()
d = Disk(); mbr_around(d, 8, 0x0c); fat32(d, 8); disks['fat32-mbr'] = d.image()
d = Disk(); gpt_around(d, 16); fat32(d, 16); disks['fat32-gpt'] = d.image()
d = Disk(); exfat(d, 0); disks['exfat'] = d.image()
d = Disk(); mbr_around(d, 8, 0x07); exfat(d, 8); disks['exfat-mbr'] = d.image()
n = 0
for name, img in disks.items():
    for mode, path in [(0, 0), (0, 2), (1, 1), (1, 3), (1, 7), (0, 5), (1, 4), (1, 6), (0, 12)]:
        for ends in (0, 0x80):
            open(f'{out}/{name}-{mode}-{path}-{ends:x}', 'wb').write(bytes([0, mode, path, ends]) + img)
            n += 1
print(n, 'seeds in', out)
