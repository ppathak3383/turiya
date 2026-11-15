/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sakshi's side of Nadi (the USB host organ). Included after kosa_host.h. Made only when 'usb' is typed.
 */


static const char *nadi_why;
static pci_fn xhci_fn;
static bool have_xhci;

static bool nadi_fail(const char *why) { nadi_why = why; return false; }

static bool make_nadi(void)
{
	pci_scan();
	for (unsigned i = 0; i < npci && !have_xhci; i++)
		if (pcis[i].cls == 0x0c && pcis[i].sub == 0x03 && pcis[i].pif == 0x30) { xhci_fn = pcis[i]; have_xhci = true; }
	if (!have_xhci) return nadi_fail("there is no USB 3 (xHCI) controller on this machine");
	pci_fn &p = xhci_fn;
	uint32_t b0 = pci_read(p.bus, p.dev, p.fn, 0x10);
	if (b0 & 1) return nadi_fail("its registers are an I/O port range");
	uint64_t bar = b0 & ~0xfULL;
	if (((b0 >> 1) & 3) == 2) bar |= (uint64_t)pci_read(p.bus, p.dev, p.fn, 0x14) << 32;
	if (!bar) return nadi_fail("its registers have no address");
	uint16_t cmd = (uint16_t)pci_read(p.bus, p.dev, p.fn, 0x04);
	pci_write16(p.bus, p.dev, p.fn, 0x04, (uint16_t)(cmd | 0x6));      /* memory space and bus mastering on */

	if (!nduts) duts_init();
	if (!make_organ(nadi, 0, NO_IRQ, 0, 150)) return nadi_fail(nadi.step);
	for (unsigned i = 0; i < NADI_REG_PAGES; i++) {
		seL4_CPtr f = dev_page(bar + i * 4096UL);
		if (!f || organ_map(nadi, f, NADI_REGS_VA + i * 4096UL, seL4_ReadWrite, seL4_X86_CacheDisabled))
			return nadi_fail("its registers could not be mapped");
	}
	uptr view = nadi.scratch + 0x800000;
	seL4_CPtr wf = alloc_slot();
	if (!wf || retype(ram_ut, seL4_X86_4K, 0, wf, 1)) return nadi_fail("no window page");
	if (map_in(seL4_CapInitThreadVSpace, wf, view, seL4_ReadWrite, seL4_X86_Default_VMAttributes)
	    || organ_map(nadi, wf, NADI_WINDOW_VA, seL4_ReadWrite, seL4_X86_Default_VMAttributes))
		return nadi_fail("the window could not be mapped");
	nwin = (NadiWindow *)view;
	memset(nwin, 0, 4096);
	for (unsigned i = 0; i < NADI_NDMA; i++) {
		seL4_CPtr f = alloc_slot();
		if (!f || retype(ram_ut, seL4_X86_4K, 0, f, 1)) return nadi_fail("no DMA page");
		seL4_X86_Page_GetAddress_t a = seL4_X86_Page_GetAddress(f);
		if (a.error) return nadi_fail("no DMA address");
		nwin->pa[i] = a.paddr; nwin->va[i] = NADI_DMA_VA + i * 4096UL;
		if (organ_map(nadi, f, nwin->va[i], seL4_ReadWrite, seL4_X86_Default_VMAttributes)) return nadi_fail("DMA map");
	}
	nwin->bar_va = NADI_REGS_VA; nwin->tsc_hz = tsc_hz;
	if (!remake(nadi) || start(nadi)) return nadi_fail("its first start");
	nadi.made = true;
	return true;
}

static const char *speed_word(unsigned s)
{
	return s == 1 ? "full speed" : s == 2 ? "low speed" : s == 3 ? "high speed" : s == 4 ? "super speed" : "speed ?";
}

static void do_usb(void)
{
	colour = SAFFRON; out("  Nadi, the channel "); colour = DIM; out("(USB: finds what is plugged in; listens to a USB keyboard)\n"); colour = FG;
	if (!nadi.made) {
		if (!make_nadi()) { colour = RED; out("  Nadi could not be made: "); out(nadi_why ? nadi_why : "?"); out("\n"); colour = FG; return; }
		seL4_Word gen = 0, st = 0;
		if (!wait_for(nadi, L_READY, 10, &gen, &st)) { colour = RED; out("  no word from Nadi.\n"); colour = FG; return; }
		nadi.awake = true;
	}
	if (!nadi.awake) { colour = DIM; out("  Nadi is not awake.\n"); colour = FG; return; }
	colour = DIM; out("  controller "); colour = FG; out("xHCI "); out_hex(nwin->version);
	colour = DIM; out("  at PCI "); out_hexw(xhci_fn.bus, 2); out(":"); out_hexw(xhci_fn.dev, 2); out("."); out_hexw(xhci_fn.fn, 1);
	out(", "); out_dec(nwin->max_ports); out(" ports, "); out_dec(nwin->max_slots); out(" slots, ");
	out_dec(nwin->scratchpads); out(" scratchpad pages\n"); colour = FG;
	if (nwin->status) {
		colour = RED; out("  the controller did not start (code "); out_hex(nwin->status); out(", step "); out_dec(nwin->step); out(")\n");
		colour = FG; return;
	}
	colour = DIM; out("  connected  "); colour = FG; out_dec(nwin->ports_connected); out(nwin->ports_connected == 1 ? " port\n" : " ports\n");
	for (unsigned i = 0; i < nwin->ndev && i < NADI_MAXDEV; i++) {
		NadiDevice &d = nwin->dev[i];
		colour = DIM; out("    port "); out_dec(d.port); out("  "); colour = FG;
		out_pad(d.what, 22);
		colour = DIM; out_hexw(d.vendor, 4); out(":"); out_hexw(d.product, 4); out("  "); out(speed_word(d.speed));
		if (nwin->keyboard_slot && d.slot == nwin->keyboard_slot) { colour = GREEN; out("  <- listening: type on it"); }
		out("\n"); colour = FG;
	}
	if (!nwin->ndev) { colour = DIM; out("    nothing answered\n"); colour = FG; }
}
