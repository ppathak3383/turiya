/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Nadi (channel): the organ that holds the USB host controller (xHCI), Turiya's channel to outside
 * devices.
 *
 * Svadharma, its one duty: find what is plugged in, name it, and listen to a USB keyboard (boot protocol),
 * passing each character to Sakshi exactly as Tvak does.
 *
 * Maryada, its limits: 1 fault channel, 2 send-only channel to Sakshi, 3 its notification. The controller's
 * registers (uncached), 48 DMA pages and one window shared with Sakshi. No interrupts: it polls on Sakshi's
 * pulse. It never sends data OUT to a device except the standard set-up requests (set configuration, set
 * protocol, set idle), which carry no data. It never touches a storage device's data.
 *
 * Cikitsa: if it faults, Sakshi re-makes it from its seed; it resets the controller and starts again.
 * Known gap: without an IOMMU a confused controller could DMA anywhere (no AMD IOMMU support in seL4 yet).
 */
#include <stdint.h>
#include <sel4/sel4.h>
#include "nadi_window.h"

enum : seL4_CPtr { C_FAULT = 1, C_OUT = 2, C_NTFN = 3 };
enum : seL4_Word { L_KEY = 1, L_READY = 2, L_PLUG = 5, L_UNPLUG = 6 };
enum : seL4_Word { N_PULSE = 2, N_FALL = 0x80 };

extern "C" { __attribute__((aligned(16), used)) unsigned char stack[16 * 1024]; }
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

extern "C" void *memset(void *d, int c, unsigned long n)
{ unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

extern "C" void nadi_main(seL4_Word gen, seL4_Word ipc);
extern "C" __attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile ("lea stack+16384(%rip), %rsp\n and $-16, %rsp\n call nadi_main\n 1: jmp 1b\n");
}

static void say(seL4_Word label, seL4_Word a, seL4_Word b)
{
	seL4_Word m0 = a, m1 = b, m2 = 0, m3 = 0;
	seL4_SendWithMRs(C_OUT, seL4_MessageInfo_new(label, 0, 0, 2), &m0, &m1, &m2, &m3);
}

static uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

static NadiWindow *const W = (NadiWindow *)NADI_WINDOW_VA;
static volatile uint8_t *R, *OP, *RT, *DB;

static uint32_t r32(volatile uint8_t *b, unsigned off) { return *(volatile uint32_t *)(b + off); }
static void w32(volatile uint8_t *b, unsigned off, uint32_t v) { __asm__ volatile ("sfence" ::: "memory"); *(volatile uint32_t *)(b + off) = v; }
static void w64(volatile uint8_t *b, unsigned off, uint64_t v) { w32(b, off, (uint32_t)v); w32(b, off + 4, (uint32_t)(v >> 32)); }

static uint64_t ms_ticks(uint64_t ms) { return (W->tsc_hz ? W->tsc_hz : 3000000000ULL) / 1000 * ms; }
static void pause_ms(uint64_t ms) { uint64_t t0 = rdtsc(), l = ms_ticks(ms); while (rdtsc() - t0 < l) __asm__ volatile ("pause"); }

/* wait until (reg & mask) == want, at most ms */
static bool wait32(volatile uint8_t *b, unsigned off, uint32_t mask, uint32_t want, uint64_t ms)
{
	uint64_t t0 = rdtsc(), l = ms_ticks(ms);
	while ((r32(b, off) & mask) != want) { if (rdtsc() - t0 > l) return false; __asm__ volatile ("pause"); }
	return true;
}

/* ---- DMA pages: a simple page allocator over what Sakshi gave ---- */
static unsigned next_page;
struct Page { volatile uint8_t *va; uint64_t pa; };
static Page page(void)
{
	if (next_page >= NADI_NDMA) return { 0, 0 };
	unsigned i = next_page++;
	memset((void *)W->va[i], 0, 4096);
	return { (volatile uint8_t *)W->va[i], W->pa[i] };
}

/* ---- rings ---- */
struct Trb { uint64_t param; uint32_t status, control; };
enum { RING_N = 256 };
struct Ring { volatile Trb *t; uint64_t pa; unsigned i, cycle; };

