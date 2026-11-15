/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - keyboard tissue: the i8042 controller and scan code set 1 (US layout).
 *
 * Shared by Tvak (the organ that owns the keyboard) and by Sakshi's reflex, which runs only if Tvak
 * cannot live. Freestanding; the caller passes its own port functions, so the same code works in
 * whichever compartment holds the ports.
 */
#pragma once
#include <stdint.h>

namespace Kbd {

struct Port { uint8_t (*in)(uint16_t); void (*out)(uint16_t, uint8_t); };

inline bool wait_write(Port const &p) { for (int i = 0; i < 100000; i++) if (!(p.in(0x64) & 2)) return true; return false; }
inline bool wait_read(Port const &p)  { for (int i = 0; i < 100000; i++) if (p.in(0x64) & 1) return true; return false; }

/* flush, turn on the keyboard interrupt and scan-code translation, enable the keyboard.
 * false if there is no controller at all (port 0x64 reads as 0xff) */
inline bool init(Port const &p)
{
	if (p.in(0x64) == 0xff) return false;
	for (int i = 0; i < 64 && (p.in(0x64) & 1); i++) p.in(0x60);
	if (wait_write(p)) {
		p.out(0x64, 0x20);
		if (wait_read(p)) {
			uint8_t cfg = p.in(0x60);
			cfg = (uint8_t)((cfg | 0x01 | 0x40) & ~0x10);
			if (wait_write(p)) { p.out(0x64, 0x60); if (wait_write(p)) p.out(0x60, cfg); }
		}
		if (wait_write(p)) p.out(0x64, 0xae);
	}
	for (int i = 0; i < 64 && (p.in(0x64) & 1); i++) p.in(0x60);
	return true;
}

static const char MAP[2][0x3a] = {
	{ 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, '\t',
	  'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
	  'd','f','g','h','j','k','l',';','\'','`', 0, '\\','z','x','c','v',
	  'b','n','m',',','.','/', 0, '*', 0, ' ' },
	{ 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, '\t',
	  'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
	  'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
	  'B','N','M','<','>','?', 0, '*', 0, ' ' },
};

/* scan codes in, characters out; keeps shift, caps lock and the 0xe0 prefix */
struct Decoder
{
	bool shift = false, caps = false, ext = false;

	char feed(uint8_t sc)
	{
		if (sc == 0xe0) { ext = true; return 0; }
		bool release = sc & 0x80; uint8_t code = sc & 0x7f;
		if (ext) { ext = false; return (code == 0x1c && !release) ? '\n' : 0; }   /* keypad enter */
		if (code == 0x2a || code == 0x36) { shift = !release; return 0; }
		if (release) return 0;
		if (code == 0x3a) { caps = !caps; return 0; }
		if (code >= 0x3a) return 0;
		char c = MAP[shift ? 1 : 0][code];
		if (caps && c >= 'a' && c <= 'z') c = (char)(c - 32);
		else if (caps && c >= 'A' && c <= 'Z') c = (char)(c + 32);
		return c;
	}
};

/* read whatever the controller holds (at most 32 bytes); put(c) for every character */
template <typename PUT>
inline unsigned drain(Port const &p, Decoder &d, PUT put)
{
	unsigned n = 0;
	for (int i = 0; i < 32; i++) {
		uint8_t st = p.in(0x64);
		if (!(st & 1)) break;
		uint8_t b = p.in(0x60); n++;
		if (st & 0x20) continue;            /* touchpad / mouse byte: not ours */
		char c = d.feed(b);
		if (c) put(c);
	}
	return n;
}

} /* namespace Kbd */
