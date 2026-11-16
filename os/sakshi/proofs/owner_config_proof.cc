/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - machine-checked properties of the owner's-file parser (../owner_config.h), with CBMC.
 *
 * CBMC explores EVERY file up to N bytes (every byte value at every position, every length 0..N):
 *
 *   Q1  no out-of-bounds access, no null dereference, no overflow (CBMC's own checks); unwinding complete
 *   Q2  the list has at most MAX names, each entry is the parser's own copy, and it ends with null at count()
 *   Q3  every listed name is 1..NAME bytes, each byte printable ASCII other than '|', or >= 0x80
 *   Q4  no two listed names are the same
 *   Q5  every line is in exactly one count (comment, accepted, duplicate, over_limit, skipped)
 *   Q6  every listed name appears in the file, byte for byte, right after "known:" and blanks
 *
 * Two instances: the small one (1 name of at most 2 bytes, files up to 16 bytes) reaches every limit (a
 * full list, a name one byte too long, a duplicate, a name over the limit) and is checked for Q1-Q5, and for Q6
 * on files up to 12 bytes (check_small_q6, --unwind 18); the real
 * one Sakshi uses (16 names of at most 63 bytes) is set up for Q1-Q5 on files up to 40 bytes, but on 16 Nov it
 * ran out of 7 GB (also at 72 bytes); the small instance covers every limit path, and the fuzzer checks the
 * real one against a reference model.
 *
 * Run (from this folder):
 *   cbmc owner_config_proof.cc -I.. --function check_small --unwind 18 --unwinding-assertions \
 *        --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check
 *   cbmc owner_config_proof.cc -I.. --function check_real --unwind 42 --unwinding-assertions \
 *        --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check
 */
#define nullptr 0          /* CBMC's C++ front end predates C++11 */
#define assert(c) __CPROVER_assert((c), #c)
#include "owner_config.h"
using namespace Turiya;

unsigned char nondet_uchar();
unsigned long nondet_ulong();

static bool name_byte(u8 c) { return (c >= 0x21 && c <= 0x7e && c != '|') || c >= 0x80; }

template <unsigned MAXN, unsigned NM, unsigned N, bool Q6>
static void check()
{
	u8 file[N];
	for (unsigned i = 0; i < N; i++) file[i] = nondet_uchar();
	usize len = nondet_ulong();
	__CPROVER_assume(len <= N);

	Owner_config_t<MAXN, NM> cfg;
	cfg.parse(file, len);
	Owner_config_counts const c = cfg.counts();
	Cstr const *k = cfg.known();

	/* Q2 */
	assert(k != 0 && cfg.count() <= MAXN && k[cfg.count()] == 0);
	for (usize i = 0; i < cfg.count(); i++) assert(k[i] == cfg.name(i) && k[i] != 0);   /* the list is the names */
	/* Q5 */
	assert(c.lines == c.comments + c.accepted + c.duplicates + c.over_limit + c.skipped);
	assert(c.accepted == cfg.count() && c.lines <= len);

	for (usize i = 0; i < cfg.count(); i++) {
		usize n = 0;
		while (n <= NM && cfg.at(i, n)) n++;
		/* Q3 */
		assert(n >= 1 && n <= NM);
		for (usize j = 0; j < n; j++) assert(name_byte((u8)cfg.at(i, j)));
		/* Q4 */
		for (usize j = 0; j < i; j++) {
			bool differ = false;
			for (usize b = 0; b <= n; b++) if (cfg.at(i, b) != cfg.at(j, b)) { differ = true; break; }
			assert(differ);
		}
		/* Q6: somewhere in the file, "known:", blanks, then exactly this name */
		if (!Q6) continue;
		bool found = false;
		for (usize p = 0; p + 6 + n <= len && !found; p++) {
			if (file[p] != 'k' || file[p+1] != 'n' || file[p+2] != 'o' || file[p+3] != 'w' || file[p+4] != 'n'
			    || file[p+5] != ':') continue;
			usize q = p + 6;
			while (q < len && (file[q] == ' ' || file[q] == '\t')) q++;
			if (q + n > len) continue;
			bool same = true;
			for (usize b = 0; b < n; b++) if (file[q + b] != (u8)cfg.at(i, b)) same = false;
			if (same && (q + n == len || !name_byte(file[q + n]))) found = true;
		}
		assert(found);
	}
}

void check_small() { check<1, 2, 16, false>(); }
void check_small_q6() { check<1, 2, 12, true>(); }
void check_real()  { check<16, FIELD_MAX, 40, false>(); }