static void ring_init(Ring &r, Page p)
{
	r.t = (volatile Trb *)p.va; r.pa = p.pa; r.i = 0; r.cycle = 1;
	r.t[RING_N - 1].param = p.pa;                                  /* link back to the start, toggling the cycle */
	r.t[RING_N - 1].status = 0;
	r.t[RING_N - 1].control = 6u << 10 | 1u << 1;
}

static uint64_t ring_push(Ring &r, uint64_t param, uint32_t status, uint32_t control)
{
	volatile Trb *t = &r.t[r.i];
	uint64_t at = r.pa + r.i * 16UL;
	t->param = param; t->status = status;
	__asm__ volatile ("" ::: "memory");
	t->control = (control & ~1u) | r.cycle;
	if (++r.i == RING_N - 1) {                                     /* hand the link TRB over, then wrap */
		volatile Trb *l = &r.t[RING_N - 1];
		l->control = (l->control & ~1u) | r.cycle;
		r.i = 0; r.cycle ^= 1;
	}
	return at;
}

static Ring cmd, ep0[NADI_MAXDEV + 1], kin;
static volatile Trb *ev; static uint64_t ev_pa; static unsigned ev_i, ev_cycle;

/* the next event, if any (0 = none) */
static volatile Trb *event_peek(void)
{
	volatile Trb *e = &ev[ev_i];
	return (e->control & 1) == ev_cycle ? e : nullptr;
}
static void event_pop(void)
{
	if (++ev_i == RING_N) { ev_i = 0; ev_cycle ^= 1; }
	w64(RT, 0x38, (ev_pa + ev_i * 16UL) | 8);                       /* ERDP, clearing EHB */
}

static unsigned ctx;                                               /* context size: 32 or 64 bytes */

/* wait for an event of one type (and slot, if nonzero); other events are dropped. Returns the completion code. */
static int wait_event(unsigned type, unsigned slot, uint64_t ms, uint32_t *ctrl, uint32_t *status)
{
	uint64_t t0 = rdtsc(), l = ms_ticks(ms);
	for (;;) {
		volatile Trb *e = event_peek();
		if (e) {
			uint32_t c = e->control, s = e->status;
			event_pop();
			if (((c >> 10) & 0x3f) == type && (!slot || ((c >> 24) & 0xff) == slot)) {
				if (ctrl) *ctrl = c;
				if (status) *status = s;
				return (int)(s >> 24);
			}
			continue;
		}
		if (rdtsc() - t0 > l) return -1;
		__asm__ volatile ("pause");
	}
}

static int command(uint64_t param, uint32_t control, uint32_t *ctrl_out)
{
	ring_push(cmd, param, 0, control);
	w32(DB, 0, 0);
	return wait_event(33, 0, 1000, ctrl_out, nullptr);             /* command completion */
}

/* ---- per device ---- */
static Page dctx[NADI_MAXDEV + 1], ictx, buf;
static volatile uint64_t *dcbaa;

static volatile uint32_t *in_ctrl(void) { return (volatile uint32_t *)ictx.va; }
static volatile uint32_t *in_slot(void) { return (volatile uint32_t *)(ictx.va + ctx); }
static volatile uint32_t *in_ep(unsigned dci) { return (volatile uint32_t *)(ictx.va + (dci + 1) * ctx); }

/* one control transfer on endpoint 0; data IN of len bytes into buf (len 0: no data stage) */
static int control(unsigned slot, uint8_t rt, uint8_t req, uint16_t value, uint16_t index, uint16_t len)
{
	Ring &r = ep0[slot];
	uint64_t setup = (uint64_t)rt | (uint64_t)req << 8 | (uint64_t)value << 16 | (uint64_t)index << 32 | (uint64_t)len << 48;
	ring_push(r, setup, 8, 2u << 10 | 1u << 6 | (len ? 3u << 16 : 0));          /* setup, immediate data */
	if (len) ring_push(r, buf.pa, len, 3u << 10 | 1u << 16);                      /* data IN */
	ring_push(r, 0, 0, 4u << 10 | 1u << 5 | (len ? 0 : 1u << 16));                /* status, interrupt on completion */
	w32(DB, slot * 4, 1);
	uint32_t c = 0, s = 0;
	int cc = wait_event(32, slot, 1000, &c, &s);
	return cc == 1 || cc == 13 ? 0 : (cc < 0 ? 0x100 : cc);
}

