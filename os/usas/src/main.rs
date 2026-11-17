// SPDX-License-Identifier: AGPL-3.0-or-later
//! Usas (the dawn): Turiya's start-up code, written from zero.
//!
//! A UEFI application that carries seL4 and Sakshi inside itself and:
//!   1. sets the screen to its best mode (UEFI GOP) and notes where it is,
//!   2. finds the ACPI tables (RSDP) the firmware publishes,
//!   3. lays out seL4 at its physical address and Sakshi as a boot module above it,
//!   4. converts the firmware's memory map into multiboot2 form (merged, at most 15 free regions),
//!   5. reads the owner's file, \TURIYA\OWNER.TXT on this stick (opened read-only), into Sakshi's image,
//!   6. leaves the firmware (ExitBootServices) and never calls it again,
//!   7. switches the processor from 64-bit long mode to 32-bit protected mode without paging,
//!      copies the images into place, and enters seL4 the way a multiboot2 loader does.
//! No libraries, no crates: only the UEFI specification and the multiboot2 specification.

#![no_std]
#![no_main]

mod owner;

use core::arch::global_asm;
use core::fmt::Write;
use core::panic::PanicInfo;

static KERNEL: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/payload/sel4.elf"));
static ROOT: &[u8] = include_bytes!(concat!(env!("CARGO_MANIFEST_DIR"), "/payload/sakshi"));

// ---------------------------------------------------------------- UEFI, only what we use
type Handle = *mut u8;
type Status = usize;
const EFI_SUCCESS: Status = 0;

#[repr(C)]
#[derive(PartialEq, Eq, Clone, Copy)]
struct Guid(u32, u16, u16, [u8; 8]);

const GOP_GUID: Guid = Guid(0x9042a9de, 0x23dc, 0x4a38, [0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a]);
const ACPI20_GUID: Guid = Guid(0x8868e871, 0xe4f1, 0x11d3, [0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81]);
const ACPI10_GUID: Guid = Guid(0xeb9d2d30, 0x2d88, 0x11d3, [0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d]);

#[repr(C)]
struct TableHeader { signature: u64, revision: u32, header_size: u32, crc32: u32, reserved: u32 }

#[repr(C)]
struct TextOut {
    reset: usize,
    output_string: extern "efiapi" fn(*mut TextOut, *const u16) -> Status,
    test_string: usize,
    query_mode: usize,
    set_mode: usize,
    set_attribute: usize,
    clear_screen: extern "efiapi" fn(*mut TextOut) -> Status,
}

#[repr(C)]
struct ConfigTable { guid: Guid, table: *const u8 }

#[repr(C)]
pub struct SystemTable {
    hdr: TableHeader,
    firmware_vendor: *const u16,
    firmware_revision: u32,
    con_in_handle: Handle,
    con_in: usize,
    con_out_handle: Handle,
    con_out: *mut TextOut,
    std_err_handle: Handle,
    std_err: usize,
    runtime_services: usize,
    boot_services: *mut BootServices,
    n_config: usize,
    config: *const ConfigTable,
}

