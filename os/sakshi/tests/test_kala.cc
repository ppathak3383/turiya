/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Host tests for os/sakshi/kala.h. The reference is independent of the code under test: valid encodings are
 * made by ENCODING every valid value (never by decoding), and dates, leap years and weekdays come from
 * std::chrono's civil calendar.
 *
 *   g++ -std=c++20 -O2 -Wall -Wextra -Werror -Weffc++ -I.. test_kala.cc -o test_kala && ./test_kala
 */
#include "kala.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace Turiya::Kala;
namespace chr = std::chrono;

static int passed, failed;
static void check(bool ok, char const *name)
{
	if (ok) passed++;
	else { failed++; std::printf("FAIL %s\n", name); }
}

static byte enc(int v, bool binary) { return binary ? (byte)v : (byte)(((v / 10) << 4) | (v % 10)); }

/* the reference: for one format, the table byte -> value (-1 = not a valid encoding), built by encoding */
struct Table { int v[256]; };
static Table make_table(int lo, int hi, bool binary)
{
	Table t; for (int i = 0; i < 256; i++) t.v[i] = -1;
	for (int v = lo; v <= hi; v++) t.v[enc(v, binary)] = v;
	return t;
}
static Table hour_table(byte b)
{
	Table t; for (int i = 0; i < 256; i++) t.v[i] = -1;
	bool binary = b & B_BINARY;
	for (int h = 0; h < 24; h++) {
		if (b & B_24H) t.v[enc(h, binary)] = h;
		else {
			int h12 = h % 12 == 0 ? 12 : h % 12;
			t.v[enc(h12, binary) | (h >= 12 ? 0x80 : 0)] = h;
		}
	}
	return t;
}

static Regs regs(byte b, int y, int mo, int d, int h, int mi, int s)
{
	bool binary = b & B_BINARY;
	int h12 = h % 12 == 0 ? 12 : h % 12;
	byte hb = (b & B_24H) ? enc(h, binary) : (byte)(enc(h12, binary) | (h >= 12 ? 0x80 : 0));
	Regs r = { enc(s, binary), enc(mi, binary), hb, enc(d, binary), enc(mo, binary), enc(y - 2000, binary), b,
	           0, false };
	return r;
}

static const byte MODES[4] = { 0x00, B_24H, B_BINARY, B_BINARY | B_24H };

static void test_hours()
{
	int bad = 0, valid = 0;
	for (byte b : MODES) {
		Table t = hour_table(b);
		for (int h = 0; h < 256; h++) {
			if (hour_of((byte)h, b) != t.v[h]) bad++;
			if (t.v[h] >= 0) valid++;
			Regs r = regs(b, 2026, 11, 17, 3, 10, 5); r.hour = (byte)h;
			Time x = decode(r);
			if (x.known != (t.v[h] >= 0) || (x.known && x.hour != t.v[h]) || (!x.known && x.why != BAD_HOUR)) bad++;
		}
	}
	check(bad == 0, "every hour byte in all four formats matches the reference (4 x 256)");
	check(valid == 96, "exactly 24 valid hour bytes per format");
	/* the 12-hour edge cases by name */
	check(hour_of(0x12, 0) == 0 && hour_of(0x92, 0) == 12 && hour_of(0x81, 0) == 13 && hour_of(0x11, 0) == 11,
	      "BCD 12-hour: 12 AM = 0, 12 PM = 12, 1 PM = 13, 11 AM = 11");
	check(hour_of(0x00, 0) == -1 && hour_of(0x80, 0) == -1 && hour_of(0x13, 0) == -1 && hour_of(0x0a, 0) == -1,
	      "BCD 12-hour: 0, PM 0, 13 and a hex digit are not hours");
	check(hour_of(12, B_BINARY) == 0 && hour_of(0x80 | 12, B_BINARY) == 12 && hour_of(0x80 | 11, B_BINARY) == 23,
	      "binary 12-hour: 12 AM, 12 PM, 11 PM");
	check(hour_of(0x80, B_24H) == -1 && hour_of(0x23, B_24H) == 23 && hour_of(0x24, B_24H) == -1 &&
	      hour_of(23, B_24H | B_BINARY) == 23 && hour_of(24, B_24H | B_BINARY) == -1,
	      "24-hour: the PM bit is invalid, 23 is the last hour");
}

static void test_sec_min()
{
	int bad = 0;
	for (byte b : MODES) {
		Table t = make_table(0, 59, b & B_BINARY);
		for (int v = 0; v < 256; v++) {
			Regs r = regs(b, 2026, 11, 17, 3, 10, 5); r.sec = (byte)v;
			Time x = decode(r);
			if (x.known != (t.v[v] >= 0) || (x.known && x.second != t.v[v]) || (!x.known && x.why != BAD_SECOND)) bad++;
			r = regs(b, 2026, 11, 17, 3, 10, 5); r.min = (byte)v;
			x = decode(r);
			if (x.known != (t.v[v] >= 0) || (x.known && x.minute != t.v[v]) || (!x.known && x.why != BAD_MINUTE)) bad++;
		}
	}
	check(bad == 0, "every seconds and minutes byte in all four formats matches the reference (2 x 4 x 256)");
}

