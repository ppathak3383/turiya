#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya bootstrap: from an empty directory to a bootable stick image, on Debian or Ubuntu (x86-64).
#
#   os/tools/bootstrap.sh [work dir]          (default: ./turiya-build)
#
#  1. Tools. Missing Debian/Ubuntu packages are installed with apt-get when run as root or with sudo;
#     SKIP_PACKAGES=1 only checks and stops if something is missing. Rust comes from rustup (stable, with the
#     x86_64-unknown-uefi and x86_64-unknown-none targets).
#  2. Sources. The os/ folder next to this script is copied into the work dir, so the repository is never written.
#     seL4 is fetched from github.com/seL4/seL4 at its 13.0.0 release (commit cd6d3b8c), checked, and patched
#     with os/kernel/*.patch (three small changes to boot and interrupt code; os/kernel/CONFIG.md says why).
#  3. Build. The kernel with os/kernel/turiya_x64.cmake (its configuration is then compared option by option);
#     the include list Sakshi is compiled with; the organs, the proved gate and Sakshi (os/organs/build.sh, which
#     also seals Sakshi); Usas and the stick image (os/stick/build_usas.sh). The result:
#     <work dir>/os/stick/turiya-usas.img, with a sample owner's file (os/stick/OWNER.TXT).
#  4. Checks. The host tests (C++ with -Werror, the seal, Usas's owner area); then the emulator suite
#     (os/tools/emulator_suite.sh, about 10 minutes) unless SKIP_SUITE=1.
# Everything it writes is inside the work dir; it writes to no disk and no device.
set -eu
SEL4_REV=cd6d3b8c25d49be2b100b0608cf0613483a6fffa      # seL4 13.0.0 ("recreate 13.0.0 release state")
SEL4_URL=https://github.com/seL4/seL4.git

here=$(cd "$(dirname "$0")" && pwd)
OS=$(cd "$here/.." && pwd)
W=${1:-$PWD/turiya-build}
mkdir -p "$W"; W=$(cd "$W" && pwd)
case "$W/" in "$OS/"*) echo "bootstrap: the work dir must be outside $OS" >&2; exit 2;; esac
LOG=$W/bootstrap.log; : > "$LOG"
say()  { printf '%s\n' "$*" | tee -a "$LOG"; }
step() { say ""; say "== $*"; }
die()  { say "bootstrap: $*"; exit 1; }
t0=$(date +%s)

