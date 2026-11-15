/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sakshi, the first program that runs on seL4 (the "root task").
 *
 * Written from zero: no C library, no Genode, no runtime. It
 *   1. sets up its own thread-local storage and IPC buffer,
 *   2. finds the screen the firmware left behind (UEFI GOP, passed on by seL4),
 *   3. turns the device memory of that screen into pages and maps them,
 *   4. draws its log on the screen with its own font renderer,
 *   5. runs the Kartr gate rule (kartr_rule.h, the same code as the 36 host tests) on 15
 *      test actions and shows each verdict.
 * Everything it prints also goes to the serial line (kernel debug output) for the emulator.
 */
#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/arch/bootinfo_types.h>
#include <sel4/sel4_arch/mapping.h>
#include "font8x16.h"
#include "kartr_rule.h"

typedef unsigned long  uptr;
typedef unsigned int   u32b;

/* the screen record seL4 passes on from multiboot2 (multiboot2_fb in the kernel) */
struct __attribute__((packed)) fb_info { uint64_t addr; uint32_t pitch, width, height; uint8_t bpp, type; };

/* ---- tiny runtime: stack, TLS, memset/memcpy ---- */
extern "C" { __attribute__((aligned(16), used)) unsigned char stack[64 * 1024]; }
__attribute__((aligned(64))) static unsigned char tls_block[256];
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

extern "C" void *memset(void *d, int c, unsigned long n) { unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

extern "C" void sakshi_main(seL4_BootInfo *bi);

extern "C" __attribute__((naked, noreturn)) void _start(void)
{
	/* rdi = boot info, from the kernel */
	__asm__ volatile (
		"lea stack+65536(%rip), %rsp\n"
		"and $-16, %rsp\n"
		"call sakshi_main\n"
		"1: hlt\n jmp 1b\n");
}

/* ---- serial output via the kernel's debug channel ---- */
static void sput(const char *s) { while (*s) seL4_DebugPutChar(*s++); }

/* ---- screen ---- */
static volatile unsigned char *fb; /* mapped framebuffer */
static u32b fb_w, fb_h, fb_pitch, fb_bytes; /* bytes per pixel: 3 or 4 */
static u32b cur_x, cur_y;           /* text cursor in cells */
static u32b SCALE = 2, CW = 16, CH = 32;
enum { MARGIN = 24 };
static const u32b BG = 0x00101418, FG = 0x00d8dde4, SAFFRON = 0x00f29a2e, GREEN = 0x0059c27a, RED = 0x00e2574c, DIM = 0x00808a96;
static u32b colour = 0x00d8dde4;

static inline void px(u32b x, u32b y, u32b c)
{
	volatile unsigned char *p = fb + (unsigned long)y * fb_pitch + (unsigned long)x * fb_bytes;
	if (fb_bytes == 4) *(volatile u32b *)p = c;
	else { p[0] = (unsigned char)c; p[1] = (unsigned char)(c >> 8); p[2] = (unsigned char)(c >> 16); }
}

static void fill(u32b x0, u32b y0, u32b w, u32b h, u32b c)
{
	if (!fb) return;
	for (u32b y = y0; y < y0 + h && y < fb_h; y++)
		for (u32b x = x0; x < x0 + w && x < fb_w; x++)
			px(x, y, c);
}

static void glyph(u32b cx, u32b cy, char ch)
{
	if (!fb) return;
	if (ch < 32 || ch > 126) ch = '?';
	const unsigned char *g = FONT8x16[ch - 32];
	u32b gx = MARGIN + cx * CW, py = MARGIN + cy * CH;
	if (gx + CW > fb_w || py + CH > fb_h) return;
	for (u32b y = 0; y < 16; y++)
		for (u32b x = 0; x < 8; x++) {
			u32b c = (g[y] & (0x80 >> x)) ? colour : BG;
			for (u32b dy = 0; dy < SCALE; dy++)
				for (u32b dx = 0; dx < SCALE; dx++)
					px(gx + x * SCALE + dx, py + y * SCALE + dy, c);
		}
}

static void out(const char *s)
{
	sput(s);
	u32b cols = fb_w > 2 * MARGIN ? (fb_w - 2 * MARGIN) / CW : 0;
	for (; *s; s++) {
		if (*s == '\n' || (cols && cur_x >= cols)) { cur_x = 0; cur_y++; if (*s == '\n') continue; }
		glyph(cur_x++, cur_y, *s);
	}
}

static void out_dec(unsigned long v)
{
	char b[24]; int i = 23; b[i] = 0;
	do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v);
	out(b + i);
}