/* every (day, month, year) byte triple in both number formats: 2 x 16,777,216 decodes */
static void test_all_dates()
{
	long bad = 0, valid = 0, wday_bad = 0;
	for (int binary = 0; binary < 2; binary++) {
		byte b = (byte)(B_24H | (binary ? B_BINARY : 0));
		Table td = make_table(1, 31, binary), tm = make_table(1, 12, binary), ty = make_table(0, 99, binary);
		for (int y = 0; y < 256; y++) for (int mo = 0; mo < 256; mo++) for (int d = 0; d < 256; d++) {
			Regs r = regs(b, 2026, 11, 17, 3, 10, 5);
			r.year = (byte)y; r.mon = (byte)mo; r.day = (byte)d;
			Time x = decode(r);
			bool ref = false;
			chr::year_month_day ymd{};
			if (ty.v[y] >= 0 && tm.v[mo] >= 0 && td.v[d] >= 0) {
				ymd = chr::year_month_day{ chr::year{ 2000 + ty.v[y] }, chr::month{ (unsigned)tm.v[mo] },
				                           chr::day{ (unsigned)td.v[d] } };
				ref = ymd.ok();
			}
			if (x.known != ref) { bad++; continue; }
			if (!ref) {
				Why want = ty.v[y] < 0 ? BAD_YEAR : tm.v[mo] < 0 ? BAD_MONTH : BAD_DAY;
				if (x.why != want) bad++;
				continue;
			}
			valid++;
			if (x.year != (unsigned)(2000 + ty.v[y]) || x.month != tm.v[mo] || x.day != td.v[d]) bad++;
			if (x.weekday != chr::weekday{ chr::sys_days{ ymd } }.c_encoding()) wday_bad++;
		}
	}
	check(bad == 0, "every day/month/year byte triple, BCD and binary, matches std::chrono (33,554,432)");
	check(valid == 2 * 36525, "exactly 36,525 valid dates per format (2000-2099)");
	check(wday_bad == 0, "the computed weekday matches std::chrono on every valid date");
}

/* every date 2000-2099 x every hour, all four formats: round trip, seconds count, text */
static void test_round_trip()
{
	long bad = 0, n = 0;
	auto epoch = chr::sys_days{ chr::year{ 2000 } / 1 / 1 };
	for (byte b : MODES)
		for (auto d = epoch; d < chr::sys_days{ chr::year{ 2100 } / 1 / 1 }; d += chr::days{ 1 }) {
			chr::year_month_day ymd{ d };
			int y = (int)ymd.year(), mo = (int)(unsigned)ymd.month(), dd = (int)(unsigned)ymd.day();
			for (int h = 0; h < 24; h++) {
				int mi = (dd * 7 + h) % 60, s = (dd * 13 + h * 3) % 60;
				Time x = decode(regs(b, y, mo, dd, h, mi, s));
				n++;
				if (!x.known || (int)x.year != y || x.month != mo || x.day != dd || x.hour != h || x.minute != mi ||
				    x.second != s) { bad++; continue; }
				long long want = (long long)(d - epoch).count() * 86400 + h * 3600 + mi * 60 + s;
				if ((long long)seconds_since_2000(x) != want) bad++;
				if (b == MODES[0] && h == 13) {
					char got[20], ref[32];
					format(x, got);
					std::snprintf(ref, sizeof ref, "%04d-%02d-%02d %02d:%02d:%02d", y, mo, dd, h, mi, s);
					if (std::strcmp(got, ref)) bad++;
				}
			}
		}
	check(bad == 0 && n == 4L * 36525 * 24, "round trip: every date 2000-2099 x every hour x 4 formats (3,506,400)");
}

static void test_flags()
{
	Regs r = regs(B_24H, 2026, 11, 17, 3, 10, 5);
	check(decode(r).known, "a plain BCD 24-hour time is known");
	r.b |= B_SET;
	check(!decode(r).known && decode(r).why == BEING_SET, "status B SET bit: unknown, being set");
	r = regs(B_24H, 2026, 11, 17, 3, 10, 5); r.b |= 0x01 | 0x08 | 0x10 | 0x20 | 0x40;
	check(decode(r).known, "status B interrupt/DST/square-wave bits do not change the reading");
	r = regs(B_24H, 2026, 11, 17, 3, 10, 5); r.has_century = true; r.century = 0x20;
	check(decode(r).known, "BCD century 20 accepted");
	r.century = 0x19;
	check(decode(r).why == BAD_CENTURY, "century 19 is unknown");
	r.century = 0x21;
	check(decode(r).why == BAD_CENTURY, "century 21 is unknown");
	r = regs(B_24H | B_BINARY, 2026, 11, 17, 3, 10, 5); r.has_century = true; r.century = 20;
	check(decode(r).known, "binary century 20 accepted");
	r.century = 0x20;
	check(decode(r).why == BAD_CENTURY, "binary 0x20 (= 32) is not century 20");
	r = regs(B_24H, 2024, 2, 29, 0, 0, 0);
	check(decode(r).known, "29 Feb 2024 is a date");
	r = regs(B_24H, 2025, 2, 28, 0, 0, 0); r.day = 0x29;
	check(decode(r).why == BAD_DAY, "29 Feb 2025 is not a date");
	r = regs(B_24H, 2000, 2, 28, 0, 0, 0); r.day = 0x29;
	check(decode(r).known, "29 Feb 2000 is a date (divisible by 400)");
	r = regs(B_24H, 2026, 11, 17, 3, 10, 5); r.sec = 0x60;
	check(decode(r).why == BAD_SECOND, "a leap second (60) is not taken");
	Time u = unknown(NO_STABLE_READ); char s[20]; format(u, s);
	check(!std::strcmp(s, "unknown"), "an unknown time prints as 'unknown'");
	bool names = true;
	for (int w = 0; w < WHY_COUNT; w++) if (!std::strcmp(why_name((Why)w), "?")) names = false;
	check(names, "every reason has a name");
	Time k = decode(regs(B_24H, 2026, 11, 17, 3, 10, 5));
	check(k.weekday == 2, "17 Nov 2026 is a Tuesday");
}

