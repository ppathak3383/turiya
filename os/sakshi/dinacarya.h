/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Dinacarya, the daily round. Every minute Sakshi re-reads what must never change: each organ's
 * read-only pages (its code and constants) and Sakshi's own code. A page that differs from its seal is healed
 * at once: the organ is re-made from its SHA-256-checked seed. Included into sakshi.cc before kosa_host.h.
 *
 * What it can catch: a stray write by Sakshi through its own view of an organ's pages, a device writing memory
 * it was not given (DMA reaches memory directly; no IOMMU is set up yet), a bit that flips in memory. An organ
 * cannot change its own read-only pages: seL4 maps them read-only.
 *
 * Sakshi cannot re-make itself. If its own code changes, it says so and asks for a restart, which loads it
 * fresh from the stick. Its build seal (stamped after linking by seal.py) also shows whether what was loaded
 * is what was built.
 */

extern "C" const unsigned char __executable_start[], _end[];

/* the build seal: 16 bytes of mark, then the SHA-256 of Sakshi's read-only segments as built. seal.py writes
 * the digest after linking. It sits in .data, which the seal does not cover. */
__attribute__((used)) static volatile unsigned char SAKSHI_SEAL[48] =
	{ 'T', 'U', 'R', 'I', 'Y', 'A', '-', 'S', 'E', 'A', 'L', '-', 'v', '1', 0, 0 };

static struct {
	unsigned long rounds, pages, found, healed, rested;
	unsigned long last_us, max_us, last_beat, self_bytes, healed_now, rested_now;
	bool due, show_next;
} dina;

enum SelfState : unsigned char { SELF_UNKNOWN, SELF_SEALED, SELF_UNSTAMPED, SELF_NOT_AS_BUILT };
static SelfState self_state;
static Turiya::u8 self_boot[32];          /* Sakshi's own read-only image, as found at start */
static bool self_changed;                 /* it changed while running (found by a round) */
static char dina_why[8][96];              /* what each organ's last change was, for its report */

static bool same32(const Turiya::u8 *a, const Turiya::u8 *b)
{
	unsigned char d = 0;
	for (int i = 0; i < 32; i++) d |= (unsigned char)(a[i] ^ b[i]);
	return d == 0;
}

/* the SHA-256 of Sakshi's own read-only segments, as loaded; false if its ELF header no longer makes sense */
static bool self_digest(Turiya::u8 out[32], unsigned long *bytes)
{
	const elf_ehdr *e = (const elf_ehdr *)__executable_start;
	if (e->ident[0] != 0x7f || e->ident[1] != 'E' || e->ident[4] != 2 || !e->phnum || e->phnum > 16
	    || e->phentsize != sizeof(elf_phdr) || e->phoff + 16 * sizeof(elf_phdr) > 4096) return false;
	Turiya::Sha256 s; unsigned long n = 0;
	for (unsigned i = 0; i < e->phnum; i++) {
		const elf_phdr *ph = (const elf_phdr *)(__executable_start + e->phoff + (uptr)i * sizeof(elf_phdr));
		if (ph->type != 1 || (ph->flags & 2) || !ph->filesz) continue;
		if (ph->vaddr < (uptr)__executable_start || ph->vaddr + ph->filesz > (uptr)_end || ph->vaddr + ph->filesz < ph->vaddr)
			return false;
		s.add((const Turiya::u8 *)ph->vaddr, ph->filesz); n += ph->filesz;
	}
	s.finish(out);
	if (bytes) *bytes = n;
	return n != 0;
}

/* at start: is what was loaded what was built? */
static void self_check_at_start(void)
{
	if (!self_digest(self_boot, &dina.self_bytes)) { self_state = SELF_NOT_AS_BUILT; return; }
	Turiya::u8 stamp[32]; bool stamped = false;
	for (int i = 0; i < 32; i++) { stamp[i] = SAKSHI_SEAL[16 + i]; if (stamp[i]) stamped = true; }
	self_state = !stamped ? SELF_UNSTAMPED : same32(stamp, self_boot) ? SELF_SEALED : SELF_NOT_AS_BUILT;
}

