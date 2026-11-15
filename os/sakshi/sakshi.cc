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
 *   6. listens to the keyboard (i8042, IRQ 1, also felt on every heartbeat),
 *   7. offers a first command line: every typed action goes through the gate; an irreversible one
 *      can be approved only from this keyboard, which only Sakshi holds.
 * Everything it prints also goes to the serial line (kernel debug output) for the emulator.
 */
#include <stdint.h>
#include <sel4/sel4.h>
#include <sel4/arch/bootinfo_types.h>
#include <sel4/sel4_arch/mapping.h>
#include "font8x16.h"
#include "kartr_rule.h"

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

/* status bar below the grid: the heart and the keyboard */
static unsigned long beats, kbd_irqs, keys;
static void status_bar(void)
{
	if (!fb) return;
	u32b y = MARGIN + rows * CH + CH / 2;
	if (y + CH > fb_h) return;
	fill(0, y - CH / 4, fb_w, CH + CH / 2, BAR);
	char line[160]; unsigned k = 0;
	auto add = [&](const char *s) { while (*s && k < sizeof line - 1) line[k++] = *s++; };
	auto addn = [&](unsigned long v) { char b[24]; int i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); add(b + i); };
	add("heart "); addn(beats); add(" beats   keyboard ");
	add(kbd_irqs ? "by interrupt" : (keys ? "felt on each heartbeat" : "waiting"));
	add("   keys "); addn(keys); add("   type 'help'");
	line[k] = 0;
	for (unsigned i = 0; i < k; i++) glyph_at(MARGIN + i * CW, y, line[i], i < 5 ? RED : DIM, BAR);
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

/* turn the device untyped(s) covering [paddr, paddr+len) into frames and map them at vbase */
static int map_device(uptr paddr, uptr len, uptr vbase)
{
	static uptr carved[CONFIG_MAX_NUM_BOOTINFO_UNTYPED_CAPS];
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
		uptr at = ub + carved[i];
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
		carved[i] = at - ub;
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
		Turiya::Verdict v = Turiya::decide(key, klen, a->type, a->target, a->amount, ap, trunc,
		                                   Turiya::DEFAULT_MANDATE, OUTSIDE);
		Turiya::is_held(v) ? held++ : allowed++;
		record(a->type, a->target, a->amount, v);
		show_verdict(a->id, a->type, a->target, a->amount, v);
	}
	colour = SAFFRON; out("  kartr_gate: "); out_dec(allowed); out(" allowed, "); out_dec(held); out(" held\n");
	colour = FG;
}

/* ---- I/O ports, interrupts, heart, keyboard ---- */
static seL4_CPtr kbd_port, pit_port, cf9_port, ntfn, irq_kbd, irq_pit;

static uint8_t inb(seL4_CPtr cap, uint16_t port)
{
	if (!cap) return 0xff;
	seL4_X86_IOPort_In8_t r = seL4_X86_IOPort_In8(cap, port);
	return r.error ? 0xff : r.result;
}
static void outb(seL4_CPtr cap, uint16_t port, uint8_t v) { if (cap) seL4_X86_IOPort_Out8(cap, port, v); }

static bool kbd_wait_write(void) { for (int i = 0; i < 100000; i++) if (!(inb(kbd_port, 0x64) & 2)) return true; return false; }
static bool kbd_wait_read(void)  { for (int i = 0; i < 100000; i++) if (inb(kbd_port, 0x64) & 1) return true; return false; }

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
static bool kbd_present;

static void setup_io(void)
{
	kbd_port = issue_ports(0x60, 0x64);
	pit_port = issue_ports(0x40, 0x43);
	cf9_port = issue_ports(0xcf9, 0xcf9);
	ntfn = alloc_slot();
	if (!ntfn || retype(ram_ut, seL4_NotificationObject, 0, ntfn, 1)) { ntfn = 0; io_report = "no notification"; return; }

	/* keyboard controller: flush, enable its interrupt and scan-code translation, enable the keyboard */
	if (kbd_port && inb(kbd_port, 0x64) != 0xff) {
		kbd_present = true;
		for (int i = 0; i < 64 && (inb(kbd_port, 0x64) & 1); i++) inb(kbd_port, 0x60);
		if (kbd_wait_write()) {
			outb(kbd_port, 0x64, 0x20);
			if (kbd_wait_read()) {
				uint8_t cfg = inb(kbd_port, 0x60);
				cfg = (uint8_t)((cfg | 0x01 | 0x40) & ~0x10);
				if (kbd_wait_write()) { outb(kbd_port, 0x64, 0x60); if (kbd_wait_write()) outb(kbd_port, 0x60, cfg); }
			}
			if (kbd_wait_write()) outb(kbd_port, 0x64, 0xae);
		}
		for (int i = 0; i < 64 && (inb(kbd_port, 0x64) & 1); i++) inb(kbd_port, 0x60);
	}

	/* the heart: PIT channel 0, rate generator, 50 Hz */
	if (pit_port) {
		unsigned div = 1193182 / 50;
		outb(pit_port, 0x43, 0x34);
		outb(pit_port, 0x40, (uint8_t)(div & 0xff));
		outb(pit_port, 0x40, (uint8_t)(div >> 8));
	}
	irq_kbd = irq_on_pin(1, 1, 1);
	irq_pit = irq_on_pin(2, 2, 2);
	io_report = irq_pit ? (irq_kbd ? "heart and keyboard interrupts set" : "heart set; keyboard interrupt refused")
	                    : (irq_kbd ? "keyboard interrupt set; no heart" : "no interrupts: polling");
}

