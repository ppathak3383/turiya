/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sharira, the body: a read-only survey of the hardware, and power-off.
 *
 * Included into sakshi.cc (it uses Sakshi's screen and capability helpers). It only reads:
 *   PCI configuration space through ports 0xcf8/0xcfc, and the ACPI tables (RSDT/XSDT, FADT, DSDT)
 *   through a read-only window on physical memory. No ACPI code (AML) is ever run: the \_S5 sleep
 *   values are read out of the DSDT as data, and power-off is one write to the PM1 control port.
 */

static void out_hexw(unsigned long v, int w) { char b[17]; b[w] = 0; for (int i = w - 1; i >= 0; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15]; out(b); }
static void out_pad(const char *s, unsigned w) { unsigned n = (unsigned)slen(s); out(s); while (n++ < w) out(" "); }

/* ---- PCI ---- */
static uint32_t pci_read(unsigned bus, unsigned dev, unsigned fn, unsigned off)
{
	if (!cf9_port) return 0xffffffff;
	seL4_X86_IOPort_Out32(cf9_port, 0xcf8, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | (off & 0xfc));
	seL4_X86_IOPort_In32_t r = seL4_X86_IOPort_In32(cf9_port, 0xcfc);
	return r.error ? 0xffffffff : r.result;
}

struct pci_fn { uint8_t bus, dev, fn, cls, sub, pif; uint16_t vendor, device; };
static pci_fn pcis[96];
static unsigned npci, npci_total;

static void pci_scan(void)
{
	npci = npci_total = 0;
	for (unsigned bus = 0; bus < 256; bus++)
		for (unsigned dev = 0; dev < 32; dev++) {
			uint32_t id = pci_read(bus, dev, 0, 0);
			if ((id & 0xffff) == 0xffff) continue;
			unsigned fns = ((pci_read(bus, dev, 0, 0x0c) >> 16) & 0x80) ? 8 : 1;
			for (unsigned fn = 0; fn < fns; fn++) {
				uint32_t v = fn ? pci_read(bus, dev, fn, 0) : id;
				if ((v & 0xffff) == 0xffff) continue;
				uint32_t c = pci_read(bus, dev, fn, 8);
				npci_total++;
				if (npci < 96) pcis[npci++] = { (uint8_t)bus, (uint8_t)dev, (uint8_t)fn, (uint8_t)(c >> 24),
				                                 (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint16_t)v, (uint16_t)(v >> 16) };
			}
		}
}

static const char *vendor_name(uint16_t v)
{
	switch (v) {
	case 0x1022: case 0x1002: return "AMD";      case 0x8086: return "Intel";    case 0x10ec: return "Realtek";
	case 0x14e4: return "Broadcom"; case 0x168c: case 0x17cb: return "Qualcomm"; case 0x14c3: return "MediaTek";
	case 0x144d: return "Samsung";  case 0x1344: return "Micron";   case 0x1c5c: return "SK hynix";
	case 0x15b7: return "WD";       case 0x1987: return "Phison";   case 0x126f: return "SiliconMo";
	case 0x1e0f: return "KIOXIA";   case 0x1b4b: return "Marvell";  case 0x10de: return "NVIDIA";
	case 0x1234: case 0x1b36: case 0x1af4: return "QEMU";           case 0x103c: return "HP";
	default: return 0;
	}
}

/* what a PCI function is, in words; *why says what it means for Turiya */
static const char *class_name(const pci_fn &p, const char **why)
{
	*why = 0;
	switch (p.cls) {
	case 0x01:
		if (p.sub == 0x08) { *why = "storage: the next driver (NVMe)"; return "NVMe SSD controller"; }
		if (p.sub == 0x06) { *why = "storage (AHCI)"; return "SATA controller"; }
		return p.sub == 0x01 ? "IDE controller" : "storage controller";
	case 0x02: return p.sub == 0x00 ? "Ethernet controller" : "network controller (Wi-Fi?)";
	case 0x03: return p.sub == 0x00 ? "display controller (VGA)" : "display controller";
	case 0x04: return p.sub == 0x03 ? "audio (HD Audio)" : "multimedia controller";
	case 0x05: return "memory controller";
	case 0x06: return p.sub == 0x00 ? "host bridge" : p.sub == 0x01 ? "ISA bridge" : p.sub == 0x04 ? "PCI bridge" : "bridge";
	case 0x07: return "communication controller";
	case 0x08: return p.sub == 0x06 ? "IOMMU" : "system peripheral";
	case 0x09: return "input controller";
	case 0x0c:
		if (p.sub == 0x03) {
			*why = "USB: keyboards, sticks, phones";
			return p.pif == 0x30 ? "USB 3 controller (xHCI)" : p.pif == 0x40 ? "USB4 controller"
			     : p.pif == 0x20 ? "USB 2 controller (EHCI)" : "USB 1 controller";
		}
		return p.sub == 0x05 ? "SMBus controller" : "serial bus controller";
	case 0x0d: return "wireless controller";
	case 0x10: return "encryption controller";
	case 0x11: return "signal processing (sensors)";
	case 0x13: return "instrumentation";
	default: return "other";
	}
}

/* ---- device memory: the 4K frame at a physical address, from the device untyped that covers it.
 * Skipped space is kept as smaller untypeds, so a lower address can still be reached later. ---- */
struct dev_ut { uptr base; seL4_Word bits; seL4_CPtr cap; uptr water; };
static dev_ut duts[512];
static unsigned nduts;

static void duts_init(void)
{
	seL4_Word n = bootinfo->untyped.end - bootinfo->untyped.start;
	for (seL4_Word i = 0; i < n && nduts < 512; i++) {
		seL4_UntypedDesc *u = &bootinfo->untypedList[i];
		if (u->isDevice) duts[nduts++] = { u->paddr, u->sizeBits, bootinfo->untyped.start + i, u->paddr + dev_carved[i] };
	}
}

static seL4_CPtr dev_page(uptr pa)
{
	pa &= ~0xfffUL;
	dev_ut *d = 0;
	for (unsigned i = 0; i < nduts; i++) {
		dev_ut &u = duts[i];
		if (pa >= u.base && pa - u.base < (1UL << u.bits) && (!d || u.bits < d->bits)) d = &u;
	}
	if (!d || pa < d->water) return 0;
	while (d->water < pa) {
		seL4_Word k = 12;
		while (k + 1 < d->bits && !(d->water & ((1UL << (k + 1)) - 1)) && d->water + (1UL << (k + 1)) <= pa) k++;
		seL4_CPtr s = alloc_slot();
		if (!s || seL4_Untyped_Retype(d->cap, seL4_UntypedObject, k, seL4_CapInitThreadCNode, 0, 0, s, 1)) return 0;
		uptr b = d->water;
		d->water += 1UL << k;
		if (nduts < 512) duts[nduts++] = { b, k, s, b };
	}
	seL4_CPtr f = alloc_slot();
	if (!f || seL4_Untyped_Retype(d->cap, seL4_X86_4K, 0, seL4_CapInitThreadCNode, 0, 0, f, 1)) return 0;
	d->water += 4096;
	return f;
}

/* a read-only window on physical memory below 4 GiB, at PHYS_WIN + address */
enum : uptr { PHYS_WIN = 0x7000000000UL };
static uptr phys_pages[1024];
static unsigned nphys;

static const uint8_t *phys(uptr pa, uptr len)
{
	if (!len || pa + len > 0x100000000UL) return 0;
	if (!nduts) duts_init();
	for (uptr p = pa & ~0xfffUL; p < pa + len; p += 4096) {
		bool have = false;
		for (unsigned i = 0; i < nphys && !have; i++) have = phys_pages[i] == p;
		if (have) continue;
		if (nphys == 1024) return 0;
		seL4_CPtr f = dev_page(p);
		if (!f || map_in(seL4_CapInitThreadVSpace, f, PHYS_WIN + p, seL4_CanRead, seL4_X86_Default_VMAttributes)) return 0;
		phys_pages[nphys++] = p;
	}
	return (const uint8_t *)(PHYS_WIN + pa);
}

/* ---- ACPI tables ---- */
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static const uint8_t *acpi_table(uint64_t pa)
{
	if (!pa || pa >= 0x100000000UL) return 0;
	const uint8_t *h = phys(pa, 36);
	if (!h) return 0;
	uint32_t len = rd32(h + 4);
	if (len < 36 || len > (1u << 20)) return 0;
	return phys(pa, len);
}

struct acpi_state { bool done, xsdt; char sigs[64][5]; unsigned ntables; uint64_t fadt, dsdt;
                    unsigned pm1a, pm1b, century; int s5a, s5b; const char *why; };
static acpi_state acpi;

/* \_S5 is a data object in the DSDT: Name(_S5, Package(){a, b, ...}). Read its first two numbers. */
static int aml_num(const uint8_t *p, const uint8_t *e, const uint8_t **next)
{
	if (p >= e) return -1;
	if (*p == 0x0a && p + 1 < e) { *next = p + 2; return p[1]; }
	if (*p == 0x00 || *p == 0x01) { *next = p + 1; return *p; }
	if (*p == 0x0b && p + 2 < e) { *next = p + 3; return p[1] | p[2] << 8; }
	return -1;
}

static void find_s5(const uint8_t *d, uint32_t len)
{
	for (uint32_t i = 38; i + 10 < len; i++) {
		if (d[i] != '_' || d[i + 1] != 'S' || d[i + 2] != '5' || d[i + 3] != '_') continue;
		if (!(d[i - 1] == 0x08 || (d[i - 1] == '\\' && d[i - 2] == 0x08))) continue;
		const uint8_t *p = d + i + 4, *e = d + len, *n = 0;
		if (*p++ != 0x12) continue;                      /* PackageOp */
		p += ((*p >> 6) & 3) + 1;                        /* PkgLength */
		p++;                                             /* NumElements */
		int a = aml_num(p, e, &n);
		if (a < 0) continue;
		int b = aml_num(n, e, &n);
		acpi.s5a = a; acpi.s5b = b < 0 ? 0 : b;
		return;
	}
}

static void acpi_survey(void)
{
	if (acpi.done) return;
	acpi.done = true; acpi.s5a = acpi.s5b = -1;
	if (!have_rsdp) { acpi.why = "no RSDP was passed at boot"; return; }
	uint64_t xsdt = rsdp_copy[15] >= 2 ? rd64(rsdp_copy + 24) : 0;
	const uint8_t *root = acpi_table(xsdt);
	unsigned esz = 8;
	acpi.xsdt = root != 0;
	if (!root) { root = acpi_table(rd32(rsdp_copy + 16)); esz = 4; }
	if (!root) { acpi.why = "the root table could not be mapped"; return; }
	uint32_t len = rd32(root + 4);
	for (uint32_t off = 36; off + esz <= len && acpi.ntables < 64; off += esz) {
		uint64_t pa = esz == 8 ? rd64(root + off) : rd32(root + off);
		const uint8_t *t = pa < 0x100000000UL ? phys(pa, 36) : 0;
		char *s = acpi.sigs[acpi.ntables++];
		if (!t) { memcpy(s, "????", 5); continue; }
		memcpy(s, t, 4); s[4] = 0;
		for (int k = 0; k < 4; k++) if (s[k] < 32 || s[k] > 126) s[k] = '?';
		if (streq(s, "FACP")) acpi.fadt = pa;
	}
	const uint8_t *f = acpi_table(acpi.fadt);
	if (!f) { acpi.why = "no FADT"; return; }
	uint32_t flen = rd32(f + 4);
	acpi.century = flen > 108 ? f[108] : 0;       /* the CMOS index of the century register, 0 for none (Kala) */
	acpi.pm1a = rd32(f + 64); acpi.pm1b = rd32(f + 68);
	if (!acpi.pm1a && flen >= 184 && f[172] == 1) acpi.pm1a = (unsigned)rd64(f + 176);
	if (!acpi.pm1b && flen >= 196 && f[184] == 1) acpi.pm1b = (unsigned)rd64(f + 188);
	acpi.dsdt = flen >= 148 ? rd64(f + 140) : 0;
	if (!acpi.dsdt) acpi.dsdt = rd32(f + 40);
	const uint8_t *d = acpi_table(acpi.dsdt);
	if (!d) { acpi.why = "the DSDT could not be mapped"; return; }
	find_s5(d, rd32(d + 4));
	if (acpi.s5a < 0) acpi.why = "the DSDT has no \\_S5 sleep values";
	else if (!acpi.pm1a) acpi.why = "the FADT gives no PM1 control port";
}

static void show_body(void)
{
	colour = SAFFRON; out("  Sharira, the body "); colour = DIM; out("(a read-only survey: nothing was changed)\n");
	colour = DIM; out("  processor  "); colour = FG; { char *c = cpu; while (*c == ' ') c++; out(c); } out("\n");
	colour = DIM; out("  memory     "); colour = FG; out_dec(ram_mib); out(" MiB");
	if (fb) { colour = DIM; out("    screen "); colour = FG; out_dec(fb_w); out(" x "); out_dec(fb_h); }
	out("\n");
	pci_scan();
	colour = DIM; out("  PCI        "); colour = FG; out_dec(npci_total); out(" functions\n");
	for (unsigned i = 0; i < npci; i++) {
		pci_fn &p = pcis[i];
		const char *why, *what = class_name(p, &why), *vn = vendor_name(p.vendor);
		colour = DIM; out("    "); out_hexw(p.bus, 2); out(":"); out_hexw(p.dev, 2); out("."); out_hexw(p.fn, 1); out("  ");
		colour = FG;
		if (vn) out_pad(vn, 10); else { out_hexw(p.vendor, 4); out("      "); }
		out_pad(what, 30);
		if (why) { colour = GREEN; out("<- "); out(why); }
		out("\n");
	}
	acpi_survey();
	colour = DIM; out("  ACPI       "); colour = FG;
	if (!acpi.ntables) { out(acpi.why ? acpi.why : "no tables"); out("\n"); return; }
	out(acpi.xsdt ? "XSDT, " : "RSDT, "); out_dec(acpi.ntables); out(" tables:");
	unsigned col = 30;
	for (unsigned i = 0; i < acpi.ntables; i++) {
		if (col > 70) { out("\n            "); col = 12; }
		out(" "); out(acpi.sigs[i]); col += 5;
	}
	out("\n");
	colour = DIM; out("  power-off  "); colour = FG;
	if (acpi.s5a >= 0 && acpi.pm1a) {
		out("PM1a port "); out_hex(acpi.pm1a); if (acpi.pm1b) { out(", PM1b "); out_hex(acpi.pm1b); }
		out(", sleep type "); out_dec((unsigned long)acpi.s5a);
		colour = DIM; out(" (\\_S5 read from the DSDT as data; no ACPI code runs)\n");
	} else { out("not known: "); out(acpi.why ? acpi.why : "?"); out("\n"); }
	colour = FG;
}

static void do_poweroff(void)
{
	acpi_survey();
	if (acpi.s5a < 0 || !acpi.pm1a) {
		colour = RED; out("  power-off is not known here ("); out(acpi.why ? acpi.why : "?"); out("). Hold the power button.\n");
		colour = FG; return;
	}
	static seL4_CPtr pa, pb;
	if (!pa) pa = issue_ports(acpi.pm1a, acpi.pm1a + 1);
	if (!pb && acpi.pm1b) pb = issue_ports(acpi.pm1b, acpi.pm1b + 1);
	if (!pa) { colour = RED; out("  the power port could not be claimed. Hold the power button.\n"); colour = FG; return; }
	colour = SAFFRON; out("  Turiya goes into susupti, deep sleep: powering off.\n"); colour = FG;
	seL4_X86_IOPort_In16_t ra = seL4_X86_IOPort_In16(pa, acpi.pm1a);
	uint16_t ca = (uint16_t)(((ra.error ? 0 : ra.result) & ~0x3c00) | ((acpi.s5a & 7) << 10));
	uint16_t cb = 0;
	if (pb) {
		seL4_X86_IOPort_In16_t rb = seL4_X86_IOPort_In16(pb, acpi.pm1b);
		cb = (uint16_t)(((rb.error ? 0 : rb.result) & ~0x3c00) | ((acpi.s5b & 7) << 10));
	}
	seL4_X86_IOPort_Out16(pa, acpi.pm1a, ca);
	if (pb) seL4_X86_IOPort_Out16(pb, acpi.pm1b, cb);
	seL4_X86_IOPort_Out16(pa, acpi.pm1a, (uint16_t)(ca | 0x2000));      /* SLP_EN */
	if (pb) seL4_X86_IOPort_Out16(pb, acpi.pm1b, (uint16_t)(cb | 0x2000));
	for (int i = 0; i < 3000000; i++) __asm__ volatile ("pause");
	colour = RED; out("  the machine is still on: hold the power button.\n"); colour = FG;
}
