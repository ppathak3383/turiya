/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sakshi's side of Nadi (the USB host organ). One Nadi per USB 3 controller (up to two: many laptops,
 * the HP among them, have two). Included after kosa_host.h. Made only when 'usb' or 'stick' is typed.
 */

static const uint8_t *ndatas[2];       /* Sakshi's read-only views of each Nadi's data page */
static seL4_CPtr ndata_caps[2];        /* and the pages themselves, to lend to Smrti */
static pci_fn xhci_fns[2];
static unsigned nxhci;
static bool xhci_scanned;
static const char *nadi_why[2];
static uint32_t stick_seq;
static int stick_k = -1;               /* which Nadi holds the stick */

static bool nadi_fail(int k, const char *why) { nadi_why[k] = why; return false; }

static bool make_nadi(int k)
{
	organ &o = *NADIS[k];
	pci_fn &p = xhci_fns[k];
	uint32_t b0 = pci_read(p.bus, p.dev, p.fn, 0x10);
	if (b0 & 1) return nadi_fail(k, "its registers are an I/O port range");
	uint64_t bar = b0 & ~0xfULL;
	if (((b0 >> 1) & 3) == 2) bar |= (uint64_t)pci_read(p.bus, p.dev, p.fn, 0x14) << 32;
	if (!bar) return nadi_fail(k, "its registers have no address");
	uint16_t cmd = (uint16_t)pci_read(p.bus, p.dev, p.fn, 0x04);
	pci_write16(p.bus, p.dev, p.fn, 0x04, (uint16_t)(cmd | 0x6));      /* memory space and bus mastering on */

	if (!nduts) duts_init();
	if (!make_organ(o, 0, NO_IRQ, 0, 150)) return nadi_fail(k, o.step);
	/* how much register space this controller has: read its capability registers first (read-only look) */
	uptr view = o.scratch + 0x800000;
	seL4_CPtr f0 = dev_page(bar);
	if (!f0 || map_in(seL4_CapInitThreadVSpace, f0, view + 0x10000, seL4_CanRead, seL4_X86_CacheDisabled))
		return nadi_fail(k, "its registers could not be reached");
	volatile uint32_t *cap = (volatile uint32_t *)(view + 0x10000);
	uint32_t caplen = cap[0] & 0xff, hcs1 = cap[1], hcc1 = cap[4], dboff = cap[5] & ~3u, rtsoff = cap[6] & ~0x1fu;
	uint32_t span = caplen + 0x400 + 0x10 * (hcs1 >> 24);
	if (dboff + 4 * (NADI_MAXDEV + 1) > span) span = dboff + 4 * (NADI_MAXDEV + 1);
	if (rtsoff + 0x40 > span) span = rtsoff + 0x40;
	if (((hcc1 >> 16) << 2) + 0x100 > span) span = ((hcc1 >> 16) << 2) + 0x100;
	unsigned pages = (span + 4095) / 4096;
	if (pages > NADI_REG_PAGES) pages = NADI_REG_PAGES;
	for (unsigned i = 0; i < pages; i++) {
		seL4_CPtr f = i ? dev_page(bar + i * 4096UL) : f0;
		if (!f || organ_map(o, f, NADI_REGS_VA + i * 4096UL, seL4_ReadWrite, seL4_X86_CacheDisabled))
			return nadi_fail(k, "its registers could not be mapped");
	}
	seL4_CPtr wf = alloc_slot();
	if (!wf || retype(ram_ut, seL4_X86_4K, 0, wf, 1)) return nadi_fail(k, "no window page");
	if (map_in(seL4_CapInitThreadVSpace, wf, view, seL4_ReadWrite, seL4_X86_Default_VMAttributes)
	    || organ_map(o, wf, NADI_WINDOW_VA, seL4_ReadWrite, seL4_X86_Default_VMAttributes))
		return nadi_fail(k, "the window could not be mapped");
	NadiWindow *w = (NadiWindow *)view;
	nwins[k] = w;
	memset(w, 0, 4096);
	w->reg_bytes = pages * 4096u;
	for (unsigned i = 0; i < NADI_NDMA; i++) {
		seL4_CPtr f = alloc_slot();
		if (!f || retype(ram_ut, seL4_X86_4K, 0, f, 1)) return nadi_fail(k, "no DMA page");
		seL4_X86_Page_GetAddress_t a = seL4_X86_Page_GetAddress(f);
		if (a.error) return nadi_fail(k, "no DMA address");
		w->pa[i] = a.paddr; w->va[i] = NADI_DMA_VA + i * 4096UL;
		if (organ_map(o, f, w->va[i], seL4_ReadWrite, seL4_X86_Default_VMAttributes)) return nadi_fail(k, "DMA map");
		if (i == NADI_DATA) {
			if (map_in(seL4_CapInitThreadVSpace, f, view + 4096, seL4_CanRead, seL4_X86_Default_VMAttributes))
				return nadi_fail(k, "data view");
			ndatas[k] = (const uint8_t *)(view + 4096); ndata_caps[k] = f;
		}
	}
	w->bar_va = NADI_REGS_VA; w->tsc_hz = tsc_hz; w->reg_bytes = pages * 4096u;
	if (!remake(o) || start(o)) return nadi_fail(k, "its first start");
	o.made = true;
	return true;
}