static const char *class_word(uint8_t cls, uint8_t sub, uint8_t proto)
{
	if (cls == 3 && sub == 1 && proto == 1) return "keyboard";
	if (cls == 3 && sub == 1 && proto == 2) return "mouse";
	if (cls == 3) return "input device (HID)";
	if (cls == 8) return "storage";
	if (cls == 9) return "hub";
	if (cls == 0xe0) return "wireless (Bluetooth?)";
	if (cls == 2 || cls == 0x0a) return "network or modem";
	if (cls == 0x0e) return "camera";
	if (cls == 1) return "audio";
	if (cls == 0xef || cls == 0xff) return "vendor-specific";
	return "other";
}

static uint8_t kbd_dci;

static uint32_t address_and_name(unsigned port, unsigned speed)
{
	uint32_t c = 0;
	if (command(0, 9u << 10, &c) != 1) return 0x100;              /* enable slot */
	unsigned slot = (c >> 24) & 0xff;
	if (!slot || slot > NADI_MAXDEV) return 0x101;
	dctx[slot] = page(); Page r0 = page();
	if (!dctx[slot].va || !r0.va) return 0x102;
	dcbaa[slot] = dctx[slot].pa;
	ring_init(ep0[slot], r0);
	unsigned mps = speed == 4 ? 512 : speed == 3 ? 64 : 8;
	memset((void *)ictx.va, 0, 4096);
	in_ctrl()[1] = 3;                                              /* add slot and EP0 */
	in_slot()[0] = 1u << 27 | (uint32_t)speed << 20;              /* one context entry, speed, route 0 */
	in_slot()[1] = (uint32_t)port << 16;
	in_ep(1)[1] = (uint32_t)mps << 16 | 4u << 3 | 3u << 1;         /* control, CErr 3 */
	*(volatile uint64_t *)&in_ep(1)[2] = r0.pa | 1;
	in_ep(1)[4] = 8;
	if (command(ictx.pa, 11u << 10 | slot << 24, nullptr) != 1) return 0x103;     /* address device */

	NadiDevice &d = W->dev[W->ndev];
	memset(&d, 0, sizeof d);
	d.port = (uint8_t)port; d.speed = (uint8_t)speed; d.slot = (uint8_t)slot;
	if (control(slot, 0x80, 6, 0x0100, 0, 8)) { memcpy(d.what, "no answer", 10); W->ndev++; return 0; }
	unsigned real_mps = buf.va[7];
	if (speed <= 2 && real_mps && real_mps != mps) {               /* full/low speed: learn EP0's real size */
		memset((void *)ictx.va, 0, 4096);
		in_ctrl()[1] = 2;
		in_ep(1)[1] = (uint32_t)real_mps << 16 | 4u << 3 | 3u << 1;
		*(volatile uint64_t *)&in_ep(1)[2] = (ep0[slot].pa + ep0[slot].i * 16UL) | ep0[slot].cycle;
		in_ep(1)[4] = 8;
		command(ictx.pa, 13u << 10 | slot << 24, nullptr);        /* evaluate context */
	}
	if (control(slot, 0x80, 6, 0x0100, 0, 18)) { memcpy(d.what, "no descriptor", 14); W->ndev++; return 0; }
	d.vendor = (uint16_t)(buf.va[8] | buf.va[9] << 8); d.product = (uint16_t)(buf.va[10] | buf.va[11] << 8);
	uint8_t dcls = buf.va[4];
	if (control(slot, 0x80, 6, 0x0200, 0, 9)) { memcpy(d.what, "no configuration", 17); W->ndev++; return 0; }
	unsigned total = buf.va[2] | buf.va[3] << 8;
	if (total > 1024) total = 1024;
	if (control(slot, 0x80, 6, 0x0200, 0, (uint16_t)total)) { memcpy(d.what, "no configuration", 17); W->ndev++; return 0; }
	uint8_t cfg = buf.va[5];

	/* walk interfaces and endpoints; the first interface names the device; a boot keyboard is listened to */
	int ifnum = -1, kbd_if = -1; uint8_t kbd_ep = 0, kbd_int = 10; uint16_t kbd_mps = 8;
	bool named = false, in_kbd = false;
	for (unsigned o = 0; o + 2 <= total; ) {
		unsigned len = buf.va[o], type = buf.va[o + 1];
		if (len < 2) break;
		if (type == 4 && o + 9 <= total) {
			ifnum = buf.va[o + 2];
			uint8_t c3 = buf.va[o + 5], s3 = buf.va[o + 6], p3 = buf.va[o + 7];
			in_kbd = c3 == 3 && s3 == 1 && p3 == 1;
			if (!named || in_kbd) { d.cls = c3; d.sub = s3; d.proto = p3; named = true; }
			if (in_kbd && kbd_if < 0) kbd_if = ifnum;
		}
		if (type == 5 && o + 7 <= total && in_kbd && !kbd_ep && (buf.va[o + 2] & 0x80) && (buf.va[o + 3] & 3) == 3) {
			kbd_ep = buf.va[o + 2] & 0x0f; kbd_mps = (uint16_t)(buf.va[o + 4] | buf.va[o + 5] << 8); kbd_int = buf.va[o + 6];
		}
		o += len;
	}
	if (!named) d.cls = dcls;
	const char *w = class_word(d.cls, d.sub, d.proto);
	unsigned k = 0; while (w[k] && k < sizeof d.what - 1) { d.what[k] = w[k]; k++; } d.what[k] = 0;
	W->ndev++;

	if (kbd_if >= 0 && kbd_ep && !W->keyboard_slot) {
		if (control(slot, 0x00, 9, cfg, 0, 0)) return 0;          /* set configuration */
		control(slot, 0x21, 0x0b, 0, (uint16_t)kbd_if, 0);       /* set protocol: boot */
		control(slot, 0x21, 0x0a, 0, (uint16_t)kbd_if, 0);       /* set idle: report on change */
		Page kr = page();
		if (!kr.va) return 0x104;
		ring_init(kin, kr);
		kbd_dci = (uint8_t)(kbd_ep * 2 + 1);
		unsigned interval = 3;
		if (speed <= 2) { unsigned f = kbd_int ? kbd_int * 8u : 8u; interval = 0; while ((2u << interval) <= f && interval < 10) interval++; }
		else if (kbd_int) interval = kbd_int - 1u;
		memset((void *)ictx.va, 0, 4096);
		in_ctrl()[1] = 1u | 1u << kbd_dci;
		in_slot()[0] = (uint32_t)kbd_dci << 27 | (uint32_t)speed << 20;
		in_slot()[1] = (uint32_t)port << 16;
		in_ep(kbd_dci)[0] = interval << 16;
		in_ep(kbd_dci)[1] = (uint32_t)kbd_mps << 16 | 7u << 3 | 3u << 1;     /* interrupt IN, CErr 3 */
		*(volatile uint64_t *)&in_ep(kbd_dci)[2] = kr.pa | 1;
		in_ep(kbd_dci)[4] = (uint32_t)kbd_mps << 16 | 8;
		if (command(ictx.pa, 12u << 10 | slot << 24, nullptr) != 1) return 0x105;   /* configure endpoint */
		W->keyboard_slot = slot;
		ring_push(kin, buf.pa + 2048, 8, 1u << 10 | 1u << 5);    /* one report in flight: normal, IOC */
		w32(DB, slot * 4, kbd_dci);
	}
	return 0;
}

