/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sakshi's side of Kala, the clock. kala.h decides what the clock's bytes mean; this file only reads
 * them. Included into sakshi.cc after sharira.h (the FADT names the century register).
 *
 * Sakshi already holds ports 0x70/0x71 for its breath (the RTC's periodic interrupt, 2 Hz). Reading the time
 * selects and reads registers 0x00-0x0b and the century register, and nothing else: no clock register is ever
 * written, so status A and B (the breath's rate and its interrupt) stay as they are. Sakshi is one thread, so a
 * read never interleaves with the breath's own read of register C.
 *
 * The clock is read once a heartbeat (each second) for the status bar, and afresh for 'time' and 'check'.
 * The RTC knows no time zone: Turiya shows the time as the machine's clock keeps it (Windows keeps local time).
 */

struct Cmos_port {
	unsigned long reads;
	Turiya::Kala::byte operator()(Turiya::Kala::byte reg)
	{
		reads++;
		outb(rtc_port, 0x70, reg);
		return inb(rtc_port, 0x71);
	}
};

static unsigned long kala_reads, kala_unknown;
static unsigned kala_century_fadt;          /* the century register the FADT names (0: none) */
static unsigned char kala_century;          /* the same, when Sakshi can read it (0x0e-0x7f), else 0 */
static bool kala_looked;

static const char *const KALA_WDAY[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const KALA_MON[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                          "September", "October", "November", "December" };

static Turiya::Kala::Time kala_read(unsigned long *reads)
{
	if (!kala_looked) {
		kala_looked = true;
		acpi_survey();
		kala_century_fadt = acpi.century;
		kala_century = acpi.century >= 0x0e && acpi.century < 0x80 ? (unsigned char)acpi.century : 0;
	}
	Cmos_port p { 0 };
	Turiya::Kala::Time t = Turiya::Kala::read(p, kala_century);
	if (reads) *reads = p.reads;
	kala_reads++;
	if (!t.known) kala_unknown++;
	return t;
}

/* "Tue 17 Nov 2026 07:52" (21 bytes and a 0), or "clock unknown" */
static void kala_bar_text(Turiya::Kala::Time const &t, char *o)
{
	unsigned k = 0;
	auto add = [&](const char *s, unsigned n) { for (unsigned i = 0; i < n && s[i]; i++) o[k++] = s[i]; };
	auto two = [&](unsigned v) { o[k++] = (char)('0' + v / 10 % 10); o[k++] = (char)('0' + v % 10); };
	if (!t.known) { add("clock unknown", 13); o[k] = 0; return; }
	add(KALA_WDAY[t.weekday % 7], 3); o[k++] = ' ';
	two(t.day); o[k++] = ' ';
	add(KALA_MON[(t.month - 1) % 12], 3); o[k++] = ' ';
	two(t.year / 100); two(t.year); o[k++] = ' ';
	two(t.hour); o[k++] = ':'; two(t.minute);
	o[k] = 0;
}

/* once a heartbeat, before the status bar is drawn */
static void clock_tick(void)
{
	if (!rtc_port) { clock_bar[0] = 0; return; }
	kala_bar_text(kala_read(nullptr), clock_bar);
}

/* for 'check': one short piece of a line */
static void kala_check(void)
{
	colour = DIM; out("; clock ");
	if (!rtc_port) { colour = RED; out("no RTC ports"); colour = FG; return; }
	Turiya::Kala::Time t = kala_read(nullptr);
	char s[20]; Turiya::Kala::format(t, s);
	colour = t.known ? FG : RED; out(s);
	if (!t.known) { out(", "); out(Turiya::Kala::why_name(t.why)); }
	colour = FG;
}

static void show_time(void)
{
	using namespace Turiya::Kala;
	colour = SAFFRON; out("  Kala, the clock  "); colour = FG;
	if (!rtc_port) { colour = RED; out("no clock: Sakshi was not given the RTC's ports\n"); colour = FG; return; }
	unsigned long reads = 0;
	Time t = kala_read(&reads);
	outb(rtc_port, 0x70, R_B); byte b = inb(rtc_port, 0x71);
	if (t.known) {
		char s[20]; format(t, s);
		out(KALA_WDAY[t.weekday % 7]); out(" "); out_dec(t.day); out(" "); out(KALA_MON[(t.month - 1) % 12]); out(" ");
		out_dec(t.year); out(", "); out(s + 11); out("\n");
	} else { colour = RED; out("unknown: "); out(why_name(t.why)); out("\n"); colour = FG; }
	colour = DIM;
	out("  as the machine's clock keeps it; the clock knows no time zone (Windows keeps it in local time)\n");
	out("  the clock counts in "); out(b & B_BINARY ? "binary" : "BCD"); out(", "); out(b & B_24H ? "24-hour" : "12-hour");
	out("; century register ");
	if (!kala_century_fadt) out("none named by the firmware (years are read as 20xx)");
	else if (!kala_century) { out_hex(kala_century_fadt); out(" named but out of Sakshi's reach, not used"); }
	else { out_hex(kala_century); out(t.known ? " says 20" : " read"); }
	out(t.known ? "\n  read twice alike, " : "\n  "); out_dec(reads); out(" register reads; "); out_dec(kala_reads);
	out(" readings since start, "); out_dec(kala_unknown); out(" unknown. Nothing in the clock is ever set.\n");
	colour = FG;
}
