#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya - host tests for seal.py, Sakshi's build seal. Usage: test_seal.py <a built sakshi ELF>
# The seal must change when any byte Sakshi runs or reads as constant changes, and must not change when its
# writable data changes (where the stamp itself lives).
import os, struct, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import seal  # noqa: E402

passed = failed = 0


def check(name, ok):
    global passed, failed
    print(('pass  ' if ok else 'FAIL  ') + name)
    passed += ok
    failed += not ok


def stamp_of(b):
    at = bytes(b).index(seal.MARK) + len(seal.MARK)
    return bytes(b[at:at + 32])


def main(elf):
    orig = open(elf, 'rb').read()
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, 'sakshi')
        open(p, 'wb').write(orig)
        seal.main(p)
        b = bytearray(open(p, 'rb').read())
        digest, n = seal.seal(b)
        check('the stamp is the SHA-256 of the read-only segments', stamp_of(b) == digest)
        seal.main(p)
        check('sealing twice gives the same file', open(p, 'rb').read() == bytes(b))

        segs = list(seal.segments(b))
        ro = [(off, fsz) for typ, fl, off, va, pa, fsz, msz, al in segs if typ == 1 and not fl & 2 and fsz]
        rw = [(off, fsz) for typ, fl, off, va, pa, fsz, msz, al in segs if typ == 1 and fl & 2 and fsz]
        check('there are read-only and writable segments', bool(ro) and bool(rw))

        # every 97th byte of every read-only segment, and its first and last bytes: one flip must change the seal
        missed = tried = 0
        for off, fsz in ro:
            for i in sorted(set(list(range(0, fsz, 97)) + [0, fsz - 1])):
                c = bytearray(b)
                c[off + i] ^= 0x01
                tried += 1
                try:
                    missed += seal.seal(c)[0] == digest
                except (AssertionError, struct.error, IndexError):
                    pass                                   # a header that no longer parses: Sakshi says 'differs'
        check(f'a one-bit change in code or constants changes the seal ({tried} places tried)', missed == 0)

        # writable data, away from the stamp: the seal must not move
        at = bytes(b).index(seal.MARK)
        moved = 0
        for off, fsz in rw:
            for i in range(0, fsz, 211):
                if at - 8 <= off + i < at + 48 + 8:
                    continue
                c = bytearray(b)
                c[off + i] ^= 0xff
                moved += seal.seal(c)[0] != digest
        check('changing writable data leaves the seal alone', moved == 0)

        c = bytearray(b)
        c[at] ^= 0x01
        q = os.path.join(d, 'nomark')
        open(q, 'wb').write(c)
        try:
            seal.main(q)
            check('a file without the mark is refused', False)
        except (AssertionError, SystemExit):
            check('a file without the mark is refused', True)

        c = bytearray(b) + bytearray(seal.MARK)
        open(q, 'wb').write(c)
        try:
            seal.main(q)
            check('a file with two marks is refused', False)
        except (AssertionError, SystemExit):
            check('a file with two marks is refused', True)

        # the mark moved into a read-only segment (a planted copy of the stamp in code would be covered)
        c = bytearray(b)
        c[at:at + 16] = b'\0' * 16
        off, fsz = ro[-1]
        c[off + fsz - 64:off + fsz - 48] = seal.MARK
        open(q, 'wb').write(c)
        try:
            seal.main(q)
            check('a mark inside a read-only segment is refused', False)
        except (AssertionError, SystemExit):
            check('a mark inside a read-only segment is refused', True)

        # the phdr fields the seal reads: phoff, phentsize and phnum come from the header it also covers
        phoff, = struct.unpack_from('<Q', b, 32)
        check('the program headers lie inside the first read-only segment, so they are sealed too',
              ro[0][0] == 0 and phoff + 56 * len(segs) <= ro[0][1])
    print(f'{passed}/{passed + failed} passed')
    return failed == 0


if __name__ == '__main__':
    sys.exit(0 if main(sys.argv[1]) else 1)