static bool known[33];                                             /* ports with a device we addressed */
static uint8_t kbd_port;

/* look at the ports; with only_changed, just those whose connection changed (hot-plug) */
static void scan_ports(bool only_changed)
{
	for (unsigned p = 1; p <= W->max_ports && p <= 32; p++) {
		unsigned pr = 0x400 + 0x10 * (p - 1);
		uint32_t sc = r32(OP, pr);
		if (only_changed && !(sc & 1u << 17)) continue;
		if (sc & 1u << 17) w32(OP, pr, (sc & ~0x00fe0002u) | 1u << 17);      /* clear the connect change */
		if (!(sc & 1)) {                                                    /* gone */
			if (known[p]) {
				known[p] = false;
				if (W->ports_connected) W->ports_connected--;
				if (kbd_port == p) { W->keyboard_slot = 0; kbd_port = 0; }
				if (only_changed) say(L_UNPLUG, p, 0);
			}
			continue;
		}
		if (known[p]) continue;
		W->ports_connected++;
		if (!(sc & 2)) {                                            /* not enabled: reset (USB 2) */
			w32(OP, pr, (sc & ~0x00fe0002u) | 1u << 4);
			wait32(OP, pr, 1u << 21, 1u << 21, 500);
			sc = r32(OP, pr);
			w32(OP, pr, (sc & ~0x00fe0002u) | 1u << 21);             /* clear the reset change */
			pause_ms(20);
			sc = r32(OP, pr);
		}
		if (!(sc & 2) || W->ndev >= NADI_MAXDEV) continue;
		known[p] = true;
		unsigned had_kbd = W->keyboard_slot;
		address_and_name(p, (sc >> 10) & 0xf);
		if (!had_kbd && W->keyboard_slot) kbd_port = (uint8_t)p;
		if (only_changed) say(L_PLUG, p, W->ndev - 1);
	}
}

