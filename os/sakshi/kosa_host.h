/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sakshi's side of Kosa (the NVMe organ), the heart's tick, and waiting for an organ.
 * Included into sakshi.cc after organ_fell/organ_spoke. Kosa is made only when 'disk' is typed.
 */

/* ---- the heart's work on every tick, shared by the main loop and by waits ---- */
static unsigned long ticks;
static uint64_t last_beat_tsc;

static void heart_tick(void)
{
	ticks++;
	if (ticks % 50 == 0) {
		uint64_t now = rdtsc();
		if (last_beat_tsc && !tsc_known) tsc_hz = now - last_beat_tsc;
		last_beat_tsc = now;
		beats++;
		if (avastha == Turiya::Avastha::JAGRAT && beats - last_key_beat >= IDLE_BEATS) {
			avastha = Turiya::Avastha::SVAPNA; dreaming_idle = true;
			interject_begin(); colour = SAFFRON; out("  no key for 10 minutes: Turiya dreams (svapna).");
			colour = DIM; out(" It may think, but nothing leaves a mark until you return.\n"); colour = FG;
			interject_end();
		}
		if (hold_prompt && beats >= hold_since + 3) {
			interject_begin(); colour = DIM; out("  no word from the organ yet.\n"); colour = FG;
			hold_prompt = false; interject_end();
		}
		if (beats >= dina.last_beat + 90) dina.due = true;     /* ...and by the heart, if the breath ever fails */
		clock_tick();                                           /* Kala: the date and time for the status bar */
		status_bar();
	}
	if (irq_pit) seL4_IRQHandler_Ack(irq_pit);               /* only when Sakshi keeps the timer itself */
	if (tvak.made && tvak.awake) seL4_Signal(tvak.pulse);   /* Tvak feels the keys even without IRQ 1 */
	for (organ *n : NADIS) if (n->made && n->awake) seL4_Signal(n->pulse);   /* each Nadi looks at its ports */
	reflex_poll();
}

/* the breath: Sakshi's own slow clock (RTC, 2 Hz). It watches the heart: no tick for 4 breaths, re-make it. */
static unsigned long ticks_seen, still_breaths, hold_breaths;
static void organ_revive(organ &o, const char *why);

/* an organ re-made but silent for two seconds: say what happened anyway */
static void tell_late(void)
{
	organ *const all[] = { &tvak, &kosa, &hrdaya, &nadi, &nadi2, &smrti };
	for (organ *o : all)
		if (o->untold && o->t_fall && tsc_hz && rdtsc() - o->t_fall > 2 * tsc_hz) { tell_fall(*o, true); interject_end(); }
}

static void breath(void)
{
	breaths++;
	outb(rtc_port, 0x70, 0x0c); inb(rtc_port, 0x71);           /* tell the RTC it was heard */
	seL4_IRQHandler_Ack(irq_rtc);
	if (hrdaya.made && hrdaya.awake && !hrdaya.resting) {
		if (ticks == ticks_seen) {
			if (++still_breaths >= 4) { still_breaths = 0; organ_revive(hrdaya, "no beat for 2 seconds, seen by the breath"); }
		} else still_breaths = 0;
	}
	ticks_seen = ticks;
	tell_late();
	if (breaths % 120 == 0) dina.due = true;                 /* the daily round: every minute, by the breath */
	if (!hold_prompt) hold_breaths = 0;
	else if (++hold_breaths > 8) {
		interject_begin(); colour = DIM; out("  no word from the organ yet.\n"); colour = FG;
		hold_prompt = false; interject_end();
	}
}

/* keys that arrive while Sakshi waits for an organ are kept, and typed afterwards */
static char stash[64];
static unsigned stash_n;

static void drain_stash(void)
{
	while (stash_n && !hold_prompt) {
		char c = stash[0];
		for (unsigned i = 1; i < stash_n; i++) stash[i - 1] = stash[i];
		stash_n--;
		keys++; key_char(c);
	}
}

/* wait for one of some messages (a bit mask of labels) from one organ, at most max_beats seconds; everything
 * else is still served. Returns the label, or 0 if the organ fell or the time ran out. */