static void out_hex(unsigned long v)
{
	char b[19]; b[0] = '0'; b[1] = 'x'; int n = 0;
	for (int i = 15; i >= 0; i--) { unsigned d = (v >> (i * 4)) & 0xf; if (d || n || i == 0) b[2 + n++] = "0123456789abcdef"[d]; }
	b[2 + n] = 0; out(b);
}

/* ---- capability bookkeeping ---- */
static seL4_CPtr next_slot, last_slot;
static seL4_BootInfo *bootinfo;

static seL4_CPtr alloc_slot(void) { return next_slot < last_slot ? next_slot++ : 0; }

/* RAM untyped used for page-table objects: pick the largest non-device one */
static seL4_CPtr ram_ut;

static int retype(seL4_CPtr ut, seL4_Word type, seL4_Word size_bits, seL4_CPtr slot, seL4_Word count)
{
	return seL4_Untyped_Retype(ut, type, size_bits, seL4_CapInitThreadCNode, 0, 0, slot, count);
}

/* map one 4K frame cap at vaddr, creating missing paging structures on the way */
static int map_frame(seL4_CPtr frame, seL4_Word vaddr)
{
	for (int tries = 0; tries < 4; tries++) {
		int err = seL4_X86_Page_Map(frame, seL4_CapInitThreadVSpace, vaddr, seL4_ReadWrite,
		                            seL4_X86_WriteCombining);
		if (err != seL4_FailedLookup) return err;
		seL4_Word level = seL4_MappingFailedLookupLevel();
		seL4_CPtr s = alloc_slot();
		if (!s) return -1;
		if (level == SEL4_MAPPING_LOOKUP_NO_PT) {
			if (retype(ram_ut, seL4_X86_PageTableObject, 0, s, 1)) return -2;
			if (seL4_X86_PageTable_Map(s, seL4_CapInitThreadVSpace, vaddr, seL4_X86_Default_VMAttributes)) return -3;
		} else if (level == SEL4_MAPPING_LOOKUP_NO_PD) {
			if (retype(ram_ut, seL4_X86_PageDirectoryObject, 0, s, 1)) return -4;
			if (seL4_X86_PageDirectory_Map(s, seL4_CapInitThreadVSpace, vaddr, seL4_X86_Default_VMAttributes)) return -5;
		} else if (level == SEL4_MAPPING_LOOKUP_NO_PDPT) {
			if (retype(ram_ut, seL4_X86_PDPTObject, 0, s, 1)) return -6;
			if (seL4_X86_PDPT_Map(s, seL4_CapInitThreadVSpace, vaddr, seL4_X86_Default_VMAttributes)) return -7;
		} else return -8;
	}
	return -9;
}