static uint32_t init(void)
{
	/* a fresh start (also after a heal): forget what the window says about devices */
	W->status = 0; W->ports_connected = 0; W->ndev = 0; W->keyboard_slot = 0; W->step = 0;
	memset((void *)W->dev, 0, sizeof W->dev);
	R = (volatile uint8_t *)W->bar_va;
	unsigned caplen = *(volatile uint8_t *)R;
	W->version = r32(R, 0) >> 16;                                  /* HCIVERSION (read as one dword) */
	uint32_t hcs1 = r32(R, 4), hcs2 = r32(R, 8), hcc1 = r32(R, 0x10);
	W->max_slots = hcs1 & 0xff; W->max_ports = hcs1 >> 24;
	W->scratchpads = ((hcs2 >> 21) & 0x1f) << 5 | ((hcs2 >> 27) & 0x1f);
	ctx = (hcc1 & 4) ? 64 : 32; W->ctx_size = ctx;
	OP = R + caplen; RT = R + (r32(R, 0x18) & ~0x1fu); DB = R + (r32(R, 0x14) & ~0x3u);
	if ((unsigned)(RT - R) + 0x40 > NADI_REG_PAGES * 4096u || (unsigned)(DB - R) + 4 * (NADI_MAXDEV + 1) > NADI_REG_PAGES * 4096u) return 1;

	/* the firmware lets go: USB legacy support hand-off (extended capability 1) */
	W->step = 1;
	for (unsigned off = (hcc1 >> 16) << 2, n = 0; off && off < NADI_REG_PAGES * 4096u && n < 32; n++) {
		uint32_t v = r32(R, off);
		if ((v & 0xff) == 1) {
			w32(R, off, v | 1u << 24);                              /* OS owned */
			wait32(R, off, 1u << 16, 0, 1000);                     /* BIOS lets go */
			uint32_t ctl = r32(R, off + 4);
			w32(R, off + 4, (ctl & ~0xe01fu) | 0xe0000000u);        /* no SMIs; clear their status */
		}
		unsigned next = (v >> 8) & 0xff;
		if (!next) break;
		off += next << 2;
	}

	/* stop, reset */
	W->step = 2;
	w32(OP, 0, r32(OP, 0) & ~1u);
	if (!wait32(OP, 4, 1, 1, 100)) return 2;
	w32(OP, 0, 2);
	if (!wait32(OP, 0, 2, 0, 1000) || !wait32(OP, 4, 1u << 11, 0, 1000)) return 3;

	/* device contexts, scratchpads, rings */
	W->step = 3;
	unsigned slots = W->max_slots < (unsigned)NADI_MAXDEV ? W->max_slots : (unsigned)NADI_MAXDEV;
	w32(OP, 0x38, slots);
	Page dc = page(); dcbaa = (volatile uint64_t *)dc.va;
	if (W->scratchpads) {
		if (W->scratchpads > 24) return 4;
		Page sa = page();
		for (unsigned i = 0; i < W->scratchpads; i++) { Page sp = page(); if (!sp.va) return 5; ((volatile uint64_t *)sa.va)[i] = sp.pa; }
		dcbaa[0] = sa.pa;
	}
	w64(OP, 0x30, dc.pa);
	ring_init(cmd, page());
	w64(OP, 0x18, cmd.pa | 1);
	Page er = page(), erst = page();
	ev = (volatile Trb *)er.va; ev_pa = er.pa; ev_i = 0; ev_cycle = 1;
	((volatile uint64_t *)erst.va)[0] = er.pa;
	((volatile uint32_t *)erst.va)[2] = RING_N;
	w32(RT, 0x28, 1);
	w64(RT, 0x38, er.pa);
	w64(RT, 0x30, erst.pa);
	ictx = page(); buf = page();
	if (!buf.va) return 6;

	/* run */
	W->step = 4;
	w32(OP, 0, 1);
	if (!wait32(OP, 4, 1, 0, 1000)) return 7;
	pause_ms(100);                                                  /* let the ports settle */

	/* ports: reset the connected USB 2 ports, then address what answers */
	W->step = 5;
	scan_ports(false);
	W->step = 6;
	return 0;
}