static const char *self_words(void)
{
	if (self_changed) return "CHANGED while running: restart the machine";
	return self_state == SELF_SEALED ? "matches its build seal" : self_state == SELF_UNSTAMPED ? "not sealed at build"
	     : self_state == SELF_NOT_AS_BUILT ? "DIFFERS from what was built: write the stick again" : "not checked";
}

/* re-read one organ's read-only pages; heal it if one changed. Returns true if it was healed. */
static bool round_organ(organ &o, bool from_loop)
{
	if (!o.made || o.resting || o.t_fall) return false;      /* not made, asleep, or being healed right now */
	for (unsigned k = 0; k < o.npages; k++) {
		if (o.pages[k].w) continue;
		Turiya::Sha256 s; s.add((const Turiya::u8 *)(o.scratch + k * 4096UL), 4096);
		Turiya::u8 d[32]; s.finish(d);
		dina.pages++;
		if (same32(d, o.ro_seal[k])) continue;
		dina.found++; o.changed++;
		/* the report names the page; which byte changed is not kept */
		char *w = dina_why[o.id & 7]; unsigned m = 0;
		auto add = [&](const char *t) { while (*t && m < sizeof dina_why[0] - 1) w[m++] = *t++; };
		char hx[17]; uptr v = o.pages[k].va;
		for (int i = 15; i >= 0; i--) { hx[i] = "0123456789abcdef"[v & 15]; v >>= 4; }
		hx[16] = 0;
		const char *h = hx; while (*h == '0' && h[1]) h++;
		add("its read-only page at 0x"); add(h); add(" had changed (found by the daily round)");
		w[m] = 0;
		if (o.changed >= 3) {
			dina.rested++; dina.rested_now++;
			interject_begin(); colour = RED; out("  "); out(o.name); out(": "); out(w); out("\n"); colour = FG;
			bool shown = prompt_shown;
			if (!from_loop) prompt_shown = true;               /* inside a command: its prompt comes after it */
			organ_rest(o, "its code changed 3 times; the memory under it may be failing");
			if (!from_loop) prompt_shown = shown;
			return false;
		}
		organ_revive(o, w);                                    /* heal first (re-made from its seed), tell after */
		dina.healed++;
		return true;
	}
	return false;
}

/* the daily round: every organ, then Sakshi itself. Heal first: when an organ has changed, it is re-made at once
 * and the round stops there; it goes on from the start as soon as the organ is awake again. */
static void dinacarya_round(bool show, bool from_loop)
{
	organ *const all[] = { &tvak, &kosa, &hrdaya, &nadi, &nadi2, &smrti };
	uint64_t t0 = rdtsc();
	unsigned long pages0 = dina.pages; unsigned looked = 0;
	for (organ *o : all) {
		if (o->made && !o->resting && !o->t_fall) looked++;
		if (round_organ(*o, from_loop)) {
			dina.due = true; dina.show_next = dina.show_next || show; dina.healed_now++;
			return;                                          /* nothing written now: the report comes when it is awake */
		}
	}
	Turiya::u8 d[32]; unsigned long n = 0;
	bool ok = self_digest(d, &n);
	if ((!ok || !same32(d, self_boot)) && !self_changed) {
		self_changed = true; dina.found++;
		interject_begin(); colour = RED;
		out("  Sakshi's own code changed while running (found by the daily round). Sakshi cannot re-make itself:\n");
		out("  restart the machine to load it fresh from the stick.\n"); colour = FG;
		if (from_loop) interject_end();
	}
	uint64_t dt = rdtsc() - t0;
	dina.rounds++; dina.last_beat = beats; dina.due = false;
	dina.last_us = tsc_hz ? (unsigned long)(dt * 1000000 / tsc_hz) : 0;
	if (dina.last_us > dina.max_us) dina.max_us = dina.last_us;
	unsigned long healed = dina.healed_now, rested = dina.rested_now; dina.healed_now = dina.rested_now = 0;
	bool say = show || dina.show_next; dina.show_next = false;
	if (!say) return;
	if (from_loop) interject_begin();
	colour = DIM; out("  daily round "); out_dec(dina.rounds); out(": "); out_dec(looked); out(looked == 1 ? " organ, " : " organs, ");
	out_dec(dina.pages - pages0); out(" read-only pages and Sakshi's own "); out_dec(n); out(" bytes re-read");
	if (tsc_hz) { out(" in "); out_dec(dina.last_us); out(" microseconds"); }
	out("\n  ");
	if (self_changed) { colour = RED; out("Sakshi's own code has changed: restart the machine\n"); }
	else if (healed || rested) {
		colour = SAFFRON; out_dec(healed + rested); out(healed + rested == 1 ? " organ had changed: " : " organs had changed: ");
		if (healed) { out_dec(healed); out(healed == 1 ? " re-made from its seed" : " re-made from their seeds"); }
		if (rested) { out(healed ? ", " : ""); out_dec(rested); out(" resting (changed 3 times)"); }
		out("\n");
	}
	else { colour = GREEN; out("nothing changed; Sakshi's own code "); out(self_words()); out("\n"); }
	colour = FG;
	if (from_loop) interject_end();
}

