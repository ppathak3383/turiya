/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * CBMC proof harness for os/sakshi/kala.h. Every register byte is nondeterministic, so these hold for ALL
 * 2^64 inputs to decode() (no size bound: the input is fixed-size), and for every clock behaviour within the
 * stated number of read tries.
 *   K1 memory safety, no overflow (CBMC checks)
 *   K2 a known time is in range: 2000-2099, month 1-12, day within its month, 0-23 h, 0-59 min, 0-59 s, weekday 0-6
 *   K3 no guessing: re-encoding a known time in the format status B names gives back exactly the bytes read
 *   K4 a known time never comes from a clock being set, nor from a century register other than 20
 *   K5 read(): at most tries * (uip_tries + 8) register reads; a known time is the decode of the last two
 *      completed snapshots, which are byte-for-byte equal
 *   K6 read(): each snapshot's first field is read right after status A said no update is in progress
 *
 *   cbmc kala_proof.cc -I.. -DKALA_DECODE_ONLY --function proof_decode --unwind 101 --unwinding-assertions \
 *        --bounds-check --pointer-check --signed-overflow-check --unsigned-overflow-check --conversion-check
 *   cbmc kala_proof.cc -I.. --function proof_read   --unwind 101 --unwinding-assertions (same checks)
 */
#include "kala.h"

using namespace Turiya::Kala;

extern "C" unsigned char nondet_uchar();
extern "C" bool nondet_bool();

static byte enc(unsigned v, bool binary) { return binary ? (byte)v : (byte)(((v / 10) << 4) | (v % 10)); }

static void check_known(Regs const &r, Time const &t)
{
	if (!t.known) { __CPROVER_assert(t.why != KNOWN && t.why < WHY_COUNT, "K2: unknown has a reason"); return; }
	__CPROVER_assert(t.why == KNOWN, "K2: known has no reason");
	__CPROVER_assert(t.year >= 2000 && t.year <= 2099, "K2: year");
	__CPROVER_assert(t.month >= 1 && t.month <= 12, "K2: month");
	__CPROVER_assert(t.day >= 1 && t.day <= days_in_month(t.year, t.month), "K2: day within its month");
	__CPROVER_assert(!(t.month == 2 && t.day == 29) || leap(t.year), "K2: 29 Feb only in a leap year");
	__CPROVER_assert(t.hour <= 23 && t.minute <= 59 && t.second <= 59, "K2: time of day");
	__CPROVER_assert(t.weekday <= 6, "K2: weekday");

	bool bin = (r.b & B_BINARY) != 0;
	__CPROVER_assert(enc(t.second, bin) == r.sec && enc(t.minute, bin) == r.min, "K3: seconds, minutes");
	__CPROVER_assert(enc(t.day, bin) == r.day && enc(t.month, bin) == r.mon && enc(t.year - 2000, bin) == r.year,
	                 "K3: day, month, year");
	byte hb;
	if (r.b & B_24H) hb = enc(t.hour, bin);
	else {
		unsigned h12 = t.hour % 12 == 0 ? 12 : t.hour % 12;
		hb = (byte)(enc(h12, bin) | (t.hour >= 12 ? 0x80 : 0));
	}
	__CPROVER_assert(hb == r.hour, "K3: hour, 12- or 24-hour");
	__CPROVER_assert(!(r.b & B_SET), "K4: never while being set");
	__CPROVER_assert(!r.has_century || r.century == enc(20, bin), "K4: century is 20");
}

extern "C" void proof_decode()
{
	Regs r;
	r.sec = nondet_uchar(); r.min = nondet_uchar(); r.hour = nondet_uchar(); r.day = nondet_uchar();
	r.mon = nondet_uchar(); r.year = nondet_uchar(); r.b = nondet_uchar(); r.century = nondet_uchar();
	r.has_century = nondet_bool();
	Time t = decode(r);
	check_known(r, t);
	char out[20]; format(t, out);
	__CPROVER_assert(out[t.known ? 19 : 7] == 0, "K1: text ends where it should");
}

#ifndef KALA_DECODE_ONLY
/* the clock: every read returns a fresh nondeterministic byte; the harness keeps what it saw
 * (a plain function, since CBMC's C++ front end does not take member functions in harness structs) */
static unsigned reads, completed;
static bool uip_clear;   /* status A last said: no update in progress */
static Regs cur, last_snap, before_snap;

static byte clock_port(byte reg)
{
	reads++;
	byte v = nondet_uchar();
	if (reg == R_A) uip_clear = !(v & A_UIP);
	else if (reg == R_SEC) {
		__CPROVER_assert(uip_clear, "K6: a snapshot starts only right after status A said no update");
		uip_clear = false;
		cur.sec = v;
	}
	else if (reg == R_MIN) cur.min = v;
	else if (reg == R_HOUR) cur.hour = v;
	else if (reg == R_DAY) cur.day = v;
	else if (reg == R_MON) cur.mon = v;
	else if (reg == R_YEAR) cur.year = v;
	else if (reg == 0x32) cur.century = v;
	else if (reg == R_B) { cur.b = v; before_snap = last_snap; last_snap = cur; completed++; }
	return v;
}

struct Clock { byte operator()(byte reg) { return clock_port(reg); } };

extern "C" void proof_read()
{
	enum { TRIES = 3, UIP = 2 };
	bool century = nondet_bool();
	cur.has_century = century;
	Clock port;
	Time t = read(port, (byte)(century ? 0x32 : 0), TRIES, UIP);
	__CPROVER_assert(reads <= TRIES * (UIP + 8), "K5: bounded reads");
	if (t.why != NO_STABLE_READ && t.why != ALWAYS_UPDATING) {
		__CPROVER_assert(completed >= 2 && same(last_snap, before_snap), "K5: from two equal snapshots");
		check_known(last_snap, t);
		Time again = decode(last_snap);
		__CPROVER_assert(again.known == t.known && again.why == t.why && again.year == t.year &&
		                 again.month == t.month && again.day == t.day && again.hour == t.hour &&
		                 again.minute == t.minute && again.second == t.second, "K5: the answer is that decode");
	}
}
#endif
