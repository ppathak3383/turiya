/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * libFuzzer harness for os/sakshi/kala.h.
 *   1. decode() on arbitrary register bytes against an independent reference (std::chrono's calendar; valid
 *      encodings made by encoding values, never by decoding), field by field, with the first-bad-field reason.
 *   2. read() against a clock that answers every register read with the next input byte: the number of reads
 *      stays within its bound, and a known time is always the decode of two adjacent, byte-equal snapshots
 *      that each followed a clear "update in progress" bit (re-derived here from the log of reads).
 *
 *   clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -I.. kala_fuzz.cc
 */
#include "kala.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace Turiya::Kala;
namespace chr = std::chrono;

static int ref_field(byte v, bool binary, int lo, int hi)
{
	for (int x = lo; x <= hi; x++) {
		byte e = binary ? (byte)x : (byte)(((x / 10) << 4) | (x % 10));
		if (e == v) return x;
	}
	return -1;
}
static int ref_hour(byte h, byte b)
{
	bool binary = b & B_BINARY;
	for (int x = 0; x < 24; x++) {
		int h12 = x % 12 == 0 ? 12 : x % 12;
		byte e = (b & B_24H) ? (binary ? (byte)x : (byte)(((x / 10) << 4) | (x % 10)))
		                     : (byte)((binary ? h12 : ((h12 / 10) << 4) | (h12 % 10)) | (x >= 12 ? 0x80 : 0));
		if (e == h) return x;
	}
	return -1;
}

static void check_decode(Regs const &r)
{
	Time t = decode(r);
	bool bin = r.b & B_BINARY;
	Why want = KNOWN;
	int s = ref_field(r.sec, bin, 0, 59), m = ref_field(r.min, bin, 0, 59), h = ref_hour(r.hour, r.b);
	int y = ref_field(r.year, bin, 0, 99), mo = ref_field(r.mon, bin, 1, 12), d = ref_field(r.day, bin, 1, 31);
	int c = r.has_century ? ref_field(r.century, bin, 0, 99) : 20;
	if (r.b & 0x80) want = BEING_SET;
	else if (s < 0) want = BAD_SECOND;
	else if (m < 0) want = BAD_MINUTE;
	else if (h < 0) want = BAD_HOUR;
	else if (y < 0) want = BAD_YEAR;
	else if (c != 20) want = BAD_CENTURY;
	else if (mo < 0) want = BAD_MONTH;
	else if (d < 0 || !chr::year_month_day{ chr::year{ 2000 + y }, chr::month{ (unsigned)mo }, chr::day{ (unsigned)d } }.ok())
		want = BAD_DAY;
	if (t.why != want || t.known != (want == KNOWN)) abort();
	if (!t.known) return;
	chr::year_month_day ymd{ chr::year{ 2000 + y }, chr::month{ (unsigned)mo }, chr::day{ (unsigned)d } };
	if ((int)t.year != 2000 + y || t.month != mo || t.day != d || t.hour != h || t.minute != m || t.second != s) abort();
	if (t.weekday != chr::weekday{ chr::sys_days{ ymd } }.c_encoding()) abort();
	auto secs = (chr::sys_days{ ymd } - chr::sys_days{ chr::year{ 2000 } / 1 / 1 }).count() * 86400LL + h * 3600 + m * 60 + s;
	if ((long long)seconds_since_2000(t) != secs) abort();
	char out[20]; format(t, out);
	if (std::strlen(out) != 19) abort();
}

struct Port {
	uint8_t const *data; size_t size, pos;
	std::vector<std::pair<byte, byte>> log;
	byte operator()(byte reg)
	{
		byte v = pos < size ? data[pos] : (byte)(pos * 37);
		pos++;
		log.push_back({ reg, v });
		return v;
	}
};

extern "C" int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
	if (size < 10) return 0;
	Regs r = { data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], (data[8] & 1) != 0 };
	check_decode(r);

	byte century_reg = (data[8] & 1) ? 0x32 : 0;
	unsigned tries = 1 + data[9] % 8, uip_tries = 1 + data[8] % 16;
	Port p{ data + 10, size - 10, 0, {} };
	Time t = read(p, century_reg, tries, uip_tries);
	unsigned per = 7 + (century_reg ? 1u : 0u);
	if (p.log.size() > tries * (uip_tries + per)) abort();

	/* re-derive the snapshots from the log */
	std::vector<std::pair<bool, Regs>> snaps;     /* (complete, regs) per attempt */
	size_t i = 0;
	while (i < p.log.size()) {
		unsigned asks = 0; bool clear = false;
		while (i < p.log.size() && p.log[i].first == R_A && asks < uip_tries) {
			asks++; bool busy = p.log[i].second & A_UIP; i++;
			if (!busy) { clear = true; break; }
		}
		if (!clear) { snaps.push_back({ false, Regs{} }); continue; }
		if (i + per > p.log.size()) abort();
		Regs s{}; byte want[8] = { R_SEC, R_MIN, R_HOUR, R_DAY, R_MON, R_YEAR, 0, R_B };
		if (century_reg) want[6] = century_reg;
		byte *dst[8] = { &s.sec, &s.min, &s.hour, &s.day, &s.mon, &s.year, &s.century, &s.b };
		for (int k = 0; k < 8; k++) {
			if (k == 6 && !century_reg) continue;
			if (p.log[i].first != want[k]) abort();
			*dst[k] = p.log[i].second; i++;
		}
		s.has_century = century_reg != 0;
		snaps.push_back({ true, s });
	}
	bool ended_with_pair = snaps.size() >= 2 && snaps.back().first && snaps[snaps.size() - 2].first &&
	                       same(snaps.back().second, snaps[snaps.size() - 2].second);
	bool any_complete = false;
	for (auto &x : snaps) any_complete |= x.first;
	if (t.why == NO_STABLE_READ || t.why == ALWAYS_UPDATING) {
		if (ended_with_pair) abort();
		if ((t.why == ALWAYS_UPDATING) == any_complete) abort();
		if (snaps.size() != tries) abort();
	} else {
		if (!ended_with_pair) abort();                   /* every other answer comes from an agreeing pair */
		Time again = decode(snaps.back().second);
		if (again.known != t.known || again.why != t.why || again.year != t.year || again.month != t.month ||
		    again.day != t.day || again.hour != t.hour || again.minute != t.minute || again.second != t.second ||
		    again.weekday != t.weekday) abort();
		/* and no earlier adjacent pair agreed (read stops at the first) */
		for (size_t k = 1; k + 1 < snaps.size(); k++)
			if (snaps[k].first && snaps[k - 1].first && same(snaps[k].second, snaps[k - 1].second)) abort();
	}
	return 0;
}
