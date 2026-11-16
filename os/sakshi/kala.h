/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Kala, the clock. The CMOS real-time clock (MC146818 and its PC descendants) read as data.
 *
 * Pure and freestanding: no library, no allocation, no global state, no I/O of its own. Sakshi passes in what it
 * read from ports 0x70/0x71; this header only decides what those bytes mean. Anything it cannot be sure of
 * becomes "unknown" with a reason, never a guess:
 *   - each field is checked in the format status B names (bit 2: binary or BCD; bit 1: 24- or 12-hour, where
 *     bit 7 of the hour byte is PM). A BCD digit above 9, a value out of range, a 12-hour byte of 0 or above 12,
 *     a PM bit in 24-hour mode, a day past the end of its month (29 February only in leap years) are unknown;
 *   - status B bit 7 (SET: someone is setting the clock, updates are halted) is unknown;
 *   - years are 2000-2099. If the firmware names a century register (FADT), it must say 20, or unknown;
 *   - the weekday register is never used (firmware leaves it wrong); the weekday is computed from the date;
 *   - read(): the clock updates once a second and a read across an update can mix two seconds. A snapshot is
 *     taken only while status A bit 7 (update in progress) is clear, and a time is given only when two
 *     snapshots in a row are byte-for-byte the same. A bounded number of tries; past it, unknown.
 *
 * Written from the MC146818A data sheet and the PC/AT CMOS layout: 0x00 seconds, 0x02 minutes, 0x04 hours,
 * 0x06 weekday, 0x07 day of month, 0x08 month, 0x09 year, 0x0a status A, 0x0b status B.
 */
#ifndef TURIYA_KALA_H
#define TURIYA_KALA_H