#[repr(C)]
struct BootServices {
    hdr: TableHeader,
    raise_tpl: usize,
    restore_tpl: usize,
    allocate_pages: extern "efiapi" fn(u32, u32, usize, *mut u64) -> Status,
    free_pages: usize,
    get_memory_map: extern "efiapi" fn(*mut usize, *mut u8, *mut usize, *mut usize, *mut u32) -> Status,
    allocate_pool: usize,
    free_pool: usize,
    create_event: usize,
    set_timer: usize,
    wait_for_event: usize,
    signal_event: usize,
    close_event: usize,
    check_event: usize,
    install_protocol_interface: usize,
    reinstall_protocol_interface: usize,
    uninstall_protocol_interface: usize,
    handle_protocol: extern "efiapi" fn(Handle, *const Guid, *mut *mut u8) -> Status,
    reserved: usize,
    register_protocol_notify: usize,
    locate_handle: usize,
    locate_device_path: usize,
    install_configuration_table: usize,
    load_image: usize,
    start_image: usize,
    exit: usize,
    unload_image: usize,
    exit_boot_services: extern "efiapi" fn(Handle, usize) -> Status,
    get_next_monotonic_count: usize,
    stall: extern "efiapi" fn(usize) -> Status,
    set_watchdog_timer: extern "efiapi" fn(usize, u64, usize, *const u16) -> Status,
    connect_controller: usize,
    disconnect_controller: usize,
    open_protocol: usize,
    close_protocol: usize,
    open_protocol_information: usize,
    protocols_per_handle: usize,
    locate_handle_buffer: usize,
    locate_protocol: extern "efiapi" fn(*const Guid, *const u8, *mut *mut u8) -> Status,
}

#[repr(C)]
struct Gop {
    query_mode: extern "efiapi" fn(*mut Gop, u32, *mut usize, *mut *const GopModeInfo) -> Status,
    set_mode: extern "efiapi" fn(*mut Gop, u32) -> Status,
    blt: usize,
    mode: *const GopMode,
}

#[repr(C)]
struct GopMode { max_mode: u32, mode: u32, info: *const GopModeInfo, size_of_info: usize, fb_base: u64, fb_size: usize }

#[repr(C)]
struct GopModeInfo { version: u32, width: u32, height: u32, pixel_format: u32, masks: [u32; 4], pixels_per_line: u32 }

// the stick Usas was started from: its loaded image names the device, the device has a file system
const LOADED_IMAGE_GUID: Guid = Guid(0x5b1b31a1, 0x9562, 0x11d2, [0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b]);
const SIMPLE_FS_GUID: Guid = Guid(0x964e5b22, 0x6459, 0x11d2, [0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b]);
const FILE_MODE_READ: u64 = 1;
const EFI_NOT_FOUND: Status = 14 | (1usize << 63);

#[repr(C)]
#[allow(dead_code)]
struct LoadedImage { revision: u32, parent: Handle, system_table: *mut SystemTable, device: Handle } // the rest unused

#[repr(C)]
#[allow(dead_code)]
struct SimpleFs { revision: u64, open_volume: extern "efiapi" fn(*mut SimpleFs, *mut *mut File) -> Status }

#[repr(C)]
#[allow(dead_code)]
struct File {
    revision: u64,
    open: extern "efiapi" fn(*mut File, *mut *mut File, *const u16, u64, u64) -> Status,
    close: extern "efiapi" fn(*mut File) -> Status,
    delete: usize, // never called: Usas only reads
    read: extern "efiapi" fn(*mut File, *mut usize, *mut u8) -> Status,
    write: usize,  // never called
}

const OWNER_PATH: [u16; 18] = {
    let s = b"\\TURIYA\\OWNER.TXT\0";
    let mut a = [0u16; 18];
    let mut i = 0;
    while i < 18 { a[i] = s[i] as u16; i += 1; }
    a
};

// memory types and allocation kinds
const ALLOCATE_MAX_ADDRESS: u32 = 1;
const LOADER_CODE: u32 = 1;
const LOADER_DATA: u32 = 2;
const CONVENTIONAL: u32 = 7;

// ---------------------------------------------------------------- console
static mut CON: *mut TextOut = core::ptr::null_mut();

struct Con;
impl Write for Con {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let mut buf = [0u16; 160];
        let mut n = 0;
        for b in s.bytes() {
            if n >= buf.len() - 3 {
                buf[n] = 0;
                unsafe { ((*CON).output_string)(CON, buf.as_ptr()) };
                n = 0;
            }
            if b == b'\n' { buf[n] = b'\r' as u16; n += 1; }
            buf[n] = b as u16;
            n += 1;
        }
        buf[n] = 0;
        unsafe { if !CON.is_null() { ((*CON).output_string)(CON, buf.as_ptr()); } }
        Ok(())
    }
}

