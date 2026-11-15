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

One source patch, `sel4-mb2-framebuffer-tag.patch`: three data words in the multiboot2 header (assembly, outside
the proven C code) asking the boot loader to pass the firmware's screen on. No kernel logic changes.
