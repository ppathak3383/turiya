# seL4 kernel for Turiya (x86-64)

seL4 13.0.0: [github.com/seL4/seL4](https://github.com/seL4/seL4) at commit `cd6d3b8c25d49be2b100b0608cf0613483a6fffa`
("recreate 13.0.0 release state", tag 13.0.0), built on its own from `configs/X64_verified.cmake` with the options
below. `turiya_x64.cmake` holds every kernel option of the build (63) and is used as the initial cache;
`../tools/bootstrap.sh` fetches the source, applies the three patches, builds, and compares the configuration it got
with that file, option by option.

| Option | Value | Why |
|---|---|---|
| KernelVerificationBuild | OFF | needed for debug output while we bring hardware up |
| KernelPrinting, KernelDebugBuild | ON | serial log; Sakshi also draws on screen |
| KernelFSGSBase | msr | works on CPUs and VMs without the FSGSBASE instructions |
| KernelXSave / KernelXSaveSize | XSAVE / 1024 | AMD Ryzen + VirtualBox (XSAVEOPT hidden, larger save area) |
| KernelSupportPCID | OFF | runs on CPUs and emulators without PCID |
| KernelMaxNumBootinfoUntypedCaps | 230 | room for the screen's device memory |
| KernelSkimWindow | OFF | AMD is not affected by Meltdown |

Three patches, applied with `patch -p1` from the seL4 source root. They touch boot code and one interrupt rule:

| Patch | What | Why |
|---|---|---|
| `sel4-mb2-framebuffer-tag.patch` | three data words in the multiboot2 header (assembly) | asks the loader to pass the firmware's screen on |
| `sel4-elf-load-only-pt-load.patch` | the root-task ELF loader skips program headers that are not `PT_LOAD` | seL4 13's x86-64 loader treated every header as loadable. The `PT_TLS` header (file size 0, memory size 8 for one `__thread` variable) made it zero the first 8 bytes of the root task's `.data`. Found on 15 Nov when the first organ's name pointer read as null. |
| `sel4-carried-from-first-tree.patch` | the kernel image at physical 2 MiB (not 1 MiB); an edge-triggered IOAPIC line is never masked, level-triggered lines are masked as before | kept from the seL4 13.0.0 tree Turiya was first built from (the copy in Genode's seL4 port), so the kernel stays the one every test and every laptop check ran on. Not masking an edge line means no edge is lost while Sakshi has not yet acknowledged the one before; the heart, the breath and the keyboard are all edge-triggered. |

No change to the kernel's run-time logic beyond the interrupt-masking rule above.

## Same kernel, now from public sources (17 Nov 2026)

Until 16 Nov the kernel was built from that first tree, which carried twelve patches from Genode's seL4 port. For
this x86-64 configuration only two of them change the compiled kernel; they are the third patch above. The others
touch ARM, Intel VT-x (`KernelVTX` is OFF) or benchmarking (`KernelBenchmarks` is none). Checked: both trees,
built in the same directory with the same compiler, give the same preprocessed kernel source once file paths and
the line numbers inside debug assertion messages are set aside (the only other difference: six blank lines). The
kernel binaries differ only because of those strings (their text, and where the data after them lands). From image
`m` on, the published kernel is the one built from upstream 13.0.0 and these three patches.
