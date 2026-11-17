#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# Turiya emulator suite. Boots a Turiya stick image in QEMU with UEFI firmware (OVMF), types commands the way a
# keyboard would (QEMU's monitor, sendkey) and checks Sakshi's serial log for what each step must say.
# Every scenario is a fresh boot. Every disk is attached with snapshot=on or readonly=on: no image file changes.
#
#   os/tools/emulator_suite.sh [turiya-usas.img] [work dir] [scenario ...]
#
# The image defaults to os/stick/turiya-usas.img (os/stick/build_usas.sh). With scenario names, only those run
# (boot states falls round storage owner poweroff). Exit status 0 when every check passed; each scenario's serial
# log stays in the work dir. Needs qemu-system-x86_64, OVMF, python3, mtools and sgdisk. OVMF_CODE and OVMF_VARS
# may name the firmware files when they are not in a usual place.
set -u
here=$(cd "$(dirname "$0")" && pwd)
IMG=$(readlink -f "${1:-$here/../stick/turiya-usas.img}")
W=${2:-}
[ -n "$W" ] || W=$(mktemp -d /tmp/turiya-suite.XXXXXX)
mkdir -p "$W"; W=$(readlink -f "$W")
shift $(( $# < 2 ? $# : 2 ))
ONLY=" $* "

die() { echo "emulator_suite: $*" >&2; exit 2; }
[ -f "$IMG" ] || die "no image at $IMG (build it with os/stick/build_usas.sh)"
for t in qemu-system-x86_64 python3 mcopy mdel mformat mmd sgdisk; do command -v $t >/dev/null || die "needs $t"; done
CODE=${OVMF_CODE:-}; VARS=${OVMF_VARS:-}
for c in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/edk2/ovmf/OVMF_CODE.fd \
         /usr/share/edk2/x64/OVMF_CODE.4m.fd /usr/share/edk2-ovmf/x64/OVMF_CODE.fd; do
	[ -n "$CODE" ] || { [ -f "$c" ] && CODE=$c; }
done
[ -n "$CODE" ] && [ -f "$CODE" ] || die "no OVMF firmware found (set OVMF_CODE and OVMF_VARS)"
if [ -z "$VARS" ]; then
	for v in "${CODE/CODE/VARS}" "$(dirname "$CODE")/OVMF_VARS_4M.fd" "$(dirname "$CODE")/OVMF_VARS.fd"; do
		[ -f "$v" ] && { VARS=$v; break; }
	done
fi
[ -n "$VARS" ] && [ -f "$VARS" ] || die "no OVMF variable store found (set OVMF_VARS)"
OFF=$((2048 * 512))      # the stick's one partition (the ESP) starts at sector 2048

# ---- the keyboard and the monitor, through QEMU's monitor socket
cat > "$W/kbd.py" <<'PY'
import socket, sys, time
M = {' ': 'spc', '.': 'dot', '-': 'minus', '/': 'slash', '\n': 'ret', ',': 'comma', ';': 'semicolon', '=': 'equal'}
S = {'@': 'shift-2', '_': 'shift-minus', '!': 'shift-1', ':': 'shift-semicolon', '?': 'shift-slash', '|': 'shift-backslash'}
sock, what, arg = sys.argv[1], sys.argv[2], sys.argv[3]
s = socket.socket(socket.AF_UNIX); s.settimeout(2)
try:
    s.connect(sock)
except OSError:
    sys.exit(0)                       # QEMU has already gone (after 'poweroff')
time.sleep(0.3)
try: s.recv(65536)
except OSError: pass
if what == 'keys':
    for ch in arg.replace('\\n', '\n'):
        k = 'shift-' + ch.lower() if ch.isupper() else M.get(ch) or S.get(ch) or ch
        s.send(('sendkey %s\n' % k).encode()); time.sleep(0.12)
else:
    s.send((arg + '\n').encode())
time.sleep(1)
try: s.recv(65536)
except OSError: pass
s.close()
PY

# ---- one scenario: boot, type the steps ("keys|seconds"; "!command" goes to the monitor), stop QEMU
QEMU_LEFT=0
run() {     # run <name> <image> <extra qemu arguments> <steps>
	local name=$1 img=$2 extra=$3 steps=$4 sock=$W/$1.sock i
	CUR=$name; LOG=$W/$name.log; DAY=$(date -u +%Y-%m-%d)
	rm -f "$LOG" "$sock"; cp "$VARS" "$W/vars.fd"
	echo "== $name"
	# shellcheck disable=SC2086
	qemu-system-x86_64 -display none -serial file:"$LOG" -monitor unix:"$sock",server,nowait -m 1024 \
		-machine pc -cpu Skylake-Client,+pdpe1gb -vga std -net none \
		-drive if=pflash,format=raw,readonly=on,file="$CODE" -drive if=pflash,format=raw,file="$W/vars.fd" \
		-drive format=raw,file="$img",snapshot=on $extra > "$W/$name.qemu" 2>&1 &
	local qpid=$!
	for i in $(seq 1 120); do sleep 1; grep -q "Turiya: ready" "$LOG" 2>/dev/null && break; done
	sleep 2
	while IFS='|' read -r keys secs; do
		case "$keys" in
			'') ;;
			'!'*) python3 "$W/kbd.py" "$sock" mon "${keys#!}" ;;
			*) python3 "$W/kbd.py" "$sock" keys "$keys" ;;
		esac
		sleep "${secs:-2}"
	done <<< "$steps"
	if kill -0 $qpid 2>/dev/null; then QEMU_LEFT=1; python3 "$W/kbd.py" "$sock" mon quit; else QEMU_LEFT=0; fi
	sleep 1; kill $qpid 2>/dev/null; wait $qpid 2>/dev/null
	# every scenario: it started, the two judges never disagreed, nothing fell that must not
	has "Turiya: ready" "Turiya started (serial says ready)"
	hasnt "the two judges disagree" "the two judges never disagreed"
	hasnt "Sakshi.s own code changed while running|Sakshi.s own code has changed" "Sakshi's own code stayed as built"
	hasnt "could not be made" "every organ could be made"
}

