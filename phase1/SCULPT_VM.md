# Phase 1 — Sculpt OS in a VM on the HP Pavilion 14 (Windows 11, AMD, 16 GB)

Written 12 Nov 2026 from https://genode.org/download/sculpt and https://genode.org/news/2026.

## What we're running
- Sculpt OS 26.10 (released 30 Oct 2026). Disk image `sculpt-26-10.img` (41 MiB); VirtualBox appliance `sculpt-26-10.ova` with a known-to-work VirtualBox configuration.
- **Kernel note:** 26.10 is the first Sculpt release that uses Genode's own microkernel by default. That kernel is NOT the formally verified seL4. Plan: learn and build Turiya's components on Sculpt as shipped (Phase 1), then build the core on Genode's seL4 target (Phase 1b), which is what the design's Sākṣī core needs. Flagged in the design doc.

## Steps (one Windows action needs Arjun; everything after can be done by Claude)
1. **Arjun (one time, ~10 min):** install VirtualBox 7 for Windows from virtualbox.org. (Downloading and running an installer is a step Claude does not take on its own.)
2. Download `sculpt-26-10.ova` (and `sculpt-26-10.ova.asc`) into `LocalAgent\Turiya\phase1\images\`.
3. Verify: SHA-256 must equal `013487c42902cd1ec48725b95d78ad6817cfdb537c375e0f30ce0ce444c22474`; then check the signature against the Genode public key (Codeberg: `repos/gems/sculpt/depot/genodelabs/pubkey`) with GnuPG.
4. VirtualBox → File → Import Appliance → the .ova. Keep the shipped settings. Give it 4 GB RAM and 2 CPUs (leaves room for Gemma).
5. Boot. Expand the single AHCI boot disk from Sculpt's administrative interface before use (the download page says to).
6. Gate for Phase 1: Sculpt boots in the VM, network works, and a "hello" component built with the Goa SDK (which now runs natively on Sculpt 26.10) starts and is visible in the component graph.

## Alternative without VirtualBox
QEMU is the other documented route (`qemu-system-x86_64 ... -m 1024 -drive format=raw,file=sculpt-26-10.img ...`), but on Windows it needs QEMU installed and runs without KVM acceleration, so VirtualBox is the practical choice on this laptop.

## Open
- AMD: the 26.10 announcement names no CPU vendor; it says the Linux-based drivers moved from kernel 6.12 to 6.18 for wider modern-hardware support. Inside a VM the CPU vendor matters little; it matters again at Phase 2 (bare metal).
