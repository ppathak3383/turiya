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

static const char *gpt_type(const uint8_t *g)
{
	static const struct { uint8_t id[16]; const char *name; } T[] = {
		{ { 0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b }, "EFI system" },
		{ { 0x16,0xe3,0xc9,0xe3,0x5c,0x0b,0xb8,0x4d,0x81,0x7d,0xf9,0x2d,0xf0,0x02,0x15,0xae }, "Microsoft reserved" },
		{ { 0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7 }, "basic data (Windows)" },
		{ { 0xa4,0xbb,0x94,0xde,0xd1,0x06,0x40,0x4d,0xa1,0x6a,0xbf,0xd5,0x01,0x79,0xd6,0xac }, "Windows recovery" },
		{ { 0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4 }, "Linux" },
		{ { 0x6d,0xfd,0x57,0x06,0xab,0xa4,0xc4,0x43,0x84,0xe5,0x09,0x33,0xc8,0x4b,0x4f,0x4f }, "Linux swap" },
	};
	for (auto &t : T) {
		unsigned k = 0;
		while (k < 16 && t.id[k] == g[k]) k++;
		if (k == 16) return t.name;
	}
	return "other";
}

/* the partition table of any block device Sakshi can read through an organ: GPT, else MBR */
static void list_partitions(bool (*rd)(uint64_t, uint32_t), const uint8_t *data, uint32_t bs, uint64_t blocks)
{
	if (!rd(0, 1)) { colour = RED; out("  block 0 could not be read\n"); colour = FG; return; }
	bool gpt = data[510] == 0x55 && data[511] == 0xaa && data[450] == 0xee;
	if (data[510] == 0x55 && data[511] == 0xaa && !gpt) {
		colour = DIM; out("  partitions (MBR):\n"); colour = FG;
		uint8_t mbr[64]; for (int i = 0; i < 64; i++) mbr[i] = data[446 + i];
		unsigned found = 0;
		for (int k = 0; k < 4; k++) {
			const uint8_t *e = mbr + 16 * k;
			if (!e[4]) continue;
			found++;
			uint64_t n = rd32(e + 12);
			colour = DIM; out("    "); out_dec(k + 1); out("   "); colour = FG;
			const char *t = e[4] == 0x0c || e[4] == 0x0b ? "FAT32" : e[4] == 0x07 ? "NTFS or exFAT" : e[4] == 0x83 ? "Linux"
			              : e[4] == 0x06 || e[4] == 0x0e ? "FAT16" : e[4] == 0xef ? "EFI system" : "other";
			out_pad(t, 30);
			unsigned c0 = cur_x; out_size(n * bs); while (cur_x < c0 + 11) out(" ");
			colour = DIM; out("type "); out_hexw(e[4], 2); out(e[0] == 0x80 ? ", bootable\n" : "\n"); colour = FG;
		}
		if (!found) { colour = DIM; out("    none\n"); colour = FG; }
		return;
	}
	if (!rd(1, 1)) { colour = RED; out("  block 1 could not be read\n"); colour = FG; return; }
	static const char sig[] = "EFI PART";
	for (int i = 0; i < 8; i++) if (data[i] != (uint8_t)sig[i]) { colour = DIM; out("  no partition table\n"); colour = FG; return; }
	uint64_t elba = rd64(data + 72);
	uint32_t n = rd32(data + 80), esz = rd32(data + 84);
	if (esz < 128 || esz > 4096 || (4096 % esz) || !n) { colour = RED; out("  the partition table looks damaged\n"); colour = FG; return; }
	if (n > 128) n = 128;
	unsigned per = 4096 / esz, found = 0;
	colour = DIM; out("  partitions (GPT):\n"); colour = FG;
	for (uint32_t first = 0; first < n; first += per) {
		uint64_t lba = elba + (uint64_t)first * esz / bs;
		uint32_t cnt = 4096 / bs;
		if (lba + cnt > blocks) break;
		if (!rd(lba, cnt)) { colour = RED; out("  the partition entries could not be read\n"); colour = FG; return; }
		for (unsigned k = 0; k < per && first + k < n; k++) {
			const uint8_t *e = data + k * esz;
			bool used = false;
			for (int j = 0; j < 16; j++) used |= e[j] != 0;
			if (!used) continue;
			found++;
			uint64_t lo = rd64(e + 32), hi = rd64(e + 40);
			char name[37]; unsigned m = 0;
			for (unsigned j = 0; j < 36; j++) {
				uint16_t ch = (uint16_t)(e[56 + 2 * j] | e[57 + 2 * j] << 8);
				if (!ch) break;
				name[m++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
			}
			name[m] = 0;
			colour = DIM; out("    "); out_dec(first + k + 1); out(first + k + 1 < 10 ? "   " : "  "); colour = FG;
			out_pad(m ? name : "(no name)", 30);
			unsigned long long bytes = (hi >= lo ? hi - lo + 1 : 0) * (unsigned long long)bs;
			unsigned c0 = cur_x; out_size(bytes); while (cur_x < c0 + 11) out(" ");
			colour = DIM; out(gpt_type(e)); out("\n"); colour = FG;
		}
	}
	if (!found) { colour = DIM; out("    none\n"); colour = FG; }
}

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
