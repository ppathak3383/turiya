# seL4 kernel for Turiya (x86-64)

seL4 13.0.0, built on its own (no Genode) from `configs/X64_verified.cmake` with these changes:

| Option | Value | Why |
|---|---|---|
| KernelVerificationBuild | OFF | needed for debug output while we bring hardware up |
| KernelPrinting, KernelDebugBuild | ON | serial log; Sakshi also draws on screen |
| KernelFSGSBase | msr | works on CPUs and VMs without the FSGSBASE instructions |
| KernelXSave / KernelXSaveSize | XSAVE / 1024 | AMD Ryzen + VirtualBox (XSAVEOPT hidden, larger save area) |
| KernelSupportPCID | OFF | runs on CPUs and emulators without PCID |
| KernelMaxNumBootinfoUntypedCaps | 230 | room for the screen's device memory |
| KernelSkimWindow | OFF | AMD is not affected by Meltdown |

Two source patches, both in boot code that runs once:

| Patch | What | Why |
|---|---|---|
| `sel4-mb2-framebuffer-tag.patch` | three data words in the multiboot2 header (assembly) | asks the loader to pass the firmware's screen on |
| `sel4-elf-load-only-pt-load.patch` | the root-task ELF loader skips program headers that are not `PT_LOAD` | seL4 13's x86-64 loader treated every header as loadable. The `PT_TLS` header (file size 0, memory size 8 for one `__thread` variable) made it zero the first 8 bytes of the root task's `.data`. Found on 15 Nov when the first organ's name pointer read as null. |

No change to the kernel's run-time logic.