static seL4_Word wait_any(organ &o, seL4_Word labels, unsigned long max_beats, seL4_Word *a, seL4_Word *b)
{
	unsigned long until = beats + max_beats;
	for (;;) {
		seL4_Word badge = 0;
		seL4_MessageInfo_t info = seL4_Recv(ep, &badge);
		if (badge & (ORG_FAULT | ORG_MSG)) {
			organ *x = organ_by_id(badge & 0xfff);
			if (!x) continue;
			if (badge & ORG_FAULT) { organ_fell(*x, info); if (x == &o) return 0; continue; }
			seL4_Word l = seL4_MessageInfo_get_label(info);
			if (x == &o && l < 64 && (labels & (1UL << l))) { if (a) *a = seL4_GetMR(0); if (b) *b = seL4_GetMR(1); return l; }
			if (l == L_KEY) { if (stash_n < sizeof stash) stash[stash_n++] = (char)seL4_GetMR(0); continue; }
			organ_spoke(*x, info);
			continue;
		}
		if (badge & N_BREATH) breath();
		if (badge & N_HEART) { heart_tick(); if (beats >= until) return 0; }
	}
}

/* wait for one message from one organ, at most max_beats seconds; everything else is still served */
static bool wait_for(organ &o, seL4_Word label, unsigned long max_beats, seL4_Word *a, seL4_Word *b)
{
	unsigned long until = beats + max_beats;
	for (;;) {
		seL4_Word badge = 0;
		seL4_MessageInfo_t info = seL4_Recv(ep, &badge);
		if (badge & (ORG_FAULT | ORG_MSG)) {
			organ *x = organ_by_id(badge & 0xfff);
			if (!x) continue;
			if (badge & ORG_FAULT) { organ_fell(*x, info); if (x == &o) return false; continue; }
			seL4_Word l = seL4_MessageInfo_get_label(info);
			if (x == &o && l == label) { if (a) *a = seL4_GetMR(0); if (b) *b = seL4_GetMR(1); return true; }
			if (l == L_KEY) { if (stash_n < sizeof stash) stash[stash_n++] = (char)seL4_GetMR(0); continue; }
			organ_spoke(*x, info);
			continue;
		}
		if (badge & N_BREATH) breath();
		if (badge & N_HEART) { heart_tick(); if (beats >= until) return false; }
	}
}

/* ---- Kosa ---- */
static KosaWindow *kwin;               /* Sakshi's view of the shared window */
static const uint8_t *kdata;           /* Sakshi's read-only view of the data page */
static const char *kosa_why;
static pci_fn nvme_fn;
static bool have_nvme;
static uint32_t disk_seq;

static void pci_write16(unsigned bus, unsigned dev, unsigned fn, unsigned off, uint16_t v)
{
	seL4_X86_IOPort_Out32(cf9_port, 0xcf8, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | (off & 0xfc));
	seL4_X86_IOPort_Out16(cf9_port, (uint16_t)(0xcfc + (off & 2)), v);
}

/* give an organ a page: a copy of the frame capability, mapped in its VSpace only */
static long organ_map(organ &o, seL4_CPtr frame, uptr va, seL4_CapRights_t rights, seL4_X86_VMAttributes attr)
{
	seL4_CPtr c = alloc_slot();
	if (!c) return -1;
	if (long r = seL4_CNode_Copy(seL4_CapInitThreadCNode, c, seL4_WordBits, seL4_CapInitThreadCNode, frame, seL4_WordBits, seL4_AllRights))
		return r;
	return map_in(o.pml4, c, va, rights, attr);
}

static bool kosa_fail(const char *why) { kosa_why = why; return false; }

