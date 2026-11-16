/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - 'check': one screen that says how this machine and Turiya are, for one photo.
 * Reads only: PCI, ACPI tables, USB descriptors, a stick's top folder. It never touches the internal SSD.
 */

static void do_check(void)
{
	colour = SAFFRON; out("  check "); colour = DIM; out("(reads only; the internal SSD is not touched)\n");
	colour = DIM; out("  Turiya     "); colour = FG;
	out("heart "); out_dec(beats); out(" beats, breath "); out_dec(breaths);
	out("; "); out(organ_state());
	out("; judges agreed "); out_dec(judges_agree); out("/"); out_dec(judges_agree + judges_differ);
	out("; daily rounds "); out_dec(dina.rounds); out(", changes "); out_dec(dina.found); out("; own code "); out(self_words()); out("\n");
	colour = DIM; out("  processor  "); colour = FG; { char *c = cpu; while (*c == ' ') c++; out(c); }
	out(", "); out_dec(ram_mib); out(" MiB"); if (fb) { out(", "); out_dec(fb_w); out("x"); out_dec(fb_h); } out("\n");

	pci_scan();
	colour = DIM; out("  PCI        "); colour = FG; out_dec(npci_total); out(" functions:");
	unsigned col = 30;
	for (unsigned i = 0; i < npci; i++) {
		pci_fn &p = pcis[i];
		const char *what = 0;
		if (p.cls == 0x01 && p.sub == 0x08) what = "NVMe";
		else if (p.cls == 0x01 && p.sub == 0x06) what = "SATA";
		else if (p.cls == 0x0c && p.sub == 0x03 && p.pif == 0x30) what = "USB3";
		else if (p.cls == 0x0c && p.sub == 0x03 && p.pif == 0x40) what = "USB4";
		else if (p.cls == 0x03) what = "display";
		else if (p.cls == 0x02) what = p.sub ? "Wi-Fi" : "Ethernet";
		else if (p.cls == 0x04 && p.sub == 0x03) what = "audio";
		if (!what) continue;
		if (col > 76) { out("\n             "); col = 13; }
		out(" "); out(what); out(" "); out_hexw(p.bus, 2); out(":"); out_hexw(p.dev, 2); out("."); out_hexw(p.fn, 1);
		const char *vn = vendor_name(p.vendor);
		if (vn) { out(" "); out(vn); }
		out(";"); col += 26;
	}
	out("\n");

	acpi_survey();
	colour = DIM; out("  ACPI       "); colour = FG;
	if (acpi.ntables) { out(acpi.xsdt ? "XSDT, " : "RSDT, "); out_dec(acpi.ntables); out(" tables"); }
	if (acpi.s5a >= 0 && acpi.pm1a) { out("; power-off ready (PM1a "); out_hex(acpi.pm1a); out(", type "); out_dec((unsigned long)acpi.s5a); out(")"); }
	else { out("; power-off not known: "); out(acpi.why ? acpi.why : "?"); }
	out("\n");

	if (wake_nadis()) {
		for (unsigned k = 0; k < nxhci; k++) {
			NadiWindow *w = nwins[k];
			if (!NADIS[k]->made || !w) continue;
			colour = DIM; out("  USB "); out(NADIS[k]->name); out(k ? "  " : "   "); colour = FG;
			if (!NADIS[k]->awake) { out("not awake\n"); continue; }
			if (w->status) { colour = RED; out("did not start (code "); out_hex(w->status); out(", step "); out_dec(w->step); out(")\n"); colour = FG; continue; }
			out_dec(w->max_ports); out(" ports, "); out_dec(w->scratchpads); out(" scratchpads;");
			if (!w->ndev) out(" nothing plugged in");
			for (unsigned i = 0; i < w->ndev && i < NADI_MAXDEV; i++) {
				out(" port "); out_dec(w->dev[i].port); out(" "); out(w->dev[i].what); out(";");
			}
			out("\n");
		}
		bool stick = false;
		for (unsigned k = 0; k < nxhci; k++) if (NADIS[k]->awake && nwins[k] && nwins[k]->storage_slot) stick = true;
		if (stick) do_files("", 0);
	}
	colour = DIM; out("  Take one photo of this screen. 'disk' (the internal SSD) waits for your yes.\n"); colour = FG;
}