# ---------------------------------------------------------------- 1. tools
step "1. tools"
need_pkgs=""
want() { command -v "$1" > /dev/null 2>&1 || need_pkgs="$need_pkgs $2"; }
want gcc build-essential;           want g++ build-essential;      want make build-essential
want cmake cmake;                   want ninja ninja-build;        want python3 python3
want git git;                       want curl curl;                want xz xz-utils
want patch patch;                   want mformat mtools;           want sgdisk gdisk
want grub-mkstandalone grub-common; want qemu-system-x86_64 qemu-system-x86
want clang clang;                   want cbmc cbmc
[ -d /usr/lib/grub/x86_64-efi ] || need_pkgs="$need_pkgs grub-efi-amd64-bin"
ls /usr/share/OVMF/OVMF_CODE*.fd > /dev/null 2>&1 || ls /usr/share/edk2/*/OVMF_CODE*.fd > /dev/null 2>&1 || need_pkgs="$need_pkgs ovmf"
for m in ply:python3-ply jinja2:python3-jinja2 lxml:python3-lxml six:python3-six future:python3-future yaml:python3-yaml; do
	python3 -c "import ${m%%:*}" 2> /dev/null || need_pkgs="$need_pkgs ${m#*:}"
done
need_pkgs=$(printf '%s\n' $need_pkgs | sort -u | tr '\n' ' ')
if [ -n "${need_pkgs// /}" ]; then
	[ "${SKIP_PACKAGES:-}" = 1 ] && die "missing packages:$need_pkgs"
	SUDO=""; [ "$(id -u)" = 0 ] || SUDO=sudo
	say "installing:$need_pkgs"
	$SUDO apt-get update -q >> "$LOG" 2>&1
	# shellcheck disable=SC2086
	DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y -q $need_pkgs >> "$LOG" 2>&1 || die "apt-get could not install:$need_pkgs"
fi
if ! command -v rustup > /dev/null 2>&1; then
	[ "${SKIP_PACKAGES:-}" = 1 ] && die "rustup is missing (https://rustup.rs)"
	say "installing rustup (stable)"
	curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal >> "$LOG" 2>&1
	. "$HOME/.cargo/env"
fi
rustup toolchain list | grep -q '^stable' || rustup toolchain install stable --profile minimal >> "$LOG" 2>&1
rustup target add --toolchain stable x86_64-unknown-uefi x86_64-unknown-none >> "$LOG" 2>&1
say "gcc $(gcc -dumpfullversion), $(cmake --version | head -1), ninja $(ninja --version), $(rustc +stable --version)"
say "$(qemu-system-x86_64 --version | head -1); $(cbmc --version 2> /dev/null | head -1 | sed 's/^/cbmc /')"

# ---------------------------------------------------------------- 2. sources
step "2. sources"
rm -rf "$W/os"; mkdir -p "$W/os"
cp -a "$OS"/. "$W/os"/
rm -rf "$W/os/kbuild" "$W/os/usas/target" "$W/os/usas/payload"
say "Turiya: $OS -> $W/os$(git -C "$OS" rev-parse --short HEAD 2> /dev/null | sed 's/^/ (commit /;s/$/)/')"
if [ ! -f "$W/sel4/.turiya-patched" ]; then
	rm -rf "$W/sel4"; mkdir -p "$W/sel4"
	git -C "$W/sel4" init -q
	git -C "$W/sel4" fetch -q --depth 1 "$SEL4_URL" "$SEL4_REV" || die "could not fetch seL4 from $SEL4_URL"
	git -C "$W/sel4" -c advice.detachedHead=false checkout -q FETCH_HEAD
	[ "$(git -C "$W/sel4" rev-parse HEAD)" = "$SEL4_REV" ] || die "seL4 is not at $SEL4_REV"
	[ "$(cat "$W/sel4/VERSION")" = 13.0.0 ] || die "seL4 VERSION is not 13.0.0"
	for p in "$W"/os/kernel/*.patch; do
		patch -p1 -s -d "$W/sel4" < "$p" || die "patch $(basename "$p") did not apply"
		say "patched seL4: $(basename "$p")"
	done
	touch "$W/sel4/.turiya-patched"
fi
say "seL4 13.0.0 at $SEL4_REV, changed files: $(git -C "$W/sel4" diff --name-only | tr '\n' ' ')"

# ---------------------------------------------------------------- 3. build
step "3. build"
K=$W/os/kbuild; S=$W/sel4
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$S/gcc.cmake" -C "$W/os/kernel/turiya_x64.cmake" -S "$S" -B "$K" >> "$LOG" 2>&1 \
	|| die "cmake could not configure the kernel (see $LOG)"
ninja -C "$K" >> "$LOG" 2>&1 || die "the kernel did not build (see $LOG)"
want_cfg=$(sed -n 's/^set(\([A-Za-z0-9_]*\) "\(.*\)" CACHE \(STRING\|BOOL\) "")$/\1:\3=\2/p' "$W/os/kernel/turiya_x64.cmake" | sort)
have_cfg=$(grep -E '^(Kernel|LibSel4)[A-Za-z0-9_]*:(STRING|BOOL)=' "$K/CMakeCache.txt" | sort)
[ "$want_cfg" = "$have_cfg" ] || { diff <(echo "$want_cfg") <(echo "$have_cfg") | tee -a "$LOG"; die "the kernel's configuration differs from turiya_x64.cmake"; }
say "kernel: $(stat -c %s "$K/kernel.elf") bytes, sha256 $(sha256sum "$K/kernel.elf" | cut -c1-16)..., all $(echo "$want_cfg" | wc -l) options as listed"

echo "-I$K/libsel4/include -I$K/libsel4/arch_include/x86 -I$K/libsel4/sel4_arch_include/x86_64 -I$K/libsel4/autoconf" \
     "-I$K/libsel4/gen_config -I$K/gen_config -I$S/libsel4/include -I$S/libsel4/arch_include/x86" \
     "-I$S/libsel4/sel4_arch_include/x86_64 -I$S/libsel4/sel4_plat_include/pc99 -I$S/libsel4/mode_include/64" \
     > "$W/os/sakshi/inc.txt"
sh "$W/os/organs/build.sh" >> "$LOG" 2>&1 || die "the organs or Sakshi did not build (see $LOG)"
[ -s "$W/os/gate-rs/libgate_proved.a" ] || die "the proved gate (Rust) did not build: is 'rustc +stable' there?"
say "sakshi: $(stat -c %s "$W/os/sakshi/sakshi") bytes, sealed: $(python3 - "$W/os/sakshi/sakshi" <<'PY'
import sys
b = open(sys.argv[1], 'rb').read(); i = b.find(b'TURIYA-SEAL-v1\0\0')
print('yes, ' + b[i + 16:i + 24].hex() + '...' if i >= 0 and any(b[i + 16:i + 48]) else 'NO')
PY
)"
sh "$W/os/stick/build_usas.sh" >> "$LOG" 2>&1 || die "Usas or the stick image did not build (see $LOG)"
IMG=$W/os/stick/turiya-usas.img
say "image: $IMG"
say "       sha256 $(sha256sum "$IMG" | cut -d' ' -f1)"
say "       Usas $(sha256sum "$W/os/stick/USAS.EFI" | cut -c1-16)..., the owner's file \\TURIYA\\OWNER.TXT (a sample)"

# ---------------------------------------------------------------- 4. checks
step "4. checks"
T=$W/tests; mkdir -p "$T"
host() {   # host <name> <command...>
	local name=$1; shift
	if out=$("$@" 2>&1); then say "  ok    $name: $(printf '%s\n' "$out" | tail -1)"
	else say "  FAIL  $name"; printf '%s\n' "$out" >> "$LOG"; HOSTFAIL=1; fi
}
HOSTFAIL=0
cd "$W/os/sakshi/tests"
host "gate rule (kartr_rule.h)"      sh -c "g++ -std=c++20 -O2 -Wall -Wextra -Werror -I.. test_kartr_rule.cc -o $T/kartr && $T/kartr"
host "owner's list (owner_config.h)" sh -c "g++ -std=c++20 -O2 -Wall -Wextra -Werror -Weffc++ -I.. test_owner_config.cc -o $T/owner && $T/owner"
host "clock (kala.h)"                sh -c "g++ -std=c++20 -O2 -Wall -Wextra -Werror -Weffc++ -I.. test_kala.cc -o $T/kala && $T/kala"
host "build seal (seal.py)"          python3 test_seal.py ../sakshi
cd "$W/os/usas"
host "Usas owner area (owner.rs)"    sh -c "rustc +stable --edition 2021 --test src/owner.rs -o $T/usas_owner && $T/usas_owner"
cd "$W"
[ "$HOSTFAIL" = 0 ] || die "a host test failed (see $LOG)"
if [ "${SKIP_SUITE:-}" = 1 ]; then
	say "emulator suite skipped (SKIP_SUITE=1): os/tools/emulator_suite.sh $IMG"
else
	bash "$W/os/tools/emulator_suite.sh" "$IMG" "$W/suite" 2>&1 | tee -a "$LOG" | grep -E "^(==|   FAIL|emulator suite)" || true
	grep -q "emulator suite: [0-9]* passed, 0 failed" "$LOG" || die "the emulator suite did not pass (logs in $W/suite)"
fi
say ""
say "bootstrap done in $(( ($(date +%s) - t0) / 60 )) min: $IMG"
