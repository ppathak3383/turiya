/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sakshi, the first program that runs on seL4 (the "root task").
 *
 * Written from zero: no C library, no Genode, no runtime. It
 *   1. sets up its own thread-local storage and IPC buffer,
 *   2. finds the screen the firmware left behind (UEFI GOP, passed on by seL4) and maps it,
 *   3. keeps a text grid on that screen with its own font renderer,
 *   4. runs the Kartr gate rule (kartr_rule.h, the same code as the host tests) on 15 test actions,
 *   5. starts a heart: a 50 Hz timer (PIT, IRQ 0) that beats even if nothing else happens,
 *   6. makes the first organ, Tvak (touch): the keyboard runs in its own compartment with only the
 *      capabilities its duty needs; if it faults, Sakshi re-makes it from its SHA-256-checked seed,
 *   7. offers a first command line: every typed action goes through the gate; an irreversible one
 *      can be approved only from this keyboard.
 * Everything it prints also goes to the serial line (kernel debug output) for the emulator.
 */
#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/arch/bootinfo_types.h>
#include <sel4/sel4_arch/mapping.h>
#include "font8x16.h"
#include "kartr_rule.h"
#include "kbd_i8042.h"
#include "tvak_seed.h"
#include "kosa_seed.h"
#include "hrdaya_seed.h"
#include "kosa_window.h"
#include "nadi_seed.h"
#include "nadi_window.h"
#include "smrti_seed.h"
#include "smrti_window.h"

typedef unsigned long uptr;
typedef unsigned int  u32b;

/* the screen record seL4 passes on from multiboot2 (multiboot2_fb in the kernel) */
struct __attribute__((packed)) fb_info { uint64_t addr; uint32_t pitch, width, height; uint8_t bpp, type; };

/* ---- tiny runtime: stack, TLS, memset/memcpy ---- */
extern "C" { __attribute__((aligned(16), used)) unsigned char stack[64 * 1024]; }
__attribute__((aligned(64))) static unsigned char tls_block[256];
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

extern "C" void *memset(void *d, int c, unsigned long n)
{ unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

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

static unsigned long slen(const char *s) { unsigned long n = 0; while (s[n]) n++; return n; }
static bool streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ---- serial output via the kernel's debug channel ---- */
static void sput(const char *s) { while (*s) seL4_DebugPutChar(*s++); }

/* ---- screen: a text grid drawn with our own font ---- */
static volatile unsigned char *fb;
static u32b fb_w, fb_h, fb_pitch, fb_bytes;
static u32b SCALE = 1, CW = 8, CH = 16;
enum { MARGIN = 24, MAXC = 256, MAXR = 128 };
static const u32b BG = 0x00101418, FG = 0x00d8dde4, SAFFRON = 0x00f29a2e, GREEN = 0x0059c27a,
                  RED = 0x00e2574c, DIM = 0x00808a96, BAR = 0x001c2229;
static u32b colour = FG;

static char cell_ch[MAXR][MAXC];
static u32b cell_col[MAXR][MAXC];
static u32b cols = 100, rows = 40, cur_x, cur_y;

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
		for (u32b x = x0; x < x0 + w && x < fb_w; x++) px(x, y, c);
}

static void glyph_at(u32b gx, u32b gy, char ch, u32b fg, u32b bg)
{
	if (!fb) return;
	if (ch < 32 || ch > 126) ch = ' ';
	const unsigned char *g = FONT8x16[ch - 32];
	if (gx + CW > fb_w || gy + CH > fb_h) return;
	for (u32b y = 0; y < 16; y++)
		for (u32b x = 0; x < 8; x++) {
			u32b c = (g[y] & (0x80 >> x)) ? fg : bg;
			for (u32b dy = 0; dy < SCALE; dy++)
				for (u32b dx = 0; dx < SCALE; dx++)
					px(gx + x * SCALE + dx, gy + y * SCALE + dy, c);
		}
}

static void draw_cell(u32b cx, u32b cy)
{
	glyph_at(MARGIN + cx * CW, MARGIN + cy * CH, cell_ch[cy][cx], cell_col[cy][cx], BG);
}

static void redraw_all(void)
{
	if (!fb) return;
	for (u32b y = 0; y < rows; y++)
		for (u32b x = 0; x < cols; x++) draw_cell(x, y);
}

static void newline(void)
{
	cur_x = 0;
	if (++cur_y < rows) return;
	for (u32b y = 1; y < rows; y++) { memcpy(cell_ch[y - 1], cell_ch[y], cols); memcpy(cell_col[y - 1], cell_col[y], cols * 4); }
	memset(cell_ch[rows - 1], ' ', cols);
	cur_y = rows - 1;
	redraw_all();
}

static void putc_screen(char c)
{
	if (c == '\n') { newline(); return; }
	if (cur_x >= cols) newline();
	cell_ch[cur_y][cur_x] = c; cell_col[cur_y][cur_x] = colour;
	draw_cell(cur_x, cur_y);
	cur_x++;
}

static void out(const char *s) { sput(s); for (; *s; s++) putc_screen(*s); }

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

static void cursor(bool on)
{
	if (!fb || cur_x >= cols) return;
	if (on) fill(MARGIN + cur_x * CW, MARGIN + cur_y * CH + CH - 2 * SCALE, CW, 2 * SCALE, SAFFRON);
	else draw_cell(cur_x, cur_y);
}

/* status bar below the grid: the heart, the keyboard organ, the keys */
static unsigned long beats, keys, breaths;
/* the four states (step 1g): the owner's presence decides what may act */
static Turiya::Avastha avastha = Turiya::Avastha::JAGRAT;
static bool dreaming_idle;              /* svapna entered because no key came for IDLE_BEATS */
static unsigned long last_key_beat, dream_plans;
enum { IDLE_BEATS = 600 };              /* 10 minutes without a key: Turiya dreams */
static const char *avastha_name(Turiya::Avastha a)
{
	return a == Turiya::Avastha::SVAPNA ? "svapna, dreaming" : a == Turiya::Avastha::SUSUPTI ? "susupti, deep sleep" : "jagrat, waking";
}
static const char *organ_state(void);
static void status_bar(void)
{
	if (!fb) return;
	u32b y = MARGIN + rows * CH + CH / 2;
	if (y + CH > fb_h) return;
	fill(0, y - CH / 4, fb_w, CH + CH / 2, BAR);
	char line[200]; unsigned k = 0;
	auto add = [&](const char *s) { while (*s && k < sizeof line - 1) line[k++] = *s++; };
	auto addn = [&](unsigned long v) { char b[24]; int i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); add(b + i); };
	add("heart "); addn(beats); add(" beats   ");
	add(avastha == Turiya::Avastha::JAGRAT ? "waking   " : avastha == Turiya::Avastha::SVAPNA ? "DREAMING   " : "ASLEEP   ");
	add(organ_state());
	add("   keys "); addn(keys); add("   type 'help'");
	line[k] = 0;
	for (unsigned i = 0; i < k && MARGIN + (i + 1) * CW <= fb_w; i++)
		glyph_at(MARGIN + i * CW, y, line[i], i < 5 ? RED : DIM, BAR);
}

/* ---- capability bookkeeping and mapping ---- */
static seL4_CPtr next_slot, last_slot;
static seL4_BootInfo *bootinfo;
static seL4_CPtr ram_ut;

static seL4_CPtr alloc_slot(void) { return next_slot < last_slot ? next_slot++ : 0; }

static int retype(seL4_CPtr ut, seL4_Word type, seL4_Word size_bits, seL4_CPtr slot, seL4_Word count)
{
	return seL4_Untyped_Retype(ut, type, size_bits, seL4_CapInitThreadCNode, 0, 0, slot, count);
}

/* map one 4K frame into a VSpace (Sakshi's own or an organ's), making page-table levels as needed */
static int map_in(seL4_CPtr vspace, seL4_CPtr frame, seL4_Word vaddr, seL4_CapRights_t rights, seL4_X86_VMAttributes attr)
{
	for (int tries = 0; tries < 4; tries++) {
		int err = seL4_X86_Page_Map(frame, vspace, vaddr, rights, attr);
		if (err != seL4_FailedLookup) return err;
		seL4_Word level = seL4_MappingFailedLookupLevel();
		seL4_CPtr s = alloc_slot();
		if (!s) return -1;
		if (level == SEL4_MAPPING_LOOKUP_NO_PT) {
			if (retype(ram_ut, seL4_X86_PageTableObject, 0, s, 1)) return -2;
			if (seL4_X86_PageTable_Map(s, vspace, vaddr, seL4_X86_Default_VMAttributes)) return -3;
		} else if (level == SEL4_MAPPING_LOOKUP_NO_PD) {
			if (retype(ram_ut, seL4_X86_PageDirectoryObject, 0, s, 1)) return -4;
			if (seL4_X86_PageDirectory_Map(s, vspace, vaddr, seL4_X86_Default_VMAttributes)) return -5;
		} else if (level == SEL4_MAPPING_LOOKUP_NO_PDPT) {
			if (retype(ram_ut, seL4_X86_PDPTObject, 0, s, 1)) return -6;
			if (seL4_X86_PDPT_Map(s, vspace, vaddr, seL4_X86_Default_VMAttributes)) return -7;
		} else return -8;
	}
	return -9;
}