/* turn the device untyped(s) that cover [paddr, paddr+len) into frames and map them at vbase */
static int map_device(uptr paddr, uptr len, uptr vbase)
{
	uptr end = paddr + len;
	uptr pa = paddr & ~0xfffUL;
	while (pa < end) {
		/* find the device untyped containing pa */
		seL4_Word n = bootinfo->untyped.end - bootinfo->untyped.start, i;
		for (i = 0; i < n; i++) {
			seL4_UntypedDesc *u = &bootinfo->untypedList[i];
			uptr ub = u->paddr, ue = ub + (1UL << u->sizeBits);
			if (u->isDevice && pa >= ub && pa < ue) break;
		}
		if (i == n) return -20;
		seL4_UntypedDesc *u = &bootinfo->untypedList[i];
		seL4_CPtr ut = bootinfo->untyped.start + i;
		uptr ub = u->paddr, ue = ub + (1UL << u->sizeBits);
		/* frames are carved from the start of the untyped: skip what lies before pa */
		static uptr carved[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
		uptr at = ub + carved[i];
		while (at < pa) {               /* frames before the screen: created, never mapped */
			seL4_CPtr s = alloc_slot(); if (!s) return -21;
			if (retype(ut, seL4_X86_4K, 0, s, 1)) return -22;
			at += 4096;
		}
		while (at < end && at < ue) {
			seL4_CPtr s = alloc_slot(); if (!s) return -23;
			if (retype(ut, seL4_X86_4K, 0, s, 1)) return -24;
			int e = map_frame(s, vbase + (at - (paddr & ~0xfffUL)));
			if (e) return e;
			at += 4096;
		}
		carved[i] = at - ub;
		pa = at;
	}
	return 0;
}

/* ---- the gate's 15 test actions (same as phase2/genode-repo/run/turiya.run) ---- */
struct act { const char *id, *type, *target, *amount, *appr; };
static const char *APPR = "9a98f640f3090f1147f2ae6765084b5943d900e688de643fb2a1c7ad401e5a31";
static const struct act ACTS[] = {
	{ "a1",  "summarise",    "inbox",              "0",     "" },
	{ "a2",  "pay",          "ramesh.kumar@ybl",   "15000", "" },
	{ "a3",  "pay",          "ramesh.kumar@ybl",   "15000", 0 },
	{ "a4",  "pay",          "ramesh.kumar@ybl",   "50000", 0 },
	{ "a5",  "pay",          "evil@ybl",           "15000", 0 },
	{ "a6",  "delete",       "Turiya/phase0/runs", "0",     "" },
	{ "a7",  "draft_reply",  "sender",             "0",     "" },
	{ "a8",  "share_file",   "Form16",             "0",     "0000000000000000000000000000000000000000000000000000000000000000" },
	{ "a9",  "Pay",          "ramesh.kumar@ybl",   "15000", "" },
	{ "a10", "open_link",    "kyc.example/verify", "0",     "" },
	{ "a11", "pay",          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "15000", 0 },
	{ "a12", "transfer",     "ramesh.kumar@ybl",   "15000", 0 },
	{ "a13", "draft_reply",  "alerts@hdfcbank-kyc.example", "0", "" },
	{ "a14", "draft_reply",  "kyc-desk@okaxis",    "0",     "" },
	{ "a15", "pay",          "9876543210@ybl",     "15000", "" },
};
static const char *OUTSIDE =
	"From: alerts@hdfcbank-kyc.example\n"
	"Dear customer, your KYC is pending. Reply to KYC-Desk@okaxis or pay the fee of Rs 15000 to "
	"9876543210@ybl today. Details: kyc.example/verify.";

static void run_gate(void)
{
	Turiya::u8 key[64]; Turiya::usize klen =
		Turiya::unhex("6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6ba1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4", key, sizeof key);
	unsigned allowed = 0, held = 0;
	for (unsigned i = 0; i < sizeof ACTS / sizeof ACTS[0]; i++) {
		const struct act *a = &ACTS[i];
		const char *ap = a->appr ? a->appr : APPR;
		bool trunc = Turiya::slen(a->target) > Turiya::FIELD_MAX;
		Turiya::Verdict v = Turiya::decide(key, klen, a->type, a->target, a->amount, ap, trunc,
		                                   Turiya::DEFAULT_MANDATE, OUTSIDE);
		bool h = Turiya::is_held(v);
		h ? held++ : allowed++;
		colour = DIM; out("  ["); out(a->id); out("] ");
		colour = h ? RED : GREEN; out(Turiya::verdict_name(v));
		colour = FG; out(": "); out(a->type); out(" ");
		char t[40]; unsigned k = 0; for (; a->target[k] && k < 36; k++) t[k] = a->target[k];
		if (a->target[k]) { t[k++] = '.'; t[k++] = '.'; } t[k] = 0;
		out(t); out(" "); out(a->amount); out("\n");
	}
	colour = SAFFRON; out("  kartr_gate: "); out_dec(allowed); out(" allowed, "); out_dec(held); out(" held\n");
	colour = FG;
}

static void cpu_name(char *n)
{
	for (u32b leaf = 0; leaf < 3; leaf++) {
		u32b r[4];
		__asm__ volatile ("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(0x80000002 + leaf), "c"(0));
		memcpy(n + leaf * 16, r, 16);
	}
	n[48] = 0;
}

extern "C" void sakshi_main(seL4_BootInfo *bi)
{
	bootinfo = bi;
	/* thread-local storage: variant II, the thread pointer points at the end of the TLS block */
	uptr tp = (uptr)(tls_block + sizeof tls_block - 64);
	*(uptr *)tp = tp;
	seL4_TCB_SetTLSBase(seL4_CapInitThreadTCB, tp);
	seL4_SetIPCBuffer(bi->ipcBuffer);

	next_slot = bi->empty.start; last_slot = bi->empty.end;

	sput("\nTuriya: Sakshi starting (serial)\n");

	/* pick the largest RAM untyped for paging structures */
	seL4_Word best = 0;
	for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
		seL4_UntypedDesc *u = &bi->untypedList[i];
		if (!u->isDevice && u->sizeBits > best) { best = u->sizeBits; ram_ut = bi->untyped.start + i; }
	}

	/* find the screen in the extra boot info */
	struct fb_info *fbi = 0;
	unsigned long ram = 0;
	for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++)
		if (!bi->untypedList[i].isDevice) ram += 1UL << bi->untypedList[i].sizeBits;
	char *x = (char *)bi + (1UL << seL4_PageBits), *xe = x + bi->extraLen;
	while (x < xe) {
		seL4_BootInfoHeader *h = (seL4_BootInfoHeader *)x;
		{ char m[48]; unsigned k=0; const char *t="  [bootinfo extra id="; while(*t) m[k++]=*t++; m[k++]=(char)('0'+h->id%10); m[k++]=']'; m[k++]='\n'; m[k]=0; sput(m); }
		if (h->id == SEL4_BOOTINFO_HEADER_X86_FRAMEBUFFER) fbi = (struct fb_info *)(x + sizeof *h);
		if (!h->len) break;
		x += h->len;
	}

	int map_err = 1;
	if (fbi) { sput("  [fb bpp="); char b[4]={(char)('0'+fbi->bpp/10),(char)('0'+fbi->bpp%10),']',0}; sput(b); sput("\n"); }
	if (fbi && fbi->width && (fbi->bpp == 32 || fbi->bpp == 24)) {
		fb_w = fbi->width; fb_h = fbi->height; fb_pitch = fbi->pitch;
		fb_bytes = fbi->pitch / fbi->width >= 4 ? 4 : 3;
		SCALE = fb_w >= 2400 ? 3 : (fb_w >= 1200 ? 2 : 1); CW = 8 * SCALE; CH = 16 * SCALE;
		map_err = map_device(fbi->addr, (uptr)fbi->pitch * fbi->height, 0x8000000000UL);
		if (!map_err) fb = (volatile unsigned char *)0x8000000000UL;
	}
	if (fb) fill(0, 0, fb_w, fb_h, BG);

	char cpu[49]; cpu_name(cpu);
	colour = SAFFRON; out("TURIYA"); colour = DIM; out("  first light, written from zero on seL4\n\n");
	colour = FG;
	out("Sakshi is awake. I hold the screen and nothing else.\n\n");
	colour = DIM; out("  kernel      "); colour = FG; out("seL4 13.0.0 (x86-64)\n");
	colour = DIM; out("  processor   "); colour = FG; { char *c = cpu; while (*c == ' ') c++; out(c); } out("\n");
	colour = DIM; out("  memory      "); colour = FG; out_dec(ram >> 20); out(" MiB handed to Sakshi\n");
	colour = DIM; out("  screen      "); colour = FG;
	if (fb) { out_dec(fb_w); out(" x "); out_dec(fb_h); out(" at "); out_hex(fbi->addr); out("\n"); }
	else { out("not found (error "); out_dec((unsigned long)(-map_err)); out("), serial only\n"); }
	colour = DIM; out("  untyped     "); colour = FG; out_dec(bi->untyped.end - bi->untyped.start); out(" regions\n\n");

	colour = SAFFRON; out("Kartr gate, 15 test actions (rule code identical to the 36 host tests)\n"); colour = FG;
	run_gate();
	colour = DIM; out("\nNothing here can pay, send or delete: the gate only decides.\n");
	sput("Turiya: done\n");

	/* rest: block forever on a cap that never answers */
	for (;;) seL4_Yield();
}