/* run a round when one is due and nothing is waiting for an organ (rounds run from the main loop only) */
static void dinacarya_due(void)
{
	if (dina.due && !hold_prompt) dinacarya_round(false, true);
}

/* is this address covered by a segment's file bytes (code or constants), or only zero fill? */
static bool seed_covers(organ &o, uptr va)
{
	const elf_ehdr *e = (const elf_ehdr *)o.seed;
	for (unsigned i = 0; i < e->phnum; i++) {
		const elf_phdr *ph = phdr(o, i);
		if (ph->type == 1 && va >= ph->vaddr && va < ph->vaddr + ph->filesz) return true;
	}
	return false;
}

/* the test: flip one bit in an organ's read-only page, beyond its code, so it keeps running until the round */
static void taint_organ(organ &o)
{
	colour = DIM;
	if (!o.made || !o.awake || o.resting) { out("  "); out(o.name); out(" is not awake.\n"); colour = FG; return; }
	for (int k = (int)o.npages - 1; k >= 0; k--) {
		if (o.pages[k].w) continue;
		for (int off = 4095; off >= 0; off--) {
			uptr va = o.pages[k].va + (uptr)off;
			if (seed_covers(o, va)) break;                     /* only the zero fill after the page's last byte */
			volatile Turiya::u8 *p = (volatile Turiya::u8 *)(o.scratch + (uptr)k * 4096UL + (uptr)off);
			*p = (Turiya::u8)(*p ^ 0x01);
			out("  flipped one bit at "); out_hex(va); out(" in "); out(o.name); out("'s read-only page, after its last byte of\n");
			out("  code and constants, so it runs on unaware. The daily round re-reads it within a minute; 'sweep' looks now.\n");
			colour = FG; return;
		}
	}
	out("  "); out(o.name); out(" has no read-only byte beyond its code to flip.\n"); colour = FG;
}

static void show_dinacarya(void)
{
	organ *const all[] = { &tvak, &kosa, &hrdaya, &nadi, &nadi2, &smrti };
	unsigned ro = 0;
	for (organ *o : all) if (o->made) ro += o->ncode;
	colour = SAFFRON; out("  Dinacarya "); colour = FG; out("the daily round, Sakshi's own: every minute it re-reads what must not change\n");
	colour = DIM; out("           "); out_dec(ro); out(" read-only organ pages and Sakshi's own "); out_dec(dina.self_bytes); out(" bytes; ");
	out_dec(dina.rounds); out(" rounds");
	if (dina.rounds && tsc_hz) { out(", last "); out_dec(dina.last_us); out(" us"); }
	out("\n           changes found "); out_dec(dina.found); out(", healed "); out_dec(dina.healed);
	if (dina.rested) { out(", rested "); out_dec(dina.rested); }
	out("; Sakshi's own code "); out(self_words()); out("\n"); colour = FG;
}