namespace Turiya {
namespace Kala {

typedef unsigned char      byte;
typedef unsigned int       uint;

/* CMOS register numbers */
enum Reg : byte { R_SEC = 0x00, R_MIN = 0x02, R_HOUR = 0x04, R_WDAY = 0x06, R_DAY = 0x07, R_MON = 0x08,
                  R_YEAR = 0x09, R_A = 0x0a, R_B = 0x0b };

enum : byte { A_UIP = 0x80, B_SET = 0x80, B_BINARY = 0x04, B_24H = 0x02, HOUR_PM = 0x80 };

/* the raw bytes of one snapshot, as read */
struct Regs {
	byte sec, min, hour, day, mon, year, b;
	byte century;      /* only looked at when has_century */
	bool has_century;
};

enum Why : byte {
	KNOWN = 0,
	BAD_SECOND, BAD_MINUTE, BAD_HOUR, BAD_DAY, BAD_MONTH, BAD_YEAR, BAD_CENTURY,
	BEING_SET,         /* status B bit 7 */
	NO_STABLE_READ,    /* no two snapshots in a row agreed within the tries */
	ALWAYS_UPDATING,   /* status A said "update in progress" every time it was asked */
	WHY_COUNT
};

struct Time {
	bool known;
	Why  why;
	uint year;         /* 2000-2099 */
	byte month;        /* 1-12 */
	byte day;          /* 1-31, within its month */
	byte hour;         /* 0-23 */
	byte minute;       /* 0-59 */
	byte second;       /* 0-59 */
	byte weekday;      /* 0 = Sunday ... 6 = Saturday, computed from the date */
};

inline char const *why_name(Why w)
{
	switch (w) {
	case KNOWN:           return "known";
	case BAD_SECOND:      return "seconds register not valid";
	case BAD_MINUTE:      return "minutes register not valid";
	case BAD_HOUR:        return "hours register not valid";
	case BAD_DAY:         return "day register not valid for its month";
	case BAD_MONTH:       return "month register not valid";
	case BAD_YEAR:        return "year register not valid";
	case BAD_CENTURY:     return "century register is not 20";
	case BEING_SET:       return "the clock is being set";
	case NO_STABLE_READ:  return "no two reads agreed";
	case ALWAYS_UPDATING: return "the clock was always updating";
	case WHY_COUNT:       break;
	}
	return "?";
}

inline bool leap(uint year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

inline byte days_in_month(uint year, uint month)
{
	static const byte d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (month < 1 || month > 12) return 0;
	return (byte)(month == 2 && leap(year) ? 29 : d[month - 1]);
}

/* one field byte -> its value, or -1 when the byte is not a valid encoding in this format */
inline int field(byte v, bool binary)
{
	if (binary) return v;
	uint hi = v >> 4, lo = v & 0x0f;
	if (hi > 9 || lo > 9) return -1;
	return (int)(hi * 10 + lo);
}

/* the hour byte -> 0-23, or -1 */
inline int hour_of(byte h, byte b)
{
	bool binary = (b & B_BINARY) != 0;
	if (b & B_24H) {
		if (h & HOUR_PM) return -1;
		int v = field(h, binary);
		return (v >= 0 && v <= 23) ? v : -1;
	}
	int v = field((byte)(h & 0x7f), binary);
	if (v < 1 || v > 12) return -1;
	if (v == 12) v = 0;                                  /* 12 AM is 0, 12 PM is 12 */
	return (h & HOUR_PM) ? v + 12 : v;
}

/* days since 2000-01-01 (a Saturday); defined for valid dates 2000-2099 */
inline uint days_since_2000(uint year, uint month, uint day)
{
	uint n = 0;
	for (uint y = 2000; y < year; y++) n += leap(y) ? 366u : 365u;
	for (uint m = 1; m < month; m++) n += days_in_month(year, m);
	return n + day - 1;
}

inline Time unknown(Why w)
{
	Time t = { false, w, 0, 0, 0, 0, 0, 0, 0 };
	return t;
}

/* decode one snapshot; fields are checked in a fixed order, so the reason names the first bad field */
inline Time decode(Regs const &r)
{
	if (r.b & B_SET) return unknown(BEING_SET);
	bool binary = (r.b & B_BINARY) != 0;

	int s = field(r.sec, binary);  if (s < 0 || s > 59)  return unknown(BAD_SECOND);
	int m = field(r.min, binary);  if (m < 0 || m > 59)  return unknown(BAD_MINUTE);
	int h = hour_of(r.hour, r.b);  if (h < 0)            return unknown(BAD_HOUR);
	int y = field(r.year, binary); if (y < 0 || y > 99)  return unknown(BAD_YEAR);
	if (r.has_century) {
		int c = field(r.century, binary);
		if (c != 20) return unknown(BAD_CENTURY);
	}
	int mo = field(r.mon, binary); if (mo < 1 || mo > 12) return unknown(BAD_MONTH);
	uint year = 2000u + (uint)y;
	int d = field(r.day, binary);
	if (d < 1 || d > days_in_month(year, (uint)mo)) return unknown(BAD_DAY);

	Time t;
	t.known = true; t.why = KNOWN;
	t.year = year; t.month = (byte)mo; t.day = (byte)d;
	t.hour = (byte)h; t.minute = (byte)m; t.second = (byte)s;
	t.weekday = (byte)((days_since_2000(year, (uint)mo, (uint)d) + 6) % 7);
	return t;
}

/* seconds since 2000-01-01 00:00:00 (the clock's own zone; the RTC says nothing about zones). Known times only. */
inline unsigned long long seconds_since_2000(Time const &t)
{
	return (unsigned long long)days_since_2000(t.year, t.month, t.day) * 86400ull
	     + (unsigned long long)t.hour * 3600ull + (unsigned long long)t.minute * 60ull + t.second;
}

/* "2026-11-17 03:10:05" (19 bytes and a 0) into out[20]; "unknown" when not known */
inline void format(Time const &t, char out[20])
{
	if (!t.known) {
		char const *u = "unknown";
		int i = 0; for (; u[i]; i++) out[i] = u[i];
		out[i] = 0;
		return;
	}
	uint v[6] = { t.year, t.month, t.day, t.hour, t.minute, t.second };
	char sep[6] = { '-', '-', ' ', ':', ':', 0 };
	int o = 0;
	for (int i = 0; i < 6; i++) {
		if (i == 0) { out[o++] = (char)('0' + v[0] / 1000 % 10); out[o++] = (char)('0' + v[0] / 100 % 10); }
		out[o++] = (char)('0' + v[i] / 10 % 10);
		out[o++] = (char)('0' + v[i] % 10);
		if (sep[i]) out[o++] = sep[i];
	}
	out[o] = 0;
}

inline bool same(Regs const &a, Regs const &b)
{
	return a.sec == b.sec && a.min == b.min && a.hour == b.hour && a.day == b.day && a.mon == b.mon
	    && a.year == b.year && a.b == b.b && a.has_century == b.has_century
	    && (!a.has_century || a.century == b.century);
}

/*
 * The read rule. 'port' is anything with 'byte operator()(byte reg)' that reads one CMOS register (Sakshi:
 * outb 0x70, inb 0x71; tests: a scripted fake). century_reg is the FADT's century index, or 0 for none.
 * Each snapshot first waits (at most 'uip_tries' asks) for status A to say no update is in progress, then
 * reads every field. A time is given only when two snapshots in a row agree byte for byte; at most 'tries'
 * snapshots are taken. Every loop is bounded, so a broken or hostile clock costs at most
 * tries * (uip_tries + 8) register reads.
 */
template <typename Port>
inline bool snapshot(Port &port, byte century_reg, uint uip_tries, Regs &r)
{
	uint i = 0;
	while (i < uip_tries && (port(R_A) & A_UIP)) i++;
	if (i == uip_tries) return false;
	r.sec  = port(R_SEC);  r.min = port(R_MIN); r.hour = port(R_HOUR);
	r.day  = port(R_DAY);  r.mon = port(R_MON); r.year = port(R_YEAR);
	r.has_century = century_reg != 0;
	r.century = r.has_century ? port(century_reg) : 0;
	r.b = port(R_B);
	return true;
}

template <typename Port>
inline Time read(Port &port, byte century_reg = 0, uint tries = 8, uint uip_tries = 1000)
{
	Regs prev = { 0, 0, 0, 0, 0, 0, 0, 0, false }, cur = prev;
	bool have_prev = false, ever = false;
	for (uint t = 0; t < tries; t++) {
		if (!snapshot(port, century_reg, uip_tries, cur)) { have_prev = false; continue; }
		ever = true;
		if (have_prev && same(prev, cur)) return decode(cur);
		prev = cur; have_prev = true;
	}
	return unknown(ever ? NO_STABLE_READ : ALWAYS_UPDATING);
}

} /* namespace Kala */
} /* namespace Turiya */

#endif