macro_rules! say { ($($a:tt)*) => { let _ = write!(Con, $($a)*); } }

// ---------------------------------------------------------------- small helpers
fn rd16(b: &[u8], o: usize) -> u16 { u16::from_le_bytes([b[o], b[o + 1]]) }
fn rd32(b: &[u8], o: usize) -> u32 { u32::from_le_bytes([b[o], b[o + 1], b[o + 2], b[o + 3]]) }
fn rd64(b: &[u8], o: usize) -> u64 { (rd32(b, o) as u64) | ((rd32(b, o + 4) as u64) << 32) }
fn align_up(v: u64, a: u64) -> u64 { (v + a - 1) & !(a - 1) }
fn overlaps(a0: u64, a1: u64, b0: u64, b1: u64) -> bool { a0 < b1 && b0 < a1 }

struct Fail(&'static str);

// ---------------------------------------------------------------- the trampoline
// Copied to a page below 4 GiB and called with the parameter block in rcx (UEFI/MS calling
// convention). It runs after ExitBootServices: switches to 32-bit compatibility mode, turns paging
// off (which leaves long mode), clears EFER.LME and CR4, copies the kernel and the boot module into
// place (now nothing of the firmware's is needed, not even its page tables), and jumps to seL4 with
// the multiboot2 magic in EAX and the boot information in EBX.
//
// Parameter block: +0x00 stack top (u64) | +0x08 GDTR (limit u16, base u64) | +0x20 kernel src,
// +0x24 dst, +0x28 len | +0x2c module src, +0x30 dst, +0x34 len | +0x38 MBI | +0x3c entry (all u32)
// | +0x40 GDT: null, 32-bit code (0x08), 32-bit data (0x10).
global_asm!(
    r#"
    .section .text.usas_trampoline,"ax"
    .globl usas_trampoline_start
    .globl usas_trampoline_end
    .code64
usas_trampoline_start:
    cli
    movq %rcx, %rbp
    movq 0x00(%rbp), %rsp
    movq %cr4, %rax
    andq $0xfffffffffffdffff, %rax
    movq %rax, %cr4
    lgdt 0x08(%rbp)
    pushq $0x08
    leaq 2f(%rip), %rax
    pushq %rax
    lretq
    .code32
2:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl %cr0, %eax
    andl $0x7fffffff, %eax
    movl %eax, %cr0
    movl $0xc0000080, %ecx
    rdmsr
    andl $0xfffffeff, %eax
    wrmsr
    xorl %eax, %eax
    movl %eax, %cr4
    movl 0x20(%ebp), %esi
    movl 0x24(%ebp), %edi
    movl 0x28(%ebp), %ecx
    cld
    rep movsb
    movl 0x2c(%ebp), %esi
    movl 0x30(%ebp), %edi
    movl 0x34(%ebp), %ecx
    rep movsb
    movl $0x36d76289, %eax
    movl 0x38(%ebp), %ebx
    movl 0x3c(%ebp), %edx
    jmp *%edx
usas_trampoline_end:
    .code64
"#,
    options(att_syntax)
);

extern "C" {
    static usas_trampoline_start: u8;
    static usas_trampoline_end: u8;
}

// ---------------------------------------------------------------- multiboot2 information
struct Mbi { base: *mut u8, len: usize, cap: usize }

impl Mbi {
    fn put32(&mut self, v: u32) { unsafe { (self.base.add(self.len) as *mut u32).write_unaligned(v) }; self.len += 4; }
    fn put64(&mut self, v: u64) { unsafe { (self.base.add(self.len) as *mut u64).write_unaligned(v) }; self.len += 8; }
    fn put8(&mut self, v: u8) { unsafe { *self.base.add(self.len) = v }; self.len += 1; }
    fn pad8(&mut self) { while self.len % 8 != 0 { self.put8(0); } }
    fn room(&self, n: usize) -> bool { self.len + n <= self.cap }
}

#[derive(Clone, Copy)]
struct Region { base: u64, len: u64, kind: u32 }

/// EFI memory type -> multiboot2 type: 1 free, 2 reserved, 3 ACPI reclaimable, 4 ACPI NVS, 5 bad.
fn mb_type(efi: u32) -> u32 {
    match efi {
        1 | 2 | 3 | 4 | 7 => 1,
        9 => 3,
        10 => 4,
        8 => 5,
        _ => 2,
    }
}

/// Convert, sort, merge and cap the firmware's memory map. Returns the number of regions.
fn convert_map(map: *const u8, map_size: usize, desc_size: usize, out: &mut [Region; 512]) -> usize {
    let mut n = 0usize;
    let count = map_size / desc_size;
    for i in 0..count {
        let d = unsafe { map.add(i * desc_size) };
        let ty = unsafe { (d as *const u32).read_unaligned() };
        if ty == 11 || ty == 12 { continue; } // MMIO: not memory
        let base = unsafe { (d.add(8) as *const u64).read_unaligned() };
        let pages = unsafe { (d.add(24) as *const u64).read_unaligned() };
        if n < out.len() { out[n] = Region { base, len: pages * 4096, kind: mb_type(ty) }; n += 1; }
    }
    // insertion sort by base
    for i in 1..n {
        let r = out[i];
        let mut j = i;
        while j > 0 && out[j - 1].base > r.base { out[j] = out[j - 1]; j -= 1; }
        out[j] = r;
    }
    // merge contiguous regions of the same kind
    let mut m = 0usize;
    for i in 0..n {
        if m > 0 && out[m - 1].kind == out[i].kind && out[m - 1].base + out[m - 1].len == out[i].base {
            out[m - 1].len += out[i].len;
        } else {
            out[m] = out[i];
            m += 1;
        }
    }
    // seL4 accepts at most 16 free regions above 1 MiB: keep the 15 largest, mark the rest reserved
    loop {
        let mut free = 0;
        let mut smallest = usize::MAX;
        for i in 0..m {
            if out[i].kind == 1 && out[i].base >= 0x100000 {
                free += 1;
                if smallest == usize::MAX || out[i].len < out[smallest].len { smallest = i; }
            }
        }
        if free <= 15 { break; }
        out[smallest].kind = 2;
    }
    m
}

// ---------------------------------------------------------------- the owner's file (step 5e)
// \TURIYA\OWNER.TXT on the device Usas itself was loaded from (the stick), opened read-only. Returns the state
// (owner::READ, NONE, TOO_LARGE, UNREADABLE) and the number of bytes in buf. Nothing else is opened or written.
fn read_owner(bs: &BootServices, image: Handle, buf: &mut [u8; owner::MAX + 1]) -> (u8, usize) {
    let mut li: *mut u8 = core::ptr::null_mut();
    if (bs.handle_protocol)(image, &LOADED_IMAGE_GUID, &mut li) != EFI_SUCCESS || li.is_null() { return (owner::UNREADABLE, 0); }
    let dev = unsafe { (*(li as *const LoadedImage)).device };
    let mut fs: *mut u8 = core::ptr::null_mut();
    if (bs.handle_protocol)(dev, &SIMPLE_FS_GUID, &mut fs) != EFI_SUCCESS || fs.is_null() { return (owner::UNREADABLE, 0); }
    let fs = fs as *mut SimpleFs;
    let mut root: *mut File = core::ptr::null_mut();
    if unsafe { ((*fs).open_volume)(fs, &mut root) } != EFI_SUCCESS || root.is_null() { return (owner::UNREADABLE, 0); }
    let mut f: *mut File = core::ptr::null_mut();
    let st = unsafe { ((*root).open)(root, &mut f, OWNER_PATH.as_ptr(), FILE_MODE_READ, 0) };
    if st != EFI_SUCCESS || f.is_null() {
        unsafe { ((*root).close)(root) };
        return (if st == EFI_NOT_FOUND { owner::NONE } else { owner::UNREADABLE }, 0);
    }
    let (mut total, mut ok) = (0usize, true);
    while total < buf.len() {
        let mut n = buf.len() - total;
        if unsafe { ((*f).read)(f, &mut n, buf.as_mut_ptr().add(total)) } != EFI_SUCCESS { ok = false; break; }
        if n == 0 { break; }
        total += n;
    }
    unsafe { ((*f).close)(f); ((*root).close)(root); }
    if !ok { return (owner::UNREADABLE, 0); }
    if total > owner::MAX { return (owner::TOO_LARGE, 0); }
    (owner::READ, total)
}

// ---------------------------------------------------------------- the dawn
#[no_mangle]
pub extern "efiapi" fn efi_main(image: Handle, st: *mut SystemTable) -> Status {
    unsafe { CON = (*st).con_out; }
    match dawn(image, st) {
        Ok(()) => EFI_SUCCESS,
        Err(Fail(why)) => {
            say!("\nUsas could not start Turiya: {}\nReturning to the firmware in 15 seconds.\n", why);
            unsafe { ((*(*st).boot_services).stall)(15_000_000) };
            1 | (1usize << 63) // EFI_LOAD_ERROR
        }
    }
}

fn dawn(image: Handle, st: *mut SystemTable) -> Result<(), Fail> {
    let bs = unsafe { &*(*st).boot_services };
    unsafe { ((*CON).clear_screen)(CON) };
    say!("Usas, the dawn: Turiya's start-up, written from zero\n\n");
    (bs.set_watchdog_timer)(0, 0, 0, core::ptr::null());

    // ---- 1. the screen: pick the largest direct-colour mode the firmware offers
    let mut gop: *mut u8 = core::ptr::null_mut();
    let mut fb: Option<(u64, u32, u32, u32, u32)> = None; // base, pitch(bytes), width, height, pixel format
    if (bs.locate_protocol)(&GOP_GUID, core::ptr::null(), &mut gop) == EFI_SUCCESS && !gop.is_null() {
        let g = gop as *mut Gop;
        let mode = unsafe { &*(*g).mode };
        let (mut best, mut best_px) = (mode.mode, 0u64);
        for m in 0..mode.max_mode {
            let mut sz = 0usize;
            let mut info: *const GopModeInfo = core::ptr::null();
            if unsafe { ((*g).query_mode)(g, m, &mut sz, &mut info) } != EFI_SUCCESS || info.is_null() { continue; }
            let i = unsafe { &*info };
            if i.pixel_format > 1 || i.width > 3840 { continue; }
            let px = i.width as u64 * i.height as u64;
            if px > best_px { best_px = px; best = m; }
        }
        if best != mode.mode { unsafe { ((*g).set_mode)(g, best) }; }
        let mode = unsafe { &*(*g).mode };
        let i = unsafe { &*mode.info };
        if i.pixel_format <= 1 {
            fb = Some((mode.fb_base, i.pixels_per_line * 4, i.width, i.height, i.pixel_format));
        }
    }
    unsafe { ((*CON).clear_screen)(CON) };
    say!("Usas, the dawn: Turiya's start-up, written from zero\n\n");
    match fb {
        Some((b, _, w, h, f)) => { say!("  screen    {} x {} at {:#x} ({})\n", w, h, b, if f == 1 { "BGR" } else { "RGB" }); }
        None => { say!("  screen    none usable (no direct-colour GOP mode)\n"); }
    }

    // ---- 2. ACPI
    let mut rsdp: *const u8 = core::ptr::null();
    let mut rsdp_v2 = false;
    for i in 0..unsafe { (*st).n_config } {
        let c = unsafe { &*(*st).config.add(i) };
        if c.guid == ACPI20_GUID { rsdp = c.table; rsdp_v2 = true; break; }
        if c.guid == ACPI10_GUID && rsdp.is_null() { rsdp = c.table; }
    }
    if rsdp.is_null() { return Err(Fail("the firmware publishes no ACPI tables")); }
    let rev = unsafe { *rsdp.add(15) };
    rsdp_v2 = rsdp_v2 && rev >= 2;
    say!("  acpi      RSDP at {:#x}, revision {}\n", rsdp as u64, rev);

    // ---- 3. the kernel image
    let k = KERNEL;
    if k.len() < 64 || &k[0..4] != b"\x7fELF" || k[4] != 2 { return Err(Fail("seL4 image is not a 64-bit ELF")); }
    let entry_v = rd64(k, 0x18);
    let phoff = rd64(k, 0x20) as usize;
    let phentsize = rd16(k, 0x36) as usize;
    let phnum = rd16(k, 0x38) as usize;
    let (mut kmin, mut kmax, mut entry_p) = (u64::MAX, 0u64, 0u64);
    for i in 0..phnum {
        let p = phoff + i * phentsize;
        if rd32(k, p) != 1 { continue; } // PT_LOAD
        let (vaddr, paddr, memsz) = (rd64(k, p + 16), rd64(k, p + 24), rd64(k, p + 40));
        kmin = kmin.min(paddr);
        kmax = kmax.max(paddr + memsz);
        if entry_v >= vaddr && entry_v < vaddr + memsz { entry_p = paddr + (entry_v - vaddr); }
    }
    if entry_p == 0 { entry_p = entry_v; }
    kmin &= !0xfff;
    kmax = align_up(kmax, 4096);
    if kmax > 0xffff_f000 || entry_p > 0xffff_ffff { return Err(Fail("seL4 must load below 4 GiB")); }
    say!("  kernel    seL4 at {:#x}-{:#x}, entry {:#x}\n", kmin, kmax, entry_p);

    // ---- 4. memory of our own, below 4 GiB, as far from the kernel as the firmware allows
    let alloc = |pages: usize, ty: u32| -> Result<u64, Fail> {
        let mut a: u64 = 0xffff_ffff;
        if (bs.allocate_pages)(ALLOCATE_MAX_ADDRESS, ty, pages, &mut a) != EFI_SUCCESS { return Err(Fail("out of memory below 4 GiB")); }
        if overlaps(a, a + pages as u64 * 4096, kmin, kmax) { return Err(Fail("firmware gave memory where seL4 must go")); }
        Ok(a)
    };
    let kpages = ((kmax - kmin) / 4096) as usize;
    let kstage = alloc(kpages, LOADER_DATA)?;
    let mpages = (ROOT.len() + 4095) / 4096;
    let mstage = alloc(mpages, LOADER_DATA)?;
    let mbi_pages = 4;
    let mbi_addr = alloc(mbi_pages, LOADER_DATA)?;
    let tramp = alloc(2, LOADER_CODE)?;
    let map_pages = 32;
    let map_buf = alloc(map_pages, LOADER_DATA)?;

    // kernel image laid out exactly as it must sit in physical memory, zeros in between
    unsafe { core::ptr::write_bytes(kstage as *mut u8, 0, kpages * 4096) };
    for i in 0..phnum {
        let p = phoff + i * phentsize;
        if rd32(k, p) != 1 { continue; }
        let (off, paddr, filesz) = (rd64(k, p + 8) as usize, rd64(k, p + 24), rd64(k, p + 32) as usize);
        if off + filesz > k.len() { return Err(Fail("seL4 image is truncated")); }
        unsafe { core::ptr::copy_nonoverlapping(k.as_ptr().add(off), (kstage + (paddr - kmin)) as *mut u8, filesz) };
    }
    unsafe { core::ptr::copy_nonoverlapping(ROOT.as_ptr(), mstage as *mut u8, ROOT.len()) };

    // ---- 5. where Sakshi goes: free memory above the kernel, with room for seL4 to unpack it
    let mut map_size = map_pages * 4096;
    let (mut key, mut dsize, mut dver) = (0usize, 0usize, 0u32);
    if (bs.get_memory_map)(&mut map_size, map_buf as *mut u8, &mut key, &mut dsize, &mut dver) != EFI_SUCCESS {
        return Err(Fail("could not read the memory map"));
    }
    let need = align_up(ROOT.len() as u64, 4096) + 16 * 1024 * 1024;
    let mut mod_dst = 0u64;
    for i in 0..map_size / dsize {
        let d = (map_buf as usize + i * dsize) as *const u8;
        let ty = unsafe { (d as *const u32).read_unaligned() };
        if ty != CONVENTIONAL { continue; }
        let base = unsafe { (d.add(8) as *const u64).read_unaligned() };
        let end = base + unsafe { (d.add(24) as *const u64).read_unaligned() } * 4096;
        let start = align_up(base.max(kmax), 4096);
        if start + need <= end && start + need <= 0xffff_f000 { mod_dst = start; break; }
    }
    if mod_dst == 0 { return Err(Fail("no free memory above the kernel for Sakshi")); }
    say!("  sakshi    {} bytes, placed at {:#x}\n", ROOT.len(), mod_dst);

    // ---- 5b. the owner's file, into the staged copy of Sakshi (its writable data only; the seal still holds)
    let mut obuf = [0u8; owner::MAX + 1];
    let (ostate, olen) = read_owner(bs, image, &mut obuf);
    let img = unsafe { core::slice::from_raw_parts_mut(mstage as *mut u8, ROOT.len()) };
    match owner::find_area(img) {
        Some(at) if owner::place(img, at, ostate, &obuf[..olen]) => match ostate {
            owner::READ => { say!("  owner     \\TURIYA\\OWNER.TXT, {} bytes, read for Sakshi\n", olen); }
            owner::NONE => { say!("  owner     no \\TURIYA\\OWNER.TXT on this stick: Sakshi will know no target\n"); }
            owner::TOO_LARGE => { say!("  owner     \\TURIYA\\OWNER.TXT is over {} bytes: not used\n", owner::MAX); }
            _ => { say!("  owner     \\TURIYA\\OWNER.TXT could not be read: not used\n"); }
        },
        _ => { say!("  owner     Sakshi has no place for the owner's file: not used\n"); }
    }

    // ---- 6. multiboot2 information, all but the memory map
    let mut mbi = Mbi { base: mbi_addr as *mut u8, len: 0, cap: mbi_pages * 4096 };
    unsafe { core::ptr::write_bytes(mbi.base, 0, mbi.cap) };
    mbi.put32(0); mbi.put32(0); // total size, reserved
    // command line (type 1), empty: seL4 then chooses its debug console as it does under GRUB
    mbi.put32(1); mbi.put32(9); mbi.put8(0);
    mbi.pad8();
    // module (type 3): the root task
    let name = b"sakshi\0";
    mbi.put32(3); mbi.put32((16 + name.len()) as u32); mbi.put32(mod_dst as u32); mbi.put32((mod_dst + ROOT.len() as u64) as u32);
    for &c in name { mbi.put8(c); }
    mbi.pad8();
    // framebuffer (type 8), direct RGB
    if let Some((base, pitch, w, h, f)) = fb {
        mbi.put32(8); mbi.put32(38); mbi.put64(base); mbi.put32(pitch); mbi.put32(w); mbi.put32(h);
        mbi.put8(32); mbi.put8(1); mbi.put8(0); mbi.put8(0);
        if f == 1 { for v in [16u8, 8, 8, 8, 0, 8] { mbi.put8(v); } } else { for v in [0u8, 8, 8, 8, 16, 8] { mbi.put8(v); } }
        mbi.pad8();
    }
    // ACPI RSDP: version 1 (type 14, 20 bytes), and version 2 (type 15, 36 bytes) when present
    mbi.put32(14); mbi.put32(8 + 20);
    for i in 0..20 { mbi.put8(unsafe { *rsdp.add(i) }); }
    mbi.pad8();
    if rsdp_v2 {
        mbi.put32(15); mbi.put32(8 + 36);
        for i in 0..36 { mbi.put8(unsafe { *rsdp.add(i) }); }
        mbi.pad8();
    }
    let mmap_at = mbi.len;

    // ---- 7. the trampoline page: parameters, GDT, code, stack
    let t = tramp as *mut u8;
    unsafe {
        core::ptr::write_bytes(t, 0, 2 * 4096);
        let code_len = (&usas_trampoline_end as *const u8 as usize) - (&usas_trampoline_start as *const u8 as usize);
        core::ptr::copy_nonoverlapping(&usas_trampoline_start as *const u8, t.add(0x100), code_len);
        (t as *mut u64).write_unaligned(tramp + 2 * 4096 - 16);           // stack top
        (t.add(0x08) as *mut u16).write_unaligned(3 * 8 - 1);              // GDTR limit
        (t.add(0x0a) as *mut u64).write_unaligned(tramp + 0x40);           // GDTR base
        (t.add(0x20) as *mut u32).write_unaligned(kstage as u32);
        (t.add(0x24) as *mut u32).write_unaligned(kmin as u32);
        (t.add(0x28) as *mut u32).write_unaligned((kmax - kmin) as u32);
        (t.add(0x2c) as *mut u32).write_unaligned(mstage as u32);
        (t.add(0x30) as *mut u32).write_unaligned(mod_dst as u32);
        (t.add(0x34) as *mut u32).write_unaligned(ROOT.len() as u32);
        (t.add(0x38) as *mut u32).write_unaligned(mbi_addr as u32);
        (t.add(0x3c) as *mut u32).write_unaligned(entry_p as u32);
        (t.add(0x40) as *mut u64).write_unaligned(0);
        (t.add(0x48) as *mut u64).write_unaligned(0x00cf_9a00_0000_ffff); // 32-bit code
        (t.add(0x50) as *mut u64).write_unaligned(0x00cf_9200_0000_ffff); // 32-bit data
    }
    say!("  handing over to seL4. The firmware is not called again.\n");
    (bs.stall)(700_000);

    // ---- 8. the final memory map, then leave the firmware
    let mut regions = [Region { base: 0, len: 0, kind: 0 }; 512];
    let mut tries = 0;
    loop {
        map_size = map_pages * 4096;
        if (bs.get_memory_map)(&mut map_size, map_buf as *mut u8, &mut key, &mut dsize, &mut dver) != EFI_SUCCESS {
            return Err(Fail("could not read the final memory map"));
        }
        let n = convert_map(map_buf as *const u8, map_size, dsize, &mut regions);
        mbi.len = mmap_at;
        if !mbi.room(16 + n * 24 + 8) { return Err(Fail("memory map too large")); }
        mbi.put32(6); mbi.put32((16 + n * 24) as u32); mbi.put32(24); mbi.put32(0);
        for r in regions.iter().take(n) { mbi.put64(r.base); mbi.put64(r.len); mbi.put32(r.kind); mbi.put32(0); }
        mbi.put32(0); mbi.put32(8); // end tag
        unsafe { (mbi.base as *mut u32).write_unaligned(mbi.len as u32) };
        if (bs.exit_boot_services)(image, key) == EFI_SUCCESS { break; }
        tries += 1;
        if tries > 3 { return Err(Fail("the firmware would not let go (ExitBootServices)")); }
    }

    // ---- 9. no firmware from here on
    let go: extern "efiapi" fn(u64) -> ! = unsafe { core::mem::transmute(tramp + 0x100) };
    go(tramp)
}

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    loop { core::hint::spin_loop(); }
}