pass=0; fail=0; FAILS=""
ok()   { pass=$((pass + 1)); printf '   ok    %s\n' "$1"; }
bad()  { fail=$((fail + 1)); FAILS="$FAILS
  $CUR: $1"; printf '   FAIL  %s\n' "$1"; }
has()  { if grep -qE -- "$1" "$LOG" 2>/dev/null; then ok "${2:-$1}"; else bad "${2:-$1}"; fi; }
hasnt(){ if grep -qE -- "$1" "$LOG" 2>/dev/null; then bad "${2:-not: $1}"; else ok "${2:-not: $1}"; fi; }
count(){ local n; n=$(grep -cE -- "$1" "$LOG" 2>/dev/null); n=${n:-0}
         if [ "$n" -ge "$2" ]; then ok "${3:-$1} ($n)"; else bad "${3:-$1} ($n, want at least $2)"; fi; }
want() { [ "$ONLY" = "  " ] || [[ "$ONLY" == *" $1 "* ]]; }

# ---- 1. boot: the build seal, the owner's file, the gate twice, the clock, an approval
if want boot; then
run boot "$IMG" "" 'gate\n|8
time\n|3
owner\n|3
summarise newsletter\n|3
approve\n|3
check\n|10'
has "own code +[0-9]+ bytes, matches its build seal" "own code matches its build seal"
has "owner +4 known targets \(the owner.s file" "the owner's file gave 4 known targets"
has "kartr_gate: 4 allowed, 11 held; .*agreed 15 of 15" "boot gate: 4 allowed, 11 held, judges 15/15"
has "kartr_gate: 4 allowed, 11 held; .*agreed 30 of 30" "gate again: same verdicts, judges 30/30"
has "Kala, the clock  (Sun|Mon|Tues|Wednes|Thurs|Fri|Satur)day [0-9]{1,2} [A-Z][a-z]+ 20[0-9]{2}, [0-2][0-9]:[0-5][0-9]:[0-5][0-9]" "time: a full date and time"
has "century register 0x32 says 20" "time: the FADT's century register read, says 20"
has "read twice alike, 1[68] register reads" "time: two equal snapshots, 16 or 18 register reads"
has "lines +8: 4 comments, 4 taken, 0 already listed, 0 past the limit of 16, 0 not understood" "owner: every line accounted for"
has "held_target_not_known: summarise newsletter" "an unknown target is held"
has "allowed_with_approval: summarise newsletter" "...and the owner can approve exactly it"
has "clock $DAY [0-2][0-9]:[0-5][0-9]:[0-5][0-9]" "check: the clock gives today's date (QEMU keeps UTC)"
has "judges agreed ([0-9]+)/\1;" "check: the judges agreed on every decision"
fi

# ---- 2. the four states: dreaming and deep sleep hold, waking acts; an irreversible act needs approval
if want states; then
run states "$IMG" "" 'state svapna\n|2
draft_reply papa\n|2
state susupti\n|2
summarise inbox\n|2
state jagrat\n|2
draft_reply papa\n|2
pay ravi@ybl 100\n|2
approve\n|3
log\n|3'
has "held_while_dreaming: draft_reply papa" "dreaming: an act is held as a plan"
has "held_while_asleep: summarise inbox" "deep sleep: nothing acts"
has "^  allowed: draft_reply papa" "waking: a known target is allowed"
has "held_needs_approval: pay ravi@ybl 100" "an irreversible act needs approval"
has "allowed_with_approval: pay ravi@ybl 100" "...and is allowed with it"
has "witness log: 2[0-9] decisions this session" "the witness log kept every decision"
fi

# ---- 3. falls: an organ that touches what it does not hold is re-made; a silent heart is seen by the breath
if want falls; then
run falls "$IMG" "" 'crash hrdaya\n|4
stop hrdaya\n|8
crash tvak\n|4
abc\n|2
crash tvak\n|3
crash tvak\n|3
crash tvak\n|4
abc\n|2
heal tvak\n|4
organs\n|3'
has "Tvak fell: it touched memory it does not hold" "Tvak fell and Sakshi saw why"
has "Hrdaya fell: it touched memory it does not hold" "Hrdaya fell and Sakshi saw why"
has "Hrdaya stopped: no beat for 2 seconds, seen by the breath" "a silent heart is seen by the breath"
has "blocked_outside_mandate: abc" "keys still reach Sakshi after Tvak was re-made"
count "re-made from its seed \(SHA-256 checked\)" 3 "organs re-made from their checked seeds"
has "Tvak rests: it fell 4 times within 30 seconds" "an organ that falls 4 times in 30 seconds rests"
has "Sakshi feels the keyboard itself \(reflex\)" "...and Sakshi feels the keyboard itself"
has "Tvak healed: awake again" "heal wakes it again"
fi

# ---- 4. the daily round: a changed read-only page is found and the organ re-made; Sakshi's own code checked
if want round; then
run round "$IMG" "" 'sweep\n|5
taint tvak\n|3
sweep\n|5
taint hrdaya\n|3
|70
check\n|8'
has "Tvak stopped: its read-only page at 0x[0-9a-f]+ had changed \(found by the daily round\)" "sweep found the changed Tvak page"
has "Hrdaya stopped: its read-only page at 0x[0-9a-f]+ had changed \(found by the daily round\)" "the round on its own found the changed Hrdaya page"
has "daily round [0-9]+: [0-9]+ organs, [0-9]+ read-only pages and Sakshi.s own [0-9]+ bytes re-read" "a round re-reads organs and Sakshi's own code"
has "nothing changed; Sakshi.s own code matches its build seal" "a round with nothing changed says so"
has "daily rounds [1-9][0-9]*, changes 2; own code matches its build seal" "check: rounds ran, 2 changes, own code as built"
fi

# ---- 5. storage and USB: the SSD's partition table (read-only), a USB stick's files, a USB keyboard
if want storage; then
	rm -f "$W/nvme.img" "$W/stick.img"
	truncate -s 64M "$W/nvme.img"
	sgdisk -o -n 1:2048:+16M -t 1:ef00 -c 1:EFI -n 2:0:0 -t 2:0700 -c 2:DATA "$W/nvme.img" > /dev/null
	truncate -s 64M "$W/stick.img"
	sgdisk -o -n 1:2048:0 -t 1:0700 -c 1:STICK "$W/stick.img" > /dev/null
	mformat -i "$W/stick.img@@$OFF" -F -v STICK ::
	printf 'Turiya reads this stick and never writes it.\n' > "$W/hello.txt"
	mcopy -i "$W/stick.img@@$OFF" "$W/hello.txt" ::/HELLO.TXT
	mmd -i "$W/stick.img@@$OFF" ::/Photos
	mcopy -i "$W/stick.img@@$OFF" "$W/hello.txt" "::/Photos/Notes for later.txt"
	DISKS="-drive file=$W/nvme.img,if=none,id=nv,format=raw,readonly=on -device nvme,drive=nv,serial=t1 \
	       -device qemu-xhci,id=xhci -drive file=$W/stick.img,if=none,id=st,format=raw,readonly=on \
	       -device usb-storage,bus=xhci.0,drive=st"
run storage "$IMG" "$DISKS" 'disk\n|8
usb\n|10
stick\n|6
files\n|12
files Photos\n|6
read HELLO.TXT\n|6
!device_add usb-kbd,bus=xhci.0,id=kb|5
time\n|4'
has "partitions \(GPT\):" "disk: the SSD's GPT read (Kosa, read-only)"
has "DATA" "disk: the second partition's name"
has "HELLO.TXT" "files: the stick's top folder (Smrti)"
has "Notes for later.txt" "files Photos: a long name in a folder"
has "Turiya reads this stick and never writes it" "read: the start of a file, as text"
has "Kala, the clock" "a USB keyboard typed 'time' (Nadi)"
fi

# ---- 6. the owner's file: none, lines it must not take, too large, and started by GRUB (no file handed over)
if want owner; then
	cp "$IMG" "$W/o_none.img"; mdel -i "$W/o_none.img@@$OFF" ::/TURIYA/OWNER.TXT
	python3 - "$W" <<'PY'
import sys
w = sys.argv[1]
L = ["# written as Notepad writes it, CRLF", "known: papa", "known: papa", "Known: someone", "known: two words",
     "known: a|b", "known:", "   \t  "] + ["known: n%02d" % i for i in range(2, 19)]
open(w + "/owner_lines.txt", "wb").write(("\r\n".join(L) + "\r\n").encode())
open(w + "/owner_large.txt", "wb").write(b"known: papa\n" + b"#" * 4990 + b"\n")
PY
	cp "$IMG" "$W/o_lines.img"; mcopy -o -i "$W/o_lines.img@@$OFF" "$W/owner_lines.txt" ::/TURIYA/OWNER.TXT
	cp "$IMG" "$W/o_large.img"; mcopy -o -i "$W/o_large.img@@$OFF" "$W/owner_large.txt" ::/TURIYA/OWNER.TXT
	cp "$IMG" "$W/o_grub.img"; mcopy -i "$IMG@@$OFF" ::/EFI/GRUB/GRUBX64.EFI "$W/grubx64.efi"
	mcopy -o -i "$W/o_grub.img@@$OFF" "$W/grubx64.efi" ::/EFI/BOOT/BOOTX64.EFI
	STEPS='owner\n|3
summarise inbox\n|3
approve\n|3'
	run owner_none "$W/o_none.img" "" "$STEPS"
	has "no \\\\TURIYA\\\\OWNER.TXT on this stick" "Usas: no owner's file on the stick"
	has "owner +no known target: every named target other than the sender needs you" "Sakshi: no known target"
	has "kartr_gate: 2 allowed, 13 held; .*agreed 15 of 15" "default-deny: 2 allowed, 13 held, judges 15/15"
	has "held_target_not_known: summarise inbox" "summarise inbox is held"
	has "allowed_with_approval: summarise inbox" "...until the owner approves exactly it"
	run owner_lines "$W/o_lines.img" "" "$STEPS"
	has "lines +25: 2 comments, 16 taken, 1 already listed, 2 past the limit of 16, 4 not understood" "every line in exactly one count"
	has "known +papa  n02  n03 .* n16" "the 16 names, in order"
	has "16 known targets .*; 4 lines not understood, see 'owner'" "the boot line says lines were not understood"
	run owner_large "$W/o_large.img" "" "$STEPS"
	has "OWNER.TXT is over 4096 bytes: not used" "Usas: a file over 4096 bytes is not used"
	has "kartr_gate: 2 allowed, 13 held" "...and the list stays empty"
	run owner_grub "$W/o_grub.img" "" "$STEPS"
	has "not started by Usas, so no owner's file was read" "GRUB start: the area is untouched, the list empty"
	has "kartr_gate: 2 allowed, 13 held" "...default-deny"
fi

# ---- 7. power off: ACPI S5 from the FADT and the DSDT's \_S5
if want poweroff; then
run poweroff "$IMG" "" 'poweroff\n|10'
has "powering off" "poweroff: Turiya says so"
if [ "$QEMU_LEFT" = 0 ]; then ok "the machine switched itself off"; else bad "the machine switched itself off"; fi
fi

echo
echo "emulator suite: $pass passed, $fail failed (logs in $W)"
[ "$fail" = 0 ] || { printf 'failed:%s\n' "$FAILS"; exit 1; }
exit 0
