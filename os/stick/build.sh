#!/bin/sh
# Turiya stick image: GPT + EFI system partition holding one GRUB EFI file with seL4 and Sakshi inside.
set -e
cd "$(dirname "$0")"
cp ../kbuild/kernel.elf sel4.elf
cp ../sakshi/sakshi sakshi
grub-mkstandalone -O x86_64-efi -o BOOTX64.EFI --modules="part_gpt fat multiboot2 efi_gop efi_uga all_video normal configfile" \
  "boot/grub/grub.cfg=grub.cfg" "boot/sel4.elf=sel4.elf" "boot/sakshi=sakshi" "boot/pad=pad"
rm -f turiya-sel4-uefi.img
truncate -s 64M turiya-sel4-uefi.img
sgdisk -o -n 1:2048:0 -t 1:ef00 -c 1:TURIYA turiya-sel4-uefi.img >/dev/null
OFF=$((2048*512))
mformat -i turiya-sel4-uefi.img@@$OFF -F -v TURIYA ::
mmd -i turiya-sel4-uefi.img@@$OFF ::/EFI ::/EFI/BOOT
mcopy -i turiya-sel4-uefi.img@@$OFF BOOTX64.EFI ::/EFI/BOOT/
sha256sum turiya-sel4-uefi.img