static bool make_kosa(void)
{
	pci_scan();
	for (unsigned i = 0; i < npci && !have_nvme; i++)
		if (pcis[i].cls == 1 && pcis[i].sub == 8 && pcis[i].pif == 2) { nvme_fn = pcis[i]; have_nvme = true; }
	if (!have_nvme) return kosa_fail("there is no NVMe controller on this machine");
	pci_fn &p = nvme_fn;
	uint32_t b0 = pci_read(p.bus, p.dev, p.fn, 0x10);
	if (b0 & 1) return kosa_fail("its registers are an I/O port range");
	uint64_t bar = b0 & ~0xfULL;
	if (((b0 >> 1) & 3) == 2) bar |= (uint64_t)pci_read(p.bus, p.dev, p.fn, 0x14) << 32;
	if (!bar) return kosa_fail("its registers have no address");
	uint16_t cmd = (uint16_t)pci_read(p.bus, p.dev, p.fn, 0x04);
	pci_write16(p.bus, p.dev, p.fn, 0x04, (uint16_t)(cmd | 0x6));      /* memory space and bus mastering on */

	if (!nduts) duts_init();
	if (!make_organ(kosa, 0, NO_IRQ, 0, 150)) return kosa_fail(kosa.step);
	for (unsigned i = 0; i < 4; i++) {                                   /* registers: 16 KiB, uncached */
		seL4_CPtr f = dev_page(bar + i * 4096UL);
		if (!f || organ_map(kosa, f, KOSA_REGS_VA + i * 4096UL, seL4_ReadWrite, seL4_X86_CacheDisabled))
			return kosa_fail("its registers could not be mapped");
	}
	uptr view = kosa.scratch + 0x800000;                                 /* Sakshi's views of the window and data */
	seL4_CPtr wf = alloc_slot();
	if (!wf || retype(ram_ut, seL4_X86_4K, 0, wf, 1)) return kosa_fail("no window page");
	if (map_in(seL4_CapInitThreadVSpace, wf, view, seL4_ReadWrite, seL4_X86_Default_VMAttributes)
	    || organ_map(kosa, wf, KOSA_WINDOW_VA, seL4_ReadWrite, seL4_X86_Default_VMAttributes))
		return kosa_fail("the window could not be mapped");
	kwin = (KosaWindow *)view;
	memset(kwin, 0, 4096);
	for (unsigned i = 0; i < KOSA_NDMA; i++) {
		seL4_CPtr f = alloc_slot();
		if (!f || retype(ram_ut, seL4_X86_4K, 0, f, 1)) return kosa_fail("no DMA page");
		seL4_X86_Page_GetAddress_t a = seL4_X86_Page_GetAddress(f);
		if (a.error) return kosa_fail("no DMA address");
		kwin->pa[i] = a.paddr; kwin->va[i] = KOSA_DMA_VA + i * 4096UL;
		if (organ_map(kosa, f, kwin->va[i], seL4_ReadWrite, seL4_X86_Default_VMAttributes)) return kosa_fail("DMA map");
		if (i == KOSA_DATA) {
			if (map_in(seL4_CapInitThreadVSpace, f, view + 4096, seL4_CanRead, seL4_X86_Default_VMAttributes))
				return kosa_fail("data view");
			kdata = (const uint8_t *)(view + 4096);
		}
	}
	kwin->bar_va = KOSA_REGS_VA; kwin->tsc_hz = tsc_hz;
	if (!remake(kosa) || start(kosa)) return kosa_fail("its first start");
	kosa.made = true;
	return true;
}

static bool disk_read(uint64_t lba, uint32_t count)
{
	kwin->req_lba = lba; kwin->req_count = count; kwin->req_seq = ++disk_seq;
	seL4_Signal(kosa.req);
	seL4_Word seq = 0, st = 0;
	if (!wait_for(kosa, L_DONE, 5, &seq, &st)) return false;
	return seq == disk_seq && st == 0;
}

static void out_size(uint64_t bytes)
{
	if (bytes >= 1000000000ULL) { out_dec(bytes / 1000000000ULL); out("."); out_dec((bytes / 100000000ULL) % 10); out(" GB"); }
	else if (bytes >= 1000000ULL) { out_dec(bytes / 1000000ULL); out(" MB"); }
	else if (bytes >= 1000ULL) { out_dec(bytes / 1000ULL); out(" KB"); }
	else { out_dec(bytes); out(bytes == 1 ? " byte" : " bytes"); }
}

#include "vibhaga.h"                         /* the partition table: shared with its fuzzer */

static void do_disk(void)
{
	colour = SAFFRON; out("  Kosa, the storehouse "); colour = DIM; out("(it can only read: no write command exists in it)\n"); colour = FG;
	if (!kosa.made) {
		if (!make_kosa()) { colour = RED; out("  Kosa could not be made: "); out(kosa_why ? kosa_why : "?"); out("\n"); colour = FG; return; }
		seL4_Word gen = 0, st = 0;
		if (!wait_for(kosa, L_READY, 5, &gen, &st)) { colour = RED; out("  no word from Kosa.\n"); colour = FG; return; }
		kosa.awake = true; kosa.present = st == 0;
	}
	if (!kosa.awake) { colour = DIM; out("  Kosa is not awake.\n"); colour = FG; return; }
	if (kwin->status) { colour = RED; out("  the controller did not start (code "); out_hex(kwin->status); out(")\n"); colour = FG; return; }
	colour = DIM; out("  controller "); colour = FG; out(kwin->model); colour = DIM; out("  firmware "); colour = FG; out(kwin->firmware);
	colour = DIM; out("  at PCI "); out_hexw(nvme_fn.bus, 2); out(":"); out_hexw(nvme_fn.dev, 2); out("."); out_hexw(nvme_fn.fn, 1); out("\n");
	colour = DIM; out("  capacity   "); colour = FG; out_size(kwin->blocks * kwin->block_size);
	colour = DIM; out(" ("); out_dec(kwin->blocks); out(" blocks of "); out_dec(kwin->block_size); out(" bytes)\n"); colour = FG;

	list_partitions(disk_read, kdata, kwin->block_size, kwin->blocks);
}