static int map_frame(seL4_CPtr frame, seL4_Word vaddr)
{
	return map_in(seL4_CapInitThreadVSpace, frame, vaddr, seL4_ReadWrite, seL4_X86_WriteCombining);
}

/* how far each boot device untyped has been carved (shared with the body survey) */
static uptr dev_carved[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];

/* turn the device untyped(s) covering [paddr, paddr+len) into frames and map them at vbase */
static int map_device(uptr paddr, uptr len, uptr vbase)
{
	uptr end = paddr + len, pa = paddr & ~0xfffUL;
	while (pa < end) {
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
		uptr at = ub + dev_carved[i];
		if (at > pa) return -25;          /* already carved past it */
		while (at < pa) {
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
		dev_carved[i] = at - ub;
		pa = at;
	}
	return 0;
}

/* ---- the gate ---- */
struct act { const char *id, *type, *target, *amount, *appr; };
static const char *TEST_KEY = "6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6ba1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4"; /* test key, not a secret */
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

static Turiya::u8 key[64];
static Turiya::usize klen;

/* ---- two judges: the C++ rule (CBMC-checked) and the Verus-proved decision (Rust) must agree ---- */
extern "C" uint32_t turiya_proved_decide(uint32_t flags, uint32_t state, const uint8_t *given, const uint8_t *expect);
static unsigned long judges_agree, judges_differ;

static Turiya::Verdict judge(const char *type, const char *target, const char *amount, const char *appr,
                             bool trunc, const char *outside, Turiya::Avastha state)
{
	using namespace Turiya;
	Verdict c = decide(key, klen, type, target, amount, appr, trunc, DEFAULT_MANDATE, outside, state);
	u8 given[32] = { 0 }, expect[32] = { 0 };
	bool hex_ok = unhex(appr, given, 32) == 32;
	usize lt = Turiya::slen(type), lg = Turiya::slen(target), la = Turiya::slen(amount);
	if (klen && lt <= FIELD_MAX && lg <= FIELD_MAX && la <= FIELD_MAX) {
		char msg[3 * FIELD_MAX + 3]; usize m = 0;
		for (usize i = 0; i < lt; i++) msg[m++] = type[i];
		msg[m++] = '|';
		for (usize i = 0; i < lg; i++) msg[m++] = target[i];
		msg[m++] = '|';
		for (usize i = 0; i < la; i++) msg[m++] = amount[i];
		hmac(key, klen, msg, m, expect);
	}
	uint32_t f = (uint32_t)(!trunc && field_ok(type, false) && field_ok(amount, false))
	           | (uint32_t)field_ok(target, true) << 1 | (uint32_t)(target[0] != 0) << 2
	           | (uint32_t)irreversible(type) << 3 | (uint32_t)in_mandate(DEFAULT_MANDATE, type) << 4
	           | (uint32_t)in_mandate(THINKING, type) << 5 | (uint32_t)target_from_outside(target, outside) << 6
	           | (uint32_t)(klen != 0) << 7 | (uint32_t)hex_ok << 8;
	Verdict r = (Verdict)turiya_proved_decide(f, (uint32_t)state, given, expect);
	if (r == c) { judges_agree++; return c; }
	judges_differ++;
	sput("Turiya: the two judges disagree\n");
	return Verdict::HELD_BAD_INPUT;               /* they disagree: hold, whatever either said */
}

/* the witness log: every decision of this session */
struct entry { char type[24], target[40], amount[16]; Turiya::Verdict v; };
static entry kartr_log[64];
static unsigned log_n, log_total;

static void copy_trunc(char *d, const char *s, unsigned max)
{
	unsigned k = 0; for (; s[k] && k < max - 3; k++) d[k] = s[k];
	if (s[k]) { d[k++] = '.'; d[k++] = '.'; }
	d[k] = 0;
}

static void record(const char *type, const char *target, const char *amount, Turiya::Verdict v)
{
	entry &e = kartr_log[log_n % 64];
	copy_trunc(e.type, type, sizeof e.type); copy_trunc(e.target, target, sizeof e.target);
	copy_trunc(e.amount, amount, sizeof e.amount); e.v = v;
	log_n++; log_total++;
}

static void show_verdict(const char *id, const char *type, const char *target, const char *amount, Turiya::Verdict v)
{
	bool h = Turiya::is_held(v);
	if (id) { colour = DIM; out("  ["); out(id); out("] "); } else out("  ");
	colour = h ? RED : GREEN; out(Turiya::verdict_name(v));
	colour = FG; out(": "); out(type); out(" ");
	char t[40]; copy_trunc(t, target, sizeof t); out(t); out(" "); out(amount); out("\n");
}

static void run_gate(void)
{
	unsigned allowed = 0, held = 0;
	for (unsigned i = 0; i < sizeof ACTS / sizeof ACTS[0]; i++) {
		const struct act *a = &ACTS[i];
		const char *ap = a->appr ? a->appr : APPR;
		bool trunc = Turiya::slen(a->target) > Turiya::FIELD_MAX;
		Turiya::Verdict v = judge(a->type, a->target, a->amount, ap, trunc, OUTSIDE, Turiya::Avastha::JAGRAT);
		Turiya::is_held(v) ? held++ : allowed++;
		record(a->type, a->target, a->amount, v);
		show_verdict(a->id, a->type, a->target, a->amount, v);
	}
	colour = SAFFRON; out("  kartr_gate: "); out_dec(allowed); out(" allowed, "); out_dec(held); out(" held");
	colour = DIM; out("; two judges (the C++ rule, the Verus-proved decision) agreed ");
	out_dec(judges_agree); out(" of "); out_dec(judges_agree + judges_differ); out("\n");
	colour = FG;
}

/* ---- I/O ports, interrupts, heart, keyboard ---- */
static seL4_CPtr kbd_port, pit_port, cf9_port, ntfn, irq_pit;   /* cf9_port covers 0xcf8-0xcff */

static uint8_t inb(seL4_CPtr cap, uint16_t port)
{
	if (!cap) return 0xff;
	seL4_X86_IOPort_In8_t r = seL4_X86_IOPort_In8(cap, port);
	return r.error ? 0xff : r.result;
}
static void outb(seL4_CPtr cap, uint16_t port, uint8_t v) { if (cap) seL4_X86_IOPort_Out8(cap, port, v); }

static bool kbd_wait_write(void) { for (int i = 0; i < 100000; i++) if (!(inb(kbd_port, 0x64) & 2)) return true; return false; }

static seL4_CPtr issue_ports(seL4_Word first, seL4_Word last)
{
	seL4_CPtr s = alloc_slot();
	if (!s || seL4_X86_IOPortControl_Issue(seL4_CapIOPortControl, first, last, seL4_CapInitThreadCNode, s, seL4_WordBits)) return 0;
	return s;
}

static seL4_CPtr irq_on_pin(seL4_Word pin, seL4_Word vector, seL4_Word badge)
{
	seL4_CPtr irq = alloc_slot(), nb = alloc_slot();
	if (!irq || !nb) return 0;
	if (seL4_IRQControl_GetIOAPIC(seL4_CapIRQControl, seL4_CapInitThreadCNode, irq, seL4_WordBits,
	                              0, pin, 0 /* edge */, 0 /* active high */, vector)) return 0;
	if (seL4_CNode_Mint(seL4_CapInitThreadCNode, nb, seL4_WordBits, seL4_CapInitThreadCNode, ntfn, seL4_WordBits,
	                    seL4_AllRights, badge)) return 0;
	if (seL4_IRQHandler_SetNotification(irq, nb)) return 0;
	seL4_IRQHandler_Ack(irq);
	return irq;
}

static const char *io_report = "";
static seL4_CPtr ep;          /* Sakshi's endpoint: organs speak here, and their faults arrive here */

enum : seL4_Word { N_HEART = 2 };                                  /* Sakshi's own notification bits */

static seL4_CPtr rtc_port, irq_rtc;
enum : seL4_Word { N_BREATH = 8 };                                 /* the RTC: Sakshi's own slow clock */

static void setup_io(void)
{
	kbd_port = issue_ports(0x60, 0x64);       /* kept only for reboot and the reflex; Tvak gets a copy */
	pit_port = issue_ports(0x40, 0x43);       /* given to Hrdaya, the heart */
	cf9_port = issue_ports(0xcf8, 0xcff);      /* PCI configuration (0xcf8/0xcfc) and reset (0xcf9) */
	rtc_port = issue_ports(0x70, 0x71);       /* the RTC: Sakshi's breath */
	ntfn = alloc_slot(); ep = alloc_slot();
	if (!ep || retype(ram_ut, seL4_NotificationObject, 0, ntfn, 1) || retype(ram_ut, seL4_EndpointObject, 0, ep, 1)) {
		ntfn = ep = 0; io_report = "no notification"; return;
	}
	/* heartbeats and breaths reach Sakshi even while it waits on its endpoint */
	if (seL4_TCB_BindNotification(seL4_CapInitThreadTCB, ntfn)) { io_report = "notification not bound"; return; }
	/* the breath: RTC periodic interrupt at 2 Hz (rate 15), IRQ 8 */
	if (rtc_port) {
		outb(rtc_port, 0x70, 0x0a); uint8_t a = inb(rtc_port, 0x71);
		outb(rtc_port, 0x70, 0x0a); outb(rtc_port, 0x71, (uint8_t)((a & 0xf0) | 0x0f));
		outb(rtc_port, 0x70, 0x0b); uint8_t b = inb(rtc_port, 0x71);
		outb(rtc_port, 0x70, 0x0b); outb(rtc_port, 0x71, (uint8_t)(b | 0x40));
		outb(rtc_port, 0x70, 0x0c); inb(rtc_port, 0x71);
		irq_rtc = irq_on_pin(8, 8, N_BREATH);
	}
}

/* the heart inside Sakshi, used only if Hrdaya cannot be made */
static void heart_fallback(void)
{
	if (!pit_port) return;
	unsigned div = 1193182 / 50;
	outb(pit_port, 0x43, 0x34);
	outb(pit_port, 0x40, (uint8_t)(div & 0xff));
	outb(pit_port, 0x40, (uint8_t)(div >> 8));
	irq_pit = irq_on_pin(2, 2, N_HEART);
}

/* the reflex: Sakshi feels the keyboard itself, only if Tvak cannot be made or is resting */
static bool reflex, reflex_ready;
static Kbd::Decoder reflex_dec;
static uint8_t s_in8(uint16_t port) { return inb(kbd_port, port); }
static void s_out8(uint16_t port, uint8_t v) { outb(kbd_port, port, v); }
static const Kbd::Port sakshi_port { s_in8, s_out8 };

static char line[160];
static unsigned line_len;
static char last_type[24], last_target[64], last_amount[16];
static bool have_last;

static bool prompt_shown, hold_prompt;   /* hold_prompt: a command is waiting for an organ to answer */
static unsigned long hold_since;
static void prompt(void) { colour = SAFFRON; out("turiya> "); colour = FG; cursor(true); prompt_shown = true; }

/* something happened while the prompt was showing: step onto a fresh line first */
static void interject_begin(void) { if (prompt_shown) { cursor(false); out("\n"); prompt_shown = false; } }
static void interject_end(void)
{
	if (prompt_shown) return;
	prompt();
	cursor(false);
	for (unsigned i = 0; i < line_len; i++) { char s[2] = { line[i], 0 }; out(s); }
	cursor(true);
}

static void help(void)
{
	colour = FG;
	out("  help                     this list\n");
	out("  check                    one screen on this machine and Turiya (reads only; for one photo)\n");
	out("  about                    what is running, on what\n");
	out("  organs                   the parts of Turiya: duty, what each holds, health\n");
	out("  body                     the hardware Turiya found (read-only survey)\n");
	out("  disk                     read the SSD's partition table (Kosa; it can only read)\n");
	out("  usb                      what is plugged into USB; a USB keyboard can type here (Nadi)\n");
	out("  stick                    read a USB stick's partition table (only read commands exist)\n");
	out("  files [folder]           the files on a USB stick (FAT32 or exFAT, read by Smrti)\n");
	out("  read <file>              the start of a file on the stick, as text\n");
	out("  gate                     run the 15 test actions again\n");
	out("  mail                     the sample email the gate guards against\n");
	out("  <action> <target> [amt]  ask the gate, e.g.  pay ramesh.kumar@ybl 15000\n");
	out("                           summarise inbox    draft_reply papa    delete photos\n");
	out("  approve                  approve the last held action (only from this keyboard)\n");
	out("  log                      the witness log of this session\n");
	out("  state [jagrat|svapna|susupti]  waking, dreaming, deep sleep: what may act\n");
	out("  crash <organ>            make an organ touch what it does not hold (tvak, kosa, hrdaya)\n");
	out("  stop hrdaya              make the heart go silent; the breath notices and restarts it\n");
	out("  heal <organ>             wake an organ again if it is resting\n");
	out("  clear                    clear the screen\n");
	out("  reboot                   restart the machine (pull the stick for Windows)\n");
	out("  poweroff                 switch the machine off\n");
}

static char cpu[49];
static unsigned long ram_mib;
static unsigned untyped_n;
static struct fb_info *fbi_g;

static void about(void)
{
	colour = DIM; out("  kernel      "); colour = FG; out("seL4 13.0.0 (x86-64), the only inherited code\n");
	colour = DIM; out("  processor   "); colour = FG; { char *c = cpu; while (*c == ' ') c++; out(c); } out("\n");
	colour = DIM; out("  memory      "); colour = FG; out_dec(ram_mib); out(" MiB handed to Sakshi\n");
	colour = DIM; out("  screen      "); colour = FG;
	if (fb) { out_dec(fb_w); out(" x "); out_dec(fb_h); out(" at "); out_hex(fbi_g->addr); out("\n"); }
	else out("not found, serial only\n");
	colour = DIM; out("  untyped     "); colour = FG; out_dec(untyped_n); out(" regions\n");
	colour = DIM; out("  devices     "); colour = FG; out(io_report);
	out(reflex ? "; keyboard felt by Sakshi itself (reflex)\n" : "; keyboard held by Tvak, its own organ\n");
}

static void explain(Turiya::Verdict v, const char *type)
{
	colour = DIM;
	switch (v) {
	case Turiya::Verdict::ALLOWED:
		out("  within your mandate. No service is connected yet, so nothing runs.\n"); break;
	case Turiya::Verdict::ALLOWED_APPROVED:
		out("  approved by you, for exactly this action. It would now go to its service,\n"
		    "  which does not exist yet: nothing was paid, sent or deleted.\n"); break;
	case Turiya::Verdict::HELD_NEEDS_APPROVAL:
		out("  irreversible: it needs your approval. Type 'approve' to approve exactly this.\n"); break;
	case Turiya::Verdict::HELD_MISMATCH:
		out("  the approval does not match this exact action.\n"); break;
	case Turiya::Verdict::HELD_BAD_INPUT:
		out("  malformed: a '|' or control character, an empty target, or a field over 63 characters.\n"); break;
	case Turiya::Verdict::BLOCKED_OUTSIDE_MANDATE:
		out("  '"); out(type); out("' is not in your mandate (summarise, draft_reply, add_reminder)\n"
		    "  and is not a known irreversible action, so it is blocked.\n"); break;
	case Turiya::Verdict::HELD_DREAMING:
		out("  dreaming: Turiya may think but not act. Kept as a plan; wake ('state jagrat') and ask again.\n"); break;
	case Turiya::Verdict::HELD_ASLEEP:
		out("  deep sleep: nothing acts. Type 'state jagrat' to wake.\n"); break;
	case Turiya::Verdict::HELD_OUTSIDE_TARGET:
		out("  the target came from outside content: it needs you.\n"); break;
	}
	colour = FG;
}

static void hexify(const Turiya::u8 *b, unsigned n, char *hx)
{
	for (unsigned i = 0; i < n; i++) { hx[2*i] = "0123456789abcdef"[b[i] >> 4]; hx[2*i+1] = "0123456789abcdef"[b[i] & 15]; }
	hx[2*n] = 0;
}

/* ---- organs: compartments with one duty (svadharma), fixed limits (maryada) and recovery (cikitsa) ---- */
enum : seL4_Word { ORG_FAULT = 0x1000, ORG_MSG = 0x2000 };          /* endpoint badges: kind | organ id */
enum : seL4_Word { L_KEY = 1, L_READY = 2, L_DONE = 3, L_PLUG = 5, L_UNPLUG = 6, L_NEED = 7 }; /* what an organ may say */
enum : seL4_Word { N_IRQ = 1, N_PULSE = 2, N_REQ = 4, N_GIVE = 8, N_STOP = 0x40, N_FALL = 0x80 }; /* bits on an organ's notification */
enum : uptr { ORG_TLS = 0x5000000, ORG_IPC = 0x6000000, ORG_SCRATCH = 0x10000000 };

struct elf_ehdr { unsigned char ident[16]; uint16_t type, machine; uint32_t version; uint64_t entry, phoff, shoff;
                  uint32_t flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct elf_phdr { uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align; };

struct organ_page { uptr va; seL4_CPtr frame; bool w; };

struct organ
{
	const char *name; seL4_Word id;
	const unsigned char *seed; unsigned long seed_len; const char *seed_sha;
	seL4_CPtr tcb, cnode, pml4, ntfn, irq, ipc, tls, pulse, fall, req, stop, give;
	organ_page pages[32]; unsigned npages, ncode;
	uptr entry, scratch;
	unsigned long gen, falls, heals, irqs, last_heal_us, typed;
	unsigned long fell_at[4];          /* heartbeat (+1) of its last four falls */
	uint64_t t_fall;
	seL4_Word fell_label, fell_ip, fell_addr;   /* what seL4 said about its last fall */
	bool untold;                       /* healed first: the report of the fall is still to be written */
	bool made, awake, resting, present;
	const char *step; long err;        /* where making it failed, if it did */
};

static organ tvak { "Tvak", 1, TVAK_SEED, TVAK_LEN, TVAK_SHA256 };
static organ kosa { "Kosa", 2, KOSA_SEED, KOSA_LEN, KOSA_SHA256 };
static organ hrdaya { "Hrdaya", 3, HRDAYA_SEED, HRDAYA_LEN, HRDAYA_SHA256 };
static organ nadi { "Nadi", 4, NADI_SEED, NADI_LEN, NADI_SHA256 };
static organ nadi2 { "Nadi2", 5, NADI_SEED, NADI_LEN, NADI_SHA256 };   /* the second USB controller, same seed */
static organ *const NADIS[2] = { &nadi, &nadi2 };
static NadiWindow *nwins[2];              /* Sakshi's views of each Nadi's window (set when made) */
static int nadi_index(const organ &o) { return &o == &nadi ? 0 : &o == &nadi2 ? 1 : -1; }
static organ smrti { "Smrti", 6, SMRTI_SEED, SMRTI_LEN, SMRTI_SHA256 };
static organ *organ_by_id(seL4_Word id)
{
	return id == tvak.id ? &tvak : id == kosa.id ? &kosa : id == hrdaya.id ? &hrdaya : id == nadi.id ? &nadi
	     : id == nadi2.id ? &nadi2 : id == smrti.id ? &smrti : nullptr;
}
static uint64_t tsc_hz;                /* processor clock: from the kernel, else measured against the heart */
static bool tsc_known;

static uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

static bool seed_ok(organ &o)
{
	Turiya::Sha256 s; s.add(o.seed, o.seed_len);
	Turiya::u8 d[32]; s.finish(d);
	char hx[65]; hexify(d, 32, hx);
	return streq(hx, o.seed_sha);
}

static const elf_phdr *phdr(organ &o, unsigned i)
{
	const elf_ehdr *e = (const elf_ehdr *)o.seed;
	return (const elf_phdr *)(o.seed + e->phoff + (uptr)i * e->phentsize);
}

static bool fail(organ &o, const char *step, long err) { o.step = step; o.err = err; return false; }

/* give the organ its body: TCB, CSpace, VSpace, pages, and exactly the capabilities its duty needs */
enum : seL4_Word { NO_IRQ = ~0UL };
static bool make_organ(organ &o, seL4_CPtr ports, seL4_Word irq_pin, seL4_Word irq_vector, seL4_Word prio)
{
	if (!seed_ok(o)) return fail(o, "seed check", 1);
	const elf_ehdr *e = (const elf_ehdr *)o.seed;
	if (o.seed_len < sizeof *e || e->ident[0] != 0x7f || e->ident[1] != 'E' || e->ident[4] != 2 || e->machine != 62)
		return fail(o, "seed is not an x86-64 ELF", 2);
	o.entry = e->entry;
	for (unsigned i = 0; i < e->phnum; i++) {
		const elf_phdr *ph = phdr(o, i);
		if (ph->type != 1 || !ph->memsz) continue;
		if (ph->offset + ph->filesz > o.seed_len) return fail(o, "segment outside the seed", 3);
		for (uptr va = ph->vaddr & ~0xfffUL; va < ph->vaddr + ph->memsz; va += 4096) {
			unsigned k = 0;
			while (k < o.npages && o.pages[k].va != va) k++;
			if (k == o.npages) {
				if (o.npages == 32 || va >= ORG_TLS) return fail(o, "too many pages", 4);
				o.pages[o.npages++] = { va, 0, false };
			}
			if (ph->flags & 2) o.pages[k].w = true;
		}
	}
	o.scratch = ORG_SCRATCH + o.id * 0x1000000UL;
	long r;
	o.tcb = alloc_slot(); o.cnode = alloc_slot(); o.pml4 = alloc_slot(); o.ntfn = alloc_slot();
	o.ipc = alloc_slot(); o.tls = alloc_slot();
	if (!o.tls) return fail(o, "slots", 5);
	if ((r = retype(ram_ut, seL4_TCBObject, 0, o.tcb, 1)))           return fail(o, "TCB", r);
	if ((r = retype(ram_ut, seL4_CapTableObject, 6, o.cnode, 1)))     return fail(o, "CNode", r);
	if ((r = retype(ram_ut, seL4_X64_PML4Object, 0, o.pml4, 1)))      return fail(o, "PML4", r);
	if ((r = seL4_X86_ASIDPool_Assign(seL4_CapInitThreadASIDPool, o.pml4))) return fail(o, "ASID", r);
	if ((r = retype(ram_ut, seL4_NotificationObject, 0, o.ntfn, 1))) return fail(o, "notification", r);
	if ((r = retype(ram_ut, seL4_X86_4K, 0, o.ipc, 1)))               return fail(o, "IPC frame", r);
	if ((r = retype(ram_ut, seL4_X86_4K, 0, o.tls, 1)))               return fail(o, "TLS frame", r);

	/* memory: every page is seen twice, by Sakshi (to re-make it) and by the organ (code read-only) */
	for (unsigned k = 0; k <= o.npages; k++) {
		bool is_tls = k == o.npages;
		seL4_CPtr f = is_tls ? o.tls : alloc_slot(), c = alloc_slot();
		if (!f || !c) return fail(o, "slots", 6);
		if (!is_tls && (r = retype(ram_ut, seL4_X86_4K, 0, f, 1))) return fail(o, "frame", r);
		if ((r = map_in(seL4_CapInitThreadVSpace, f, o.scratch + k * 4096UL, seL4_ReadWrite, seL4_X86_Default_VMAttributes)))
			return fail(o, "Sakshi's view", r);
		if ((r = seL4_CNode_Copy(seL4_CapInitThreadCNode, c, seL4_WordBits, seL4_CapInitThreadCNode, f, seL4_WordBits, seL4_AllRights)))
			return fail(o, "frame copy", r);
		bool w = is_tls || o.pages[k].w;
		if ((r = map_in(o.pml4, c, is_tls ? (uptr)ORG_TLS : o.pages[k].va, w ? seL4_ReadWrite : seL4_CanRead,
		                seL4_X86_Default_VMAttributes)))
			return fail(o, "organ's view", r);
		if (!is_tls) { o.pages[k].frame = f; if (!w) o.ncode++; }
	}
	if ((r = map_in(o.pml4, o.ipc, ORG_IPC, seL4_ReadWrite, seL4_X86_Default_VMAttributes))) return fail(o, "IPC map", r);

	/* capabilities: slot 1 faults, 2 channel out, 3 its notification, then only what its duty needs
	 * (4 its IRQ, 5 its ports). Nothing else. */
	seL4_CPtr irq = alloc_slot(), nb = alloc_slot(); o.pulse = alloc_slot(); o.fall = alloc_slot(); o.req = alloc_slot(); o.stop = alloc_slot(); o.give = alloc_slot();
	if (!o.give) return fail(o, "slots", 7);
	if ((r = seL4_CNode_Mint(o.cnode, 1, 6, seL4_CapInitThreadCNode, ep, seL4_WordBits,
	                         seL4_CapRights_new(1, 0, 0, 1), ORG_FAULT | o.id)))          return fail(o, "fault channel", r);
	if ((r = seL4_CNode_Mint(o.cnode, 2, 6, seL4_CapInitThreadCNode, ep, seL4_WordBits,
	                         seL4_CapRights_new(0, 0, 0, 1), ORG_MSG | o.id)))            return fail(o, "channel out", r);
	if ((r = seL4_CNode_Copy(o.cnode, 3, 6, seL4_CapInitThreadCNode, o.ntfn, seL4_WordBits, seL4_CanRead)))
		return fail(o, "its notification", r);
	if (irq_pin != NO_IRQ) {
		if ((r = seL4_IRQControl_GetIOAPIC(seL4_CapIRQControl, seL4_CapInitThreadCNode, irq, seL4_WordBits,
		                                   0, irq_pin, 0 /* edge */, 0 /* active high */, irq_vector))) return fail(o, "IRQ", r);
		if ((r = seL4_CNode_Mint(seL4_CapInitThreadCNode, nb, seL4_WordBits, seL4_CapInitThreadCNode, o.ntfn, seL4_WordBits,
		                         seL4_CanWrite, N_IRQ)))                                     return fail(o, "IRQ badge", r);
		if ((r = seL4_IRQHandler_SetNotification(irq, nb)))                                  return fail(o, "IRQ notification", r);
		if ((r = seL4_CNode_Copy(o.cnode, 4, 6, seL4_CapInitThreadCNode, irq, seL4_WordBits, seL4_AllRights)))
			return fail(o, "IRQ copy", r);
		o.irq = irq;
	}
	if (ports && (r = seL4_CNode_Copy(o.cnode, 5, 6, seL4_CapInitThreadCNode, ports, seL4_WordBits, seL4_AllRights)))
		return fail(o, "ports", r);
	if ((r = seL4_CNode_Mint(seL4_CapInitThreadCNode, o.req, seL4_WordBits, seL4_CapInitThreadCNode, o.ntfn, seL4_WordBits,
	                         seL4_CanWrite, N_REQ)))                                     return fail(o, "request", r);
	if ((r = seL4_CNode_Mint(seL4_CapInitThreadCNode, o.stop, seL4_WordBits, seL4_CapInitThreadCNode, o.ntfn, seL4_WordBits,
	                         seL4_CanWrite, N_STOP)))                                    return fail(o, "stop", r);
	if ((r = seL4_CNode_Mint(seL4_CapInitThreadCNode, o.give, seL4_WordBits, seL4_CapInitThreadCNode, o.ntfn, seL4_WordBits,
	                         seL4_CanWrite, N_GIVE)))                                    return fail(o, "give", r);
	if ((r = seL4_CNode_Mint(seL4_CapInitThreadCNode, o.pulse, seL4_WordBits, seL4_CapInitThreadCNode, o.ntfn, seL4_WordBits,
	                         seL4_CanWrite, N_PULSE)))                                   return fail(o, "pulse", r);
	if ((r = seL4_CNode_Mint(seL4_CapInitThreadCNode, o.fall, seL4_WordBits, seL4_CapInitThreadCNode, o.ntfn, seL4_WordBits,
	                         seL4_CanWrite, N_FALL)))                                    return fail(o, "fall", r);
	if ((r = seL4_TCB_Configure(o.tcb, 1, o.cnode, seL4_CNode_CapData_new(0, seL4_WordBits - 6).words[0],
	                            o.pml4, 0, ORG_IPC, o.ipc)))                             return fail(o, "configure", r);
	if ((r = seL4_TCB_SetPriority(o.tcb, seL4_CapInitThreadTCB, prio)))                   return fail(o, "priority", r);
	if ((r = seL4_TCB_SetTLSBase(o.tcb, ORG_TLS + 0x800)))                               return fail(o, "TLS base", r);
	return true;
}

/* re-make the organ's memory from its seed: the seed is checked first, every page is rewritten */
static bool remake(organ &o)
{
	if (!seed_ok(o)) return false;
	for (unsigned k = 0; k <= o.npages; k++) memset((void *)(o.scratch + k * 4096UL), 0, 4096);
	const elf_ehdr *e = (const elf_ehdr *)o.seed;
	for (unsigned i = 0; i < e->phnum; i++) {
		const elf_phdr *ph = phdr(o, i);
		if (ph->type != 1) continue;
		for (uptr off = 0; off < ph->filesz; ) {
			uptr va = ph->vaddr + off, pg = va & ~0xfffUL;
			unsigned k = 0;
			while (k < o.npages && o.pages[k].va != pg) k++;
			uptr n = 4096 - (va & 0xfff);
			if (n > ph->filesz - off) n = ph->filesz - off;
			memcpy((void *)(o.scratch + k * 4096UL + (va & 0xfff)), o.seed + ph->offset + off, n);
			off += n;
		}
	}
	/* thread pointer: x86-64 TLS variant II, the word at the thread pointer points to itself */
	*(volatile uptr *)(o.scratch + o.npages * 4096UL + 0x800) = ORG_TLS + 0x800;
	return true;
}

static long start(organ &o)
{
	seL4_UserContext ctx; memset(&ctx, 0, sizeof ctx);
	ctx.rip = o.entry; ctx.rflags = 0x202; ctx.rdi = ++o.gen; ctx.rsi = ORG_IPC;
	return seL4_TCB_WriteRegisters(o.tcb, 1, 0, 18, &ctx);
}

static void reflex_on(void)
{
	reflex = true;
	reflex_dec = Kbd::Decoder();
	reflex_ready = kbd_port && Kbd::init(sakshi_port);
}

static void organ_rest(organ &o, const char *why)
{
	seL4_TCB_Suspend(o.tcb);
	o.resting = true; o.awake = false; o.t_fall = 0;
	colour = SAFFRON; out("  "); out(o.name); out(" rests: "); out(why); out(".\n");
	colour = DIM;
	if (&o == &tvak) { out("  Sakshi feels the keyboard itself (reflex) until you type 'heal tvak'.\n"); reflex_on(); }
	else { out("  it stays asleep until you type 'heal "); out(o.id == 2 ? "kosa" : o.id == 3 ? "hrdaya" : o.id == 4 ? "nadi" : o.id == 5 ? "nadi2" : o.id == 6 ? "smrti" : "?"); out("'.\n"); }
	colour = FG;
	hold_prompt = false; interject_end(); status_bar();
}

/* the breath saw an organ go silent: stop it, re-make it from its seed, start it again */
static void organ_revive(organ &o, const char *why)
{
	seL4_TCB_Suspend(o.tcb);
	o.awake = false; o.t_fall = rdtsc(); o.falls++;
	interject_begin();
	colour = RED; out("  "); out(o.name); out(" stopped: "); out(why); out("\n");
	if (!remake(o) || start(o)) { organ_rest(o, "it could not be re-made"); return; }
	colour = DIM; out("  re-made from its seed (SHA-256 checked) and started again\n"); colour = FG;
	if (!hold_prompt) { hold_prompt = true; hold_since = beats; }
}

static void crash_organ(organ &o)
{
	if (!o.made || !o.awake) { colour = DIM; out("  "); out(o.name); out(" is not awake.\n"); colour = FG; return; }
	colour = DIM; out("  asking "); out(o.name); out(" to write to the screen at 0x8000000000, which only Sakshi holds\n"); colour = FG;
	hold_prompt = true; hold_since = beats;
	seL4_Signal(o.fall);
}

static void heal_organ(organ &o)
{
	colour = DIM;
	if (!o.made) { out("  "); out(o.name); out(" was never made, so it cannot be healed.\n"); colour = FG; return; }
	if (!o.resting) { out("  "); out(o.name); out(" is not resting.\n"); colour = FG; return; }
	if (!remake(o)) { colour = RED; out("  its seed failed the SHA-256 check: not starting it.\n"); colour = FG; return; }
	o.resting = false;
	if (o.id == 1) reflex = false;
	for (int i = 0; i < 4; i++) o.fell_at[i] = 0;
	o.t_fall = rdtsc();
	if (start(o)) { o.resting = true; if (o.id == 1) reflex = true; colour = RED; out("  it could not be started.\n"); colour = FG; return; }
	out("  re-made from its seed (SHA-256 checked) and woken\n"); colour = FG;
	hold_prompt = true; hold_since = beats;
}

/* what the status bar says about the keyboard organ */
static char organ_line[96];
static const char *organ_state(void)
{
	if (!tvak.made) return "keyboard: Sakshi (reflex)";
	if (tvak.resting) return "Tvak resting, Sakshi feels the keys";
	if (!tvak.awake) return tvak.falls ? "Tvak healing" : "Tvak waking";
	unsigned k = 0;
	auto add = [&](const char *s) { while (*s && k < sizeof organ_line - 1) organ_line[k++] = *s++; };
	auto addn = [&](unsigned long v) { char b[24]; int i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); add(b + i); };
	add("Tvak awake"); add(tvak.irqs ? " (by interrupt)" : (keys ? " (on heartbeats)" : ""));
	if (tvak.heals) { add(", healed "); addn(tvak.heals); }
	organ_line[k] = 0;
	return organ_line;
}

static void show_organs(void)
{
	colour = SAFFRON; out("  Sakshi   "); colour = FG; out("witness: the screen, the gate and your approvals\n");
	colour = DIM;     out("           holds what seL4 gave at boot; makes the organs and heals them\n");
	colour = SAFFRON; out("  Tvak     "); colour = FG; out("touch: key presses into characters for Sakshi\n");
	colour = DIM;
	if (!tvak.made) {
		out("           could not be made (step: "); out(tvak.step ? tvak.step : "?"); out(", error "); out_dec((unsigned long)tvak.err);
		out("); Sakshi feels the keys itself\n");
	} else {
		out("           holds ports 0x60-0x64, IRQ 1, one channel out, "); out_dec(tvak.npages); out(" pages (");
		out_dec(tvak.ncode); out(" read-only), TLS, IPC buffer\n");
		out("           "); out(tvak.resting ? "resting" : (tvak.awake ? "awake" : "healing"));
		out(", generation "); out_dec(tvak.gen); out("; fell "); out_dec(tvak.falls); out(", healed "); out_dec(tvak.heals);
		if (tvak.heals && tvak.last_heal_us) { out(" (last in "); out_dec(tvak.last_heal_us); out(" us)"); }
		out("\n");
		out("           seed: "); out_dec(tvak.seed_len); out(" bytes, SHA-256 ");
		{ char s8[17]; memcpy(s8, tvak.seed_sha, 16); s8[16] = 0; out(s8); } out("..., checked before every start\n");
	}
	colour = SAFFRON; out("  Kosa     "); colour = FG; out("storehouse: reads the NVMe SSD; it can only read\n");
	colour = DIM;
	if (!kosa.made) out("           made only when you type 'disk'\n");
	else {
		out("           holds 3 capabilities, the controller's registers, 6 DMA pages, 1 window; no IRQ\n");
		out("           "); out(kosa.resting ? "resting" : (kosa.awake ? "awake" : "healing"));
		out(", generation "); out_dec(kosa.gen); out("; fell "); out_dec(kosa.falls); out(", healed "); out_dec(kosa.heals); out("\n");
	}
	colour = SAFFRON; out("  Nadi     "); colour = FG; out("channel: the USB controller; names devices, listens to a USB keyboard\n");
	colour = DIM;
	if (!nadi.made) out("           made only when you type 'usb' (one per USB 3 controller)\n");
	for (int k = 0; k < 2; k++) {
		organ &n = *NADIS[k];
		if (!n.made) continue;
		out("           "); out(n.name); out(": 3 capabilities, its controller's registers, 112 DMA pages, 1 window; no IRQ\n");
		out("           "); out(n.resting ? "resting" : (n.awake ? "awake" : "healing")); out(", generation "); out_dec(n.gen);
		out("; fell "); out_dec(n.falls); out(", healed "); out_dec(n.heals); out("; keys typed "); out_dec(n.typed); out("\n");
	}
	colour = SAFFRON; out("  Smrti    "); colour = FG; out("memory: reads a FAT32 file system; it touches no device, Sakshi fetches blocks\n");
	colour = DIM;
	if (!smrti.made) out("           made only when you type 'files'\n");
	else { out("           holds 3 capabilities, a read-only view of the stick's data page, 1 window\n");
	       out("           "); out(smrti.resting ? "resting" : (smrti.awake ? "awake" : "healing")); out(", generation "); out_dec(smrti.gen);
	       out("; fell "); out_dec(smrti.falls); out(", healed "); out_dec(smrti.heals); out("\n"); }
	colour = SAFFRON; out("  Hrdaya   "); colour = FG; out("heart: the PIT timer at 50 Hz, one signal to Sakshi per tick\n");
	colour = DIM;
	if (!hrdaya.made) { out("           not made ("); out(hrdaya.step ? hrdaya.step : "?"); out("); Sakshi keeps the timer itself\n"); }
	else {
		out("           holds ports 0x40-0x43, IRQ 0, one signal into Sakshi; "); out_dec(beats); out(" beats\n");
		out("           "); out(hrdaya.resting ? "resting" : (hrdaya.awake ? "awake" : "healing"));
		out(", generation "); out_dec(hrdaya.gen); out("; fell or stopped "); out_dec(hrdaya.falls); out(", healed "); out_dec(hrdaya.heals); out("\n");
	}
	colour = SAFFRON; out("  Prana    "); colour = FG; out("breath, Sakshi's own: the RTC at 2 Hz, it watches the heart\n");
	colour = DIM; out("           "); out_dec(breaths); out(breaths ? " breaths\n" : " breaths: no RTC interrupt seen yet\n"); colour = FG;
}

/* the body survey and power-off (read-only PCI and ACPI) */
static uint8_t rsdp_copy[36];
static bool have_rsdp;
#include "sharira.h"

static void do_reboot(void)
{
	colour = SAFFRON; out("  restarting...\n");
	if (kbd_wait_write()) outb(kbd_port, 0x64, 0xfe);
	for (int i = 0; i < 1000000; i++) __asm__ volatile ("pause");
	outb(cf9_port, 0xcf9, 0x02); outb(cf9_port, 0xcf9, 0x06);
	for (int i = 0; i < 1000000; i++) __asm__ volatile ("pause");
	colour = RED; out("  the machine did not restart: hold the power button.\n"); colour = FG;
}

static void clear_screen(void)
{
	for (u32b y = 0; y < rows; y++) { memset(cell_ch[y], ' ', cols); for (u32b x = 0; x < cols; x++) cell_col[y][x] = FG; }
	cur_x = cur_y = 0;
	if (fb) fill(0, 0, fb_w, MARGIN + rows * CH, BG);
}

static void do_disk(void);
static void do_usb(void);
static void do_stick(void);
static void do_files(const char *path, unsigned mode);
static void do_check(void);

static void execute(char *l)
{
	/* files [folder] and read <file>: the rest of the line is a path, spaces and all */
	{
		const char *p = l; while (*p == ' ') p++;
		bool f = p[0] == 'f' && p[1] == 'i' && p[2] == 'l' && p[3] == 'e' && p[4] == 's' && (!p[5] || p[5] == ' ');
		bool r = p[0] == 'r' && p[1] == 'e' && p[2] == 'a' && p[3] == 'd' && (!p[4] || p[4] == ' ');
		if (f || r) {
			p += f ? 5 : 4;
			while (*p == ' ') p++;
			if (r && !*p) { colour = DIM; out("  read what? e.g.  read HELLO.TXT\n"); colour = FG; return; }
			do_files(p, r ? 1 : 0);
			return;
		}
	}
	/* split into at most 4 words */
	char *w[4] = { 0, 0, 0, 0 }; int n = 0;
	for (char *p = l; *p && n < 4; ) {
		while (*p == ' ') *p++ = 0;
		if (!*p) break;
		w[n++] = p;
		while (*p && *p != ' ') p++;
	}
	if (!n) return;
	if (streq(w[0], "help")) { help(); return; }
	if (streq(w[0], "about")) { about(); return; }
	if (streq(w[0], "gate")) { run_gate(); return; }
	if (streq(w[0], "clear")) { clear_screen(); return; }
	if (streq(w[0], "reboot")) { do_reboot(); return; }
	if (streq(w[0], "organs")) { show_organs(); return; }
	if (streq(w[0], "body")) { show_body(); return; }
	if (streq(w[0], "disk")) { do_disk(); return; }
	if (streq(w[0], "usb")) { do_usb(); return; }
	if (streq(w[0], "stick")) { do_stick(); return; }
	if (streq(w[0], "check")) { do_check(); return; }
	if (streq(w[0], "state")) {
		if (w[1]) {
			if (streq(w[1], "jagrat") || streq(w[1], "wake")) avastha = Turiya::Avastha::JAGRAT;
			else if (streq(w[1], "svapna") || streq(w[1], "dream")) avastha = Turiya::Avastha::SVAPNA;
			else if (streq(w[1], "susupti") || streq(w[1], "sleep")) avastha = Turiya::Avastha::SUSUPTI;
			else { colour = DIM; out("  states: jagrat (wake), svapna (dream), susupti (sleep)\n"); colour = FG; return; }
			dreaming_idle = false; status_bar();
		}
		colour = DIM; out("  state: "); colour = SAFFRON; out(avastha_name(avastha)); colour = DIM; out("\n");
		out("  jagrat: you are here; the gate's rules apply.  svapna: Turiya may think (summarise) but nothing\n");
		out("  leaves a mark, not even with an approval; it keeps plans.  susupti: nothing acts.  turiya: the witness,\n");
		out("  the gate and its log, present in all three. After 10 minutes without a key, Turiya dreams.\n");
		colour = FG; return;
	}
	if (streq(w[0], "poweroff")) { do_poweroff(); return; }
	if (streq(w[0], "crash") || streq(w[0], "heal") || streq(w[0], "stop")) {
		organ *t = !w[1] ? nullptr : streq(w[1], "tvak") ? &tvak : streq(w[1], "kosa") ? &kosa : streq(w[1], "nadi") ? &nadi : streq(w[1], "nadi2") ? &nadi2 : streq(w[1], "smrti") ? &smrti
		         : (streq(w[1], "hrdaya") || streq(w[1], "heart")) ? &hrdaya : nullptr;
		if (t && w[0][0] == 's') {
			if (!t->made || !t->awake) { colour = DIM; out("  "); out(t->name); out(" is not awake.\n"); colour = FG; return; }
			colour = DIM; out("  asking "); out(t->name); out(" to go silent without falling; the breath should notice\n"); colour = FG;
			hold_prompt = true; hold_since = beats; seL4_Signal(t->stop); return;
		}
		if (t) { if (w[0][0] == 'c') crash_organ(*t); else heal_organ(*t); return; }
		colour = DIM; out("  only the organs (tvak, kosa, hrdaya, nadi, nadi2, smrti). Sakshi is the witness: if it fell, nothing would be left to heal it.\n");
		colour = FG; return;
	}
	if (streq(w[0], "mail")) {
		colour = DIM; out("  outside content the gate has seen (a phishing email):\n"); colour = FG;
		out("  "); for (const char *p = OUTSIDE; *p; p++) { if (*p == '\n') out("\n  "); else { char c[2] = { *p, 0 }; out(c); } }
		out("\n"); return;
	}
	if (streq(w[0], "log")) {
		colour = DIM; out("  two judges, the C++ rule and the Verus-proved decision, agreed "); out_dec(judges_agree);
		out(" of "); out_dec(judges_agree + judges_differ); out(" times this session\n");
		colour = DIM; out("  witness log: "); out_dec(log_total); out(" decisions this session, last ");
		unsigned k = log_n < 12 ? log_n : 12; out_dec(k); out(" shown\n"); colour = FG;
		for (unsigned i = log_n - k; i < log_n; i++) {
			entry &e = kartr_log[i % 64];
			show_verdict(0, e.type, e.target, e.amount, e.v);
		}
		return;
	}
	if (streq(w[0], "approve")) {
		if (!have_last) { colour = DIM; out("  nothing is waiting for approval.\n"); colour = FG; return; }
		char msg[3 * 64 + 3]; unsigned m = 0;
		for (const char *p = last_type; *p; p++) msg[m++] = *p;
		msg[m++] = '|';
		for (const char *p = last_target; *p; p++) msg[m++] = *p;
		msg[m++] = '|';
		for (const char *p = last_amount; *p; p++) msg[m++] = *p;
		Turiya::u8 mac[32]; Turiya::hmac(key, klen, msg, m, mac);
		char hex[65]; hexify(mac, 32, hex);
		Turiya::Verdict v = judge(last_type, last_target, last_amount, hex, false, nullptr, avastha);
		record(last_type, last_target, last_amount, v);
		show_verdict(0, last_type, last_target, last_amount, v); explain(v, last_type);
		colour = DIM; out("  approval: HMAC-SHA256 of type|target|amount with the test key\n  "); out(hex); out("\n"); colour = FG;
		have_last = false;
		return;
	}
	/* anything else is an action for the gate: <type> <target> [amount] */
	const char *type = w[0], *target = w[1] ? w[1] : "", *amount = w[2] ? w[2] : "0";
	bool trunc = slen(target) > Turiya::FIELD_MAX;
	Turiya::Verdict v = judge(type, target, amount, "", trunc, nullptr, avastha);
	if (v == Turiya::Verdict::HELD_DREAMING) dream_plans++;
	record(type, target, amount, v);
	show_verdict(0, type, target, amount, v); explain(v, type);
	if (v == Turiya::Verdict::HELD_NEEDS_APPROVAL && slen(type) < sizeof last_type && slen(target) < sizeof last_target
	    && slen(amount) < sizeof last_amount) {
		memcpy(last_type, type, slen(type) + 1); memcpy(last_target, target, slen(target) + 1);
		memcpy(last_amount, amount, slen(amount) + 1); have_last = true;
	}
}

static void key_char(char c)
{
	cursor(false);
	if (c == '\n') {
		out("\n"); line[line_len] = 0; prompt_shown = false;
		execute(line); line_len = 0;
		if (!hold_prompt) prompt();
		return;
	}
	if (c == 8) {
		if (line_len && cur_x) { line_len--; cur_x--; cell_ch[cur_y][cur_x] = ' '; draw_cell(cur_x, cur_y); sput("\b \b"); }
		cursor(true); return;
	}
	if (c >= 32 && c < 127 && line_len < sizeof line - 1) { line[line_len++] = c; char s[2] = { c, 0 }; out(s); }
	cursor(true);
}

/* the reflex: only while Tvak cannot be made or is resting */
static void reflex_poll(void)
{
	if (reflex && reflex_ready) Kbd::drain(sakshi_port, reflex_dec, [](char c) { keys++; key_char(c); });
}

/* what seL4 said about a fall, written once the organ is running again, so healing never waits for the screen */
static void tell_fall(organ &o, bool restarted)
{
	interject_begin();
	colour = RED; out("  "); out(o.name); out(" fell: ");
	out(o.fell_label == seL4_Fault_VMFault ? "it touched memory it does not hold"
	  : o.fell_label == seL4_Fault_UserException ? "a processor exception"
	  : o.fell_label == seL4_Fault_CapFault ? "it used a capability it does not hold" : "an unknown fault");
	out(" (ip "); out_hex(o.fell_ip);
	if (o.fell_label == seL4_Fault_VMFault) { out(", address "); out_hex(o.fell_addr); }
	out(")\n");
	colour = DIM; out("  seL4 stopped it at that instruction; nothing outside its own pages was touched.\n");
	if (restarted) out("  re-made from its seed (SHA-256 checked) and started again\n");
	colour = FG;
	o.untold = false;
}

/* an organ fell: seL4 stopped it and told us. Re-make it from its seed and start it again, within limits;
   heal first, tell after: it is running again before Sakshi writes a word. */
static void organ_fell(organ &o, seL4_MessageInfo_t info)
{
	o.t_fall = rdtsc(); o.awake = false; o.falls++;
	o.fell_label = seL4_MessageInfo_get_label(info);
	o.fell_ip = seL4_GetMR(0);
	o.fell_addr = o.fell_label == seL4_Fault_VMFault ? seL4_GetMR(seL4_VMFault_Addr) : 0;
	for (int i = 3; i > 0; i--) o.fell_at[i] = o.fell_at[i - 1];
	o.fell_at[0] = beats + 1;
	if (o.fell_at[3] && o.fell_at[0] - o.fell_at[3] <= 30) { tell_fall(o, false); organ_rest(o, "it fell 4 times within 30 seconds"); return; }
	if (!remake(o)) { tell_fall(o, false); organ_rest(o, "its seed failed the SHA-256 check"); return; }
	if (start(o))   { tell_fall(o, false); organ_rest(o, "it could not be started again"); return; }
	o.untold = true;
	if (!hold_prompt) { hold_prompt = true; hold_since = beats; }   /* the prompt returns when it says it is ready */
}

static void organ_spoke(organ &o, seL4_MessageInfo_t info)
{
	seL4_Word label = seL4_MessageInfo_get_label(info), a = seL4_GetMR(0), b = seL4_GetMR(1);
	if (label == L_KEY) {
		if (!o.awake || o.resting) return;
		o.irqs = b; keys++; o.typed++;
		last_key_beat = beats;
		if (dreaming_idle) {
			dreaming_idle = false; avastha = Turiya::Avastha::JAGRAT;
			interject_begin(); colour = GREEN; out("  welcome back: Turiya is awake (jagrat)");
			colour = DIM; out(dream_plans ? ". Plans kept while dreaming are in 'log'.\n" : ". Nothing was asked while you were away.\n");
			colour = FG; dream_plans = 0; interject_end(); status_bar();
		}
		bool first = keys == 1;
		key_char((char)a);
		if (first) status_bar();
		return;
	}
	if (label == L_PLUG || label == L_UNPLUG) {                       /* Nadi: something plugged in or pulled out */
		interject_begin();
		colour = DIM; out("  USB port "); out_dec(a); colour = FG;
		int k = nadi_index(o);
		NadiWindow *nw = k >= 0 ? nwins[k] : nullptr;
		if (label == L_PLUG && nw && b < NADI_MAXDEV) {
			out(": "); out(nw->dev[b].what); out(" plugged in");
			if (nw->keyboard_slot && nw->dev[b].slot == nw->keyboard_slot) { colour = GREEN; out(" - listening: type on it"); }
			if (nw->storage_slot && nw->dev[b].slot == nw->storage_slot) { colour = GREEN; out(" - type 'stick' to read it"); }
		} else out(": unplugged");
		out("\n"); colour = FG; interject_end();
		return;
	}
	if (label == L_READY) {
		bool healing = o.t_fall != 0;
		o.awake = true; o.present = b;
		if (healing) {
			uint64_t dt = rdtsc() - o.t_fall; o.t_fall = 0; o.heals++;
			o.last_heal_us = tsc_hz ? (unsigned long)(dt * 1000000 / tsc_hz) : 0;
			interject_begin();
			if (o.untold) tell_fall(o, true);
			colour = GREEN; out("  "); out(o.name); out(" healed: awake again");
			if (tsc_hz) { out(" in "); out_dec(o.last_heal_us); out(" microseconds"); }
			out(", generation "); out_dec(o.gen); out(o.id == 1 ? ". Type to feel it.\n" : ".\n"); colour = FG;
			hold_prompt = false; interject_end();
		}
		status_bar();
	}
}

#include "kosa_host.h"
#include "nadi_host.h"
#include "smrti_host.h"
#include "check_host.h"

static void cpu_name(char *nm)
{
	for (u32b leaf = 0; leaf < 3; leaf++) {
		u32b r[4];
		__asm__ volatile ("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(0x80000002 + leaf), "c"(0));
		memcpy(nm + leaf * 16, r, 16);
	}
	nm[48] = 0;
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

	seL4_Word best = 0;
	untyped_n = (unsigned)(bi->untyped.end - bi->untyped.start);
	for (seL4_Word i = 0; i < untyped_n; i++) {
		seL4_UntypedDesc *u = &bi->untypedList[i];
		if (!u->isDevice) { ram_mib += (1UL << u->sizeBits) >> 20; if (u->sizeBits > best) { best = u->sizeBits; ram_ut = bi->untyped.start + i; } }
	}

	/* the screen, from the extra boot info */
	struct fb_info *fbi = 0;
	char *x = (char *)bi + (1UL << seL4_PageBits), *xe = x + bi->extraLen;
	while (x < xe) {
		seL4_BootInfoHeader *h = (seL4_BootInfoHeader *)x;
		if (h->id == SEL4_BOOTINFO_HEADER_X86_FRAMEBUFFER) fbi = (struct fb_info *)(x + sizeof *h);
		if (h->id == SEL4_BOOTINFO_HEADER_X86_ACPI_RSDP && h->len >= sizeof *h + 36) { memcpy(rsdp_copy, x + sizeof *h, 36); have_rsdp = true; }
		if (h->id == SEL4_BOOTINFO_HEADER_X86_TSC_FREQ && h->len >= sizeof *h + 4) { tsc_hz = (uint64_t)*(uint32_t *)(x + sizeof *h) * 1000000; tsc_known = true; }
		if (!h->len) break;
		x += h->len;
	}
	fbi_g = fbi;
	if (fbi && fbi->width && (fbi->bpp == 32 || fbi->bpp == 24)) {
		fb_w = fbi->width; fb_h = fbi->height; fb_pitch = fbi->pitch;
		fb_bytes = fbi->pitch / fbi->width >= 4 ? 4 : 3;
		SCALE = fb_w >= 2400 ? 3 : (fb_w >= 1200 ? 2 : 1); CW = 8 * SCALE; CH = 16 * SCALE;
		if (!map_device(fbi->addr, (uptr)fbi->pitch * fbi->height, 0x8000000000UL))
			fb = (volatile unsigned char *)0x8000000000UL;
	}
	if (fb) {
		cols = (fb_w - 2 * MARGIN) / CW; rows = (fb_h - 2 * MARGIN) / CH - 2;
		if (cols > MAXC) cols = MAXC;
		if (rows > MAXR) rows = MAXR;
	}
	clear_screen();

	klen = Turiya::unhex(TEST_KEY, key, sizeof key);
	cpu_name(cpu);
	setup_io();

	/* the first organ: the keyboard leaves Sakshi and lives in its own compartment */
	if (ep && kbd_port && make_organ(tvak, kbd_port, 1, 1, 200)) {
		if (!remake(tvak)) fail(tvak, "first image", 8);
		else if (long r = start(tvak)) fail(tvak, "first start", r);
		else tvak.made = true;
	}
	/* the heart: its own organ, with one signal into Sakshi's notification; else Sakshi keeps time itself */
	if (ep && pit_port && make_organ(hrdaya, pit_port, 2, 2, 250)) {
		if (long r = seL4_CNode_Mint(hrdaya.cnode, 6, 6, seL4_CapInitThreadCNode, ntfn, seL4_WordBits, seL4_CanWrite, N_HEART))
			fail(hrdaya, "beat signal", r);
		else if (!remake(hrdaya)) fail(hrdaya, "first image", 8);
		else if (long r2 = start(hrdaya)) fail(hrdaya, "first start", r2);
		else hrdaya.made = true;
	}
	if (!hrdaya.made) heart_fallback();
	io_report = hrdaya.made ? (irq_rtc ? "heart: Hrdaya, its own organ; breath: RTC 2 Hz"
	                                   : "heart: Hrdaya, its own organ; no RTC breath")
	                        : (irq_pit ? "heart inside Sakshi (Hrdaya could not be made)" : "no heart interrupt");
	if (!tvak.made) {
		reflex_on();
		sput("Turiya: Tvak could not be made at step "); sput(tvak.step ? tvak.step : "?"); sput("\n");
	}

	colour = SAFFRON; out("TURIYA"); colour = DIM; out("  written from zero on seL4\n\n");
	colour = FG; out("Sakshi is awake. I hold the screen, the gate and my own breath; the heart and the keyboard are organs.\n\n");
	about();
	out("\n");
	colour = SAFFRON; out("Kartr gate, 15 test actions (rule code identical to the host tests)\n"); colour = FG;
	run_gate();
	colour = DIM; out("\nNothing here can pay, send or delete: the gate only decides. Type 'help'.\n\n"); colour = FG;
	status_bar();
	prompt();
	sput("Turiya: ready\n");

	if (!ep) {
		/* no endpoint at all: feel the keyboard by polling */
		for (;;) { reflex_poll(); seL4_Yield(); }
	}
	for (;;) {
		seL4_Word badge = 0;
		seL4_MessageInfo_t info = seL4_Recv(ep, &badge);
		if (badge & (ORG_FAULT | ORG_MSG)) {
			/* from an organ: the badge says which, and whether it spoke or fell */
			organ *x = organ_by_id(badge & 0xfff);
			if (x) { if (badge & ORG_FAULT) organ_fell(*x, info); else organ_spoke(*x, info); }
		} else {
			if (badge & N_BREATH) breath();
			if (badge & N_HEART) heart_tick();
		}
		drain_stash();
	}
}