/* scan code set 1, US layout */
static const char KEYMAP[2][0x3a] = {
	{ 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, '\t',
	  'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
	  'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
	  'b','n','m',',','.','/', 0, '*', 0, ' ' },
	{ 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, '\t',
	  'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
	  'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
	  'B','N','M','<','>','?', 0, '*', 0, ' ' },
};
static bool shift, caps, ext;

static char line[160];
static unsigned line_len;
static char last_type[24], last_target[64], last_amount[16];
static bool have_last;

static void prompt(void) { colour = SAFFRON; out("turiya> "); colour = FG; cursor(true); }

static void help(void)
{
	colour = FG;
	out("  help                     this list\n");
	out("  about                    what is running, on what\n");
	out("  gate                     run the 15 test actions again\n");
	out("  mail                     the sample email the gate guards against\n");
	out("  <action> <target> [amt]  ask the gate, e.g.  pay ramesh.kumar@ybl 15000\n");
	out("                           summarise inbox    draft_reply papa    delete photos\n");
	out("  approve                  approve the last held action (only from this keyboard)\n");
	out("  log                      the witness log of this session\n");
	out("  clear                    clear the screen\n");
	out("  reboot                   restart the machine (pull the stick for Windows)\n");
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
	colour = DIM; out("  devices     "); colour = FG; out(kbd_present ? "keyboard controller found; " : "no keyboard controller; "); out(io_report); out("\n");
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

static void execute(char *l)
{
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
	if (streq(w[0], "mail")) {
		colour = DIM; out("  outside content the gate has seen (a phishing email):\n"); colour = FG;
		out("  "); for (const char *p = OUTSIDE; *p; p++) { if (*p == '\n') out("\n  "); else { char c[2] = { *p, 0 }; out(c); } }
		out("\n"); return;
	}
	if (streq(w[0], "log")) {
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
		Turiya::Verdict v = Turiya::decide(key, klen, last_type, last_target, last_amount, hex, false,
		                                   Turiya::DEFAULT_MANDATE, nullptr);
		record(last_type, last_target, last_amount, v);
		show_verdict(0, last_type, last_target, last_amount, v); explain(v, last_type);
		colour = DIM; out("  approval: HMAC-SHA256 of type|target|amount with the test key\n  "); out(hex); out("\n"); colour = FG;
		have_last = false;
		return;
	}
	/* anything else is an action for the gate: <type> <target> [amount] */
	const char *type = w[0], *target = w[1] ? w[1] : "", *amount = w[2] ? w[2] : "0";
	bool trunc = slen(target) > Turiya::FIELD_MAX;
	Turiya::Verdict v = Turiya::decide(key, klen, type, target, amount, "", trunc, Turiya::DEFAULT_MANDATE, nullptr);
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
		out("\n"); line[line_len] = 0;
		execute(line); line_len = 0;
		prompt(); return;
	}
	if (c == 8) {
		if (line_len && cur_x) { line_len--; cur_x--; cell_ch[cur_y][cur_x] = ' '; draw_cell(cur_x, cur_y); sput("\b \b"); }
		cursor(true); return;
	}
	if (c >= 32 && c < 127 && line_len < sizeof line - 1) { line[line_len++] = c; char s[2] = { c, 0 }; out(s); }
	cursor(true);
}

static void scancode(uint8_t sc)
{
	if (sc == 0xe0) { ext = true; return; }
	bool release = sc & 0x80; uint8_t code = sc & 0x7f;
	if (ext) { ext = false; if (code == 0x1c && !release) { keys++; key_char('\n'); } return; }
	if (code == 0x2a || code == 0x36) { shift = !release; return; }
	if (release) return;
	if (code == 0x3a) { caps = !caps; return; }
	if (code >= 0x3a) return;
	char c = KEYMAP[shift ? 1 : 0][code];
	if (caps && c >= 'a' && c <= 'z') c = (char)(c - 32);
	else if (caps && c >= 'A' && c <= 'Z') c = (char)(c + 32);
	if (c) { keys++; key_char(c); }
}

static void poll_keyboard(void)
{
	if (!kbd_present) return;
	for (int i = 0; i < 32; i++) {
		uint8_t st = inb(kbd_port, 0x64);
		if (!(st & 1)) break;
		uint8_t d = inb(kbd_port, 0x60);
		if (st & 0x20) continue;            /* touchpad / mouse byte: not ours yet */
		scancode(d);
	}
}

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

	colour = SAFFRON; out("TURIYA"); colour = DIM; out("  written from zero on seL4\n\n");
	colour = FG; out("Sakshi is awake. I hold the screen, the keyboard and a heartbeat, nothing else.\n\n");
	about();
	out("\n");
	colour = SAFFRON; out("Kartr gate, 15 test actions (rule code identical to the host tests)\n"); colour = FG;
	run_gate();
	colour = DIM; out("\nNothing here can pay, send or delete: the gate only decides. Type 'help'.\n\n"); colour = FG;
	status_bar();
	prompt();
	sput("Turiya: ready\n");

	if (!ntfn || (!irq_kbd && !irq_pit)) {
		/* no interrupts at all: feel the keyboard by polling */
		for (;;) { poll_keyboard(); seL4_Yield(); }
	}
	unsigned long ticks = 0;
	for (;;) {
		seL4_Word badge = 0;
		seL4_Wait(ntfn, &badge);
		if (badge & 2) {
			ticks++;
			if (ticks % 50 == 0) { beats++; status_bar(); }
			seL4_IRQHandler_Ack(irq_pit);
		}
		poll_keyboard();
		if (badge & 1) { kbd_irqs++; seL4_IRQHandler_Ack(irq_kbd); if (kbd_irqs == 1) status_bar(); }
	}
}