/* HID boot keyboard usages to characters (US layout) */
static char usage_char(uint8_t u, bool shift)
{
	static const char lo[] = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\x08\t -=[]\\#;'`,./";
	static const char hi[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\x08\t _+{}|~:\"~<>?";
	if (u >= 4 && u <= 0x38) return (shift ? hi : lo)[u - 4];
	if (u == 0x58) return '\n';                                     /* keypad enter */
	return 0;
}

static uint8_t last[6];

static void keyboard_poll(void)
{
	for (;;) {
		volatile Trb *e = event_peek();
		if (!e) return;
		uint32_t c = e->control, s = e->status; uint64_t at = e->param;
		event_pop();
		if (((c >> 10) & 0x3f) != 32 || ((c >> 24) & 0xff) != W->keyboard_slot || ((c >> 16) & 0x1f) != kbd_dci) continue;
		unsigned cc = s >> 24;
		(void)at;
		if (cc == 1 || cc == 13) {
			volatile uint8_t *rep = buf.va + 2048;
			bool shift = rep[0] & 0x22;
			for (int i = 2; i < 8; i++) {
				uint8_t u = rep[i];
				if (u < 4) continue;
				bool was = false;
				for (int j = 0; j < 6; j++) was |= last[j] == u;
				if (!was) { char ch = usage_char(u, shift); if (ch) say(L_KEY, (unsigned char)ch, 0); }
			}
			for (int i = 0; i < 6; i++) last[i] = rep[2 + i];
		}
		ring_push(kin, buf.pa + 2048, 8, 1u << 10 | 1u << 5);
		w32(DB, W->keyboard_slot * 4, kbd_dci);
	}
}

extern "C" void nadi_main(seL4_Word gen, seL4_Word ipc)
{
	seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc);
	uint32_t st = init();
	W->status = st;
	say(L_READY, gen, st);
	for (;;) {
		seL4_Word bits = 0;
		seL4_Wait(C_NTFN, &bits);
		if (bits & N_FALL) *(volatile uint32_t *)0x8000000000UL = 0;      /* the crash test */
		if ((bits & N_PULSE) && !st) {
			scan_ports(true);
			if (W->keyboard_slot) keyboard_poll();
			else while (event_peek()) event_pop();             /* nothing to listen to: keep the event ring empty */
		}
	}
}