/* a scripted clock: each snapshot is a list of states; status A is asked first, then the fields */
struct Fake {
	std::vector<Regs> frames {};   /* the clock's state for each snapshot attempt */
	std::vector<int>  uip {};      /* how many times status A says "updating" in each attempt */
	size_t attempt = 0; int a_reads = 0; long reads = 0;
	static constexpr int UIP_TRIES = 1000;   /* read()'s default; an attempt that never clears ends after it */
	byte operator()(byte reg)
	{
		reads++;
		size_t f = attempt < frames.size() ? attempt : frames.size() - 1;
		if (reg == R_A) {
			int busy = attempt < uip.size() ? uip[attempt] : 0;
			if (a_reads < busy) {
				if (++a_reads == UIP_TRIES) { attempt++; a_reads = 0; }
				return A_UIP;
			}
			return 0x26;
		}
		Regs const &r = frames[f];
		switch (reg) {
		case R_SEC: return r.sec; case R_MIN: return r.min; case R_HOUR: return r.hour;
		case R_DAY: return r.day; case R_MON: return r.mon; case R_YEAR: return r.year;
		case R_B: attempt++; a_reads = 0; return r.b;     /* status B is the last read of a snapshot */
		default: return r.century;
		}
	}
};

static void test_read()
{
	Regs t0 = regs(B_24H, 2026, 11, 17, 3, 10, 59), t1 = regs(B_24H, 2026, 11, 17, 3, 11, 0);
	{ Fake f; f.frames = { t0, t0 }; Time x = read(f);
	  check(x.known && x.second == 59 && f.reads == 2 * 8, "two equal snapshots give the time in 16 reads"); }
	{ Fake f; f.frames = { t0, t1, t1 }; Time x = read(f);
	  check(x.known && x.minute == 11 && x.second == 0, "across an update: the later, agreeing pair is used"); }
	{ Fake f; f.frames = { t0, t1, t0, t1, t0, t1, t0, t1 }; Time x = read(f);
	  check(!x.known && x.why == NO_STABLE_READ, "never two equal in a row: unknown"); }
	{ Fake f; f.frames = { t0 }; f.uip = std::vector<int>(8, 5000); Time x = read(f);
	  check(!x.known && x.why == ALWAYS_UPDATING && f.reads == 8 * 1000, "always updating: unknown after 8 x 1000 asks"); }
	{ Fake f; f.frames = { t0, t0 }; f.uip = { 0, 3 }; Time x = read(f);
	  check(x.known, "a short update-in-progress wait is fine"); }
	{ /* snapshot 1 ok, snapshot 2 never clears UIP, snapshot 3 equals snapshot 1: they are not adjacent */
	  Fake f; Regs dummy = t0; f.frames = { t0, dummy, t0, t1, t0, t1, t0, t1 };
	  f.uip = { 0, 5000, 0, 0, 0, 0, 0, 0 };
	  Time x = read(f);
	  check(!x.known, "two equal snapshots with a failed one between them do not count"); }
	{ Fake f; Regs bad = t0; bad.mon = 0x13; f.frames = { bad, bad }; Time x = read(f);
	  check(!x.known && x.why == BAD_MONTH, "a stable but invalid clock is unknown, with its reason"); }
	{ Fake f; Regs c = t0; c.has_century = true; c.century = 0x20; f.frames = { c, c }; Time x = read(f, 0x32);
	  check(x.known && f.reads == 2 * 9, "with a century register: 9 reads per snapshot, known"); }
	{ Fake f; f.frames = { t0, t0 }; Time x = read(f, 0, 1);
	  check(!x.known && x.why == NO_STABLE_READ, "one try can never agree with itself: unknown"); }
}

int main()
{
	test_hours();
	test_sec_min();
	test_all_dates();
	test_round_trip();
	test_flags();
	test_read();
	std::printf("kala: %d passed, %d failed\n", passed, failed);
	return failed ? 1 : 0;
}
