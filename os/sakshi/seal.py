#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - stamp Sakshi's build seal: the SHA-256 of its read-only PT_LOAD segments (code, constants, the organs'
# seeds), written into SAKSHI_SEAL in .data, which the seal does not cover. At start Sakshi hashes the same bytes
# as loaded and compares (dinacarya.h). Usage: seal.py <sakshi ELF>   (re-sealing an already sealed file is fine)
import hashlib, struct, sys

MARK = b'TURIYA-SEAL-v1\x00\x00'


def segments(b):
    assert b[:4] == b'\x7fELF' and b[4] == 2 and b[5] == 1, 'not a little-endian ELF64 file'
    phoff, = struct.unpack_from('<Q', b, 32)
    phentsize, phnum = struct.unpack_from('<HH', b, 54)
    assert phentsize == 56 and 0 < phnum <= 16, 'unexpected program headers'
    for i in range(phnum):
        yield struct.unpack_from('<IIQQQQQQ', b, phoff + i * phentsize)


def seal(b):
    h = hashlib.sha256()
    n = 0
    for typ, flags, off, va, pa, fsz, msz, al in segments(b):
        if typ == 1 and not (flags & 2) and fsz:
            h.update(b[off:off + fsz])
            n += fsz
    return h.digest(), n


def main(path):
    b = bytearray(open(path, 'rb').read())
    assert b.count(MARK) == 1, 'the seal mark must appear exactly once'
    at = b.index(MARK) + len(MARK)
    # the stamp must land in a writable segment's file bytes, outside everything the seal covers
    ok = False
    for typ, flags, off, va, pa, fsz, msz, al in segments(b):
        if typ != 1:
            continue
        inside = off <= at and at + 32 <= off + fsz
        if inside and not (flags & 2):
            sys.exit('the seal mark lies in a read-only segment')
        ok = ok or (inside and bool(flags & 2))
    assert ok, 'the seal mark is not in a writable segment'
    d, n = seal(b)
    b[at:at + 32] = d
    assert seal(b) == (d, n), 'stamping changed the sealed bytes'
    open(path, 'wb').write(b)
    print(f'sealed {path}: {n} bytes of code and constants, sha256 {d.hex()}')


if __name__ == '__main__':
    main(sys.argv[1])
