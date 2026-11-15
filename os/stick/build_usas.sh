#!/bin/sh
# Turiya stick: Usas (our own start-up) as the default UEFI program, GRUB kept as a fallback.
set -e
cd "$(dirname "$0")"
cp ../kbuild/kernel.elf ../usas/payload/sel4.elf
cp ../sakshi/sakshi ../usas/payload/sakshi
(cd ../usas && cargo build --release --target x86_64-unknown-uefi 2>&1 | grep -E "^error" && exit 1 || true)
cp ../usas/target/x86_64-unknown-uefi/release/usas.efi USAS.EFI
cp ../kbuild/kernel.elf sel4.elf; cp ../sakshi/sakshi sakshi
grub-mkstandalone -O x86_64-efi -o GRUBX64.EFI --modules="part_gpt fat multiboot2 efi_gop efi_uga all_video normal configfile" \
  "boot/grub/grub.cfg=grub.cfg" "boot/sel4.elf=sel4.elf" "boot/sakshi=sakshi" "boot/pad=pad"
IMG=turiya-usas.img
rm -f $IMG; truncate -s 64M $IMG
sgdisk -o -n 1:2048:0 -t 1:ef00 -c 1:TURIYA $IMG >/dev/null
OFF=$((2048*512))
mformat -i $IMG@@$OFF -F -v TURIYA ::
mmd -i $IMG@@$OFF ::/EFI ::/EFI/BOOT ::/EFI/TURIYA ::/EFI/GRUB
mcopy -i $IMG@@$OFF USAS.EFI ::/EFI/BOOT/BOOTX64.EFI
mcopy -i $IMG@@$OFF USAS.EFI ::/EFI/TURIYA/USAS.EFI
mcopy -i $IMG@@$OFF GRUBX64.EFI ::/EFI/GRUB/GRUBX64.EFI
sha256sum $IMG
