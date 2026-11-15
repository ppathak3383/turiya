#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya: build the organs, turn each into a seed, then build Sakshi with the seeds inside it.
set -e
cd "$(dirname "$0")"
INC=$(cat ../sakshi/inc.txt)
FL="-std=c++20 -O2 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-pic -no-pie -nostdlib -static
    -fno-tree-loop-distribute-patterns -Wall -Wextra -Wno-missing-field-initializers -Wno-unused-parameter"
LD="-Wl,-Ttext-segment=0x400000 -Wl,-z,max-page-size=0x1000 -Wl,--build-id=none"

g++ $FL $INC $LD -o tvak tvak.cc
strip -s -o tvak.seed tvak
python3 mkseed.py tvak.seed TVAK ../sakshi/tvak_seed.h

g++ $FL $INC $LD -o kosa kosa.cc
strip -s -o kosa.seed kosa
python3 mkseed.py kosa.seed KOSA ../sakshi/kosa_seed.h

g++ $FL $INC $LD -o hrdaya hrdaya.cc
strip -s -o hrdaya.seed hrdaya
python3 mkseed.py hrdaya.seed HRDAYA ../sakshi/hrdaya_seed.h

g++ $FL $INC $LD -o nadi nadi.cc
strip -s -o nadi.seed nadi
python3 mkseed.py nadi.seed NADI ../sakshi/nadi_seed.h

# the Verus-proved gate decision: proof annotations stripped, built as a no_std library
cd ../gate-rs
python3 strip_verus.py gate.rs > gate_exec.rs
rustc +stable --edition 2021 --crate-type staticlib --target x86_64-unknown-none -C panic=abort -C opt-level=s \
      -C relocation-model=static -C code-model=small -o libgate_proved.a lib.rs 2>/dev/null

cd ../sakshi
g++ $FL $INC -I../organs $LD -o sakshi sakshi.cc ../gate-rs/libgate_proved.a
ls -l sakshi | awk '{print "sakshi", $5, "bytes"}'