static const char *speed_word(unsigned s)
{
	return s == 1 ? "full speed" : s == 2 ? "low speed" : s == 3 ? "high speed" : s == 4 ? "super speed" : "speed ?";
}

/* make a Nadi for every USB 3 controller (at most two); true if at least one is awake */
static bool wake_nadis(void)
{
	if (!xhci_scanned) {
		xhci_scanned = true;
		pci_scan();
		for (unsigned i = 0; i < npci && nxhci < 2; i++)
			if (pcis[i].cls == 0x0c && pcis[i].sub == 0x03 && pcis[i].pif == 0x30) xhci_fns[nxhci++] = pcis[i];
	}
	if (!nxhci) { colour = RED; out("  there is no USB 3 (xHCI) controller on this machine\n"); colour = FG; return false; }
	bool any = false;
	for (unsigned k = 0; k < nxhci; k++) {
		organ &o = *NADIS[k];
		if (!o.made) {
			if (!make_nadi((int)k)) {
				colour = RED; out("  "); out(o.name); out(" could not be made: "); out(nadi_why[k] ? nadi_why[k] : "?"); out("\n");
				colour = FG; continue;
			}
			seL4_Word gen = 0, st = 0;
			if (!wait_for(o, L_READY, 10, &gen, &st)) { colour = RED; out("  no word from "); out(o.name); out(".\n"); colour = FG; continue; }
			o.awake = true;
		}
		any |= o.awake;
	}
	return any;
}

static void do_usb(void)
{
	colour = SAFFRON; out("  Nadi, the channel "); colour = DIM; out("(USB: finds what is plugged in; listens to a USB keyboard)\n"); colour = FG;
	if (!wake_nadis()) return;
	for (unsigned k = 0; k < nxhci; k++) {
		organ &o = *NADIS[k];
		NadiWindow *w = nwins[k];
		if (!o.made || !w) continue;
		colour = DIM; out("  "); out(o.name); out("  xHCI "); out_hex(w->version);
		out(" at PCI "); out_hexw(xhci_fns[k].bus, 2); out(":"); out_hexw(xhci_fns[k].dev, 2); out("."); out_hexw(xhci_fns[k].fn, 1);
		out(", "); out_dec(w->max_ports); out(" ports, "); out_dec(w->max_slots); out(" slots, ");
		out_dec(w->scratchpads); out(" scratchpad pages\n"); colour = FG;
		if (!o.awake) { colour = DIM; out("    not awake\n"); colour = FG; continue; }
		if (w->status) {
			colour = RED; out("    the controller did not start (code "); out_hex(w->status); out(", step "); out_dec(w->step); out(")\n");
			colour = FG; continue;
		}
		for (unsigned i = 0; i < w->ndev && i < NADI_MAXDEV; i++) {
			NadiDevice &d = w->dev[i];
			colour = DIM; out("    port "); out_dec(d.port); out(d.port < 10 ? "   " : "  "); colour = FG;
			out_pad(d.what, 22);
			colour = DIM; out_hexw(d.vendor, 4); out(":"); out_hexw(d.product, 4); out("  "); out(speed_word(d.speed));
			if (w->keyboard_slot && d.slot == w->keyboard_slot) { colour = GREEN; out("  <- listening: type on it"); }
			if (w->storage_slot && d.slot == w->storage_slot) { colour = GREEN; out("  <- type 'stick' to read it"); }
			out("\n"); colour = FG;
		}
		if (!w->ndev) { colour = DIM; out("    nothing plugged in\n"); colour = FG; }
	}
}

static bool stick_read(uint64_t lba, uint32_t count)
{
	NadiWindow *w = nwins[stick_k];
	w->req_lba = lba; w->req_count = count; w->req_seq = ++stick_seq;
	seL4_Signal(NADIS[stick_k]->req);
	seL4_Word seq = 0, st = 0;
	if (!wait_for(*NADIS[stick_k], L_DONE, 5, &seq, &st)) return false;
	return seq == stick_seq && st == 0;
}

static void do_stick(void)
{
	if (!wake_nadis()) return;
	stick_k = -1;
	for (unsigned k = 0; k < nxhci && stick_k < 0; k++)
		if (NADIS[k]->awake && nwins[k] && nwins[k]->storage_slot) stick_k = (int)k;
	colour = SAFFRON; out("  Nadi reads the USB stick "); colour = DIM; out("(only read commands exist in it)\n"); colour = FG;
	if (stick_k < 0) { colour = DIM; out("  no USB stick is ready; 'usb' shows what is plugged in\n"); colour = FG; return; }
	NadiWindow *w = nwins[stick_k];
	colour = DIM; out("  stick      "); colour = FG; out(w->storage_vendor); out(" "); out(w->storage_product);
	colour = DIM; out("  (through "); out(NADIS[stick_k]->name); out(")\n");
	colour = DIM; out("  capacity   "); colour = FG; out_size(w->storage_blocks * w->storage_block_size);
	colour = DIM; out(" ("); out_dec(w->storage_blocks); out(" blocks of "); out_dec(w->storage_block_size); out(" bytes)\n"); colour = FG;
	list_partitions(stick_read, ndatas[stick_k], w->storage_block_size, w->storage_blocks);
}
