# Phase 1b — putting Turiya's core on the verified seL4 kernel

Written 12 Nov 2026 (evening). Sources at the end.

## Where things stand
| Question | Answer (12 Nov 2026) |
|---|---|
| Does Sculpt run on seL4? | Yes, but **unofficial and experimental**: Alexander Boettcher (Genode Labs) publishes multi-kernel Sculpt images; the 26.04 one is `sculpt_26_04_multi_kernel.img` (SHA-256 2c810ba92183038d6ffc282218638b110cd4a79d7a3032754631e38b85e03dc6). The seL4 build is based on seL4 13.0. You pick seL4 from the GRUB menu at boot; the active kernel shows on the Leitzentrale background. |
| Official Sculpt 26.10 kernel | Genode's own microkernel (base-hw), not seL4. |
| Hardware the seL4 variant was tested on | Two Lenovo ThinkPads (T460p, T490). No AMD testing mentioned. |
| What seL4's proofs cover on **x86-64 (this laptop)** | Functional correctness from the abstract spec down to C, but **not** the fast path, VT-x, VT-d, binary (compiler) verification, or the security proofs. |
| Where the proofs are strongest | ARM and RISC-V: compiled-binary correctness on ARMv7 and 64-bit RISC-V; on 64-bit ARM, functional correctness, integrity and confidentiality are proven. seL4 16.0.0 (22 Jul 2026) added an MCS proof for 64-bit RISC-V. |

## What this means for Turiya
- On this x86 laptop the "proven core" claim is **partial**: the C code is proven, the compiled binary and the security properties are not. The design doc should say so.
- The strongest proofs are on ARM, which is what phones use. Turiya's phone (Phase 3) may end up with a *stronger* proven core than the laptop. That fits "no chip trusted by name": the proof level depends on the architecture, and Turiya should show it, not hide it.
- Practical order: Phase 1 on Sculpt 26.10 in VirtualBox (official, easy), then try the 26.04 multi-kernel image on seL4 in the same VM (Phase 1b), and record what works.

## Steps for Phase 1b (after Phase 1's VirtualBox install)
1. Download `sculpt_26_04_multi_kernel.img` from depot.genode.org/alex-ab/images/ into `phase1/images/`; check the SHA-256 above.
2. Convert to a VirtualBox disk (`VBoxManage convertfromraw sculpt_26_04_multi_kernel.img sculpt_mk.vdi`) and attach to a new VM (q35-like chipset, 4 GB, 2 CPUs, e1000 network).
3. Boot, choose the seL4 entry in GRUB, and record: does it reach the Leitzentrale, which components start, what fails.
4. Gate 1b: Sculpt on seL4 boots in the VM, and a small Turiya component (the Kartṛ log) runs on it.

## Sources
- https://genodians.org/alex-ab/2025-11-02-sculpt-multi-kernel (multi-kernel images, seL4 13.0 base, tested machines)
- https://lists.sel4.systems/hyperkitty/list/announce@sel4.systems/thread/3LB7ZZUFFDANOUT54QBS7WNO2FWCZUQG/ (x64 functional-correctness proof and its exclusions)
- https://trustworthy.systems/projects/seL4-verification/ (binary correctness on ARMv7 and RISC-V 64)
- https://www.sel4.systems/news/ (AArch64 confidentiality proof)
- https://docs.sel4.systems/releases/sel4/16.0.0 (seL4 16.0.0, 22 Jul 2026)
- https://genode.org/news/2026 (Sculpt 26.10 uses Genode's own kernel by default)
