/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - what a USB device says about itself: Nadi's walk of a configuration descriptor.
 *
 * Outside content: whatever is plugged in chooses every byte. The walk reads only the 'total' bytes it is
 * given, and finds the interface that names the device, a boot keyboard's interrupt IN endpoint, and a
 * stick's pair of bulk endpoints. Shared, unchanged, by nadi.cc and by its fuzzer
 * (os/organs/proofs/nadi_desc_fuzz.cc).
 */
#pragma once

struct DescWalk
{
	bool named; uint8_t cls, sub, proto;                           /* the first interface, or the keyboard's */
	int kbd_if; uint8_t kbd_ep, kbd_int; uint16_t kbd_mps;         /* a boot keyboard's interrupt IN endpoint */
	uint8_t msc_in, msc_out; uint16_t msc_in_mps, msc_out_mps;     /* a stick's bulk IN and OUT endpoints */
	uint8_t msc_burst_in, msc_burst_out;
};

static void walk_config(volatile uint8_t const *b, unsigned total, DescWalk &w)
{
	w.named = false; w.cls = w.sub = w.proto = 0;
	w.kbd_if = -1; w.kbd_ep = 0; w.kbd_int = 10; w.kbd_mps = 8;
	w.msc_in = w.msc_out = 0; w.msc_in_mps = w.msc_out_mps = 512; w.msc_burst_in = w.msc_burst_out = 0;
	bool in_kbd = false, in_msc = false;
	uint8_t *last_burst = nullptr;
	for (unsigned o = 0; o + 2 <= total; ) {
		unsigned len = b[o], type = b[o + 1];
		if (len < 2) break;
		if (type == 4 && o + 9 <= total) {                          /* an interface */
			int ifnum = b[o + 2];
			uint8_t c3 = b[o + 5], s3 = b[o + 6], p3 = b[o + 7];
			in_kbd = c3 == 3 && s3 == 1 && p3 == 1;
			in_msc = c3 == 8 && s3 == 6 && p3 == 0x50;              /* SCSI over bulk-only transport */
			if (!w.named || in_kbd) { w.cls = c3; w.sub = s3; w.proto = p3; w.named = true; }
			if (in_kbd && w.kbd_if < 0) w.kbd_if = ifnum;
		}
		if (type == 5 && o + 7 <= total && in_msc && (b[o + 3] & 3) == 2) {          /* a stick's bulk endpoint */
			uint16_t m = (uint16_t)(b[o + 4] | b[o + 5] << 8);
			if (b[o + 2] & 0x80) { if (!w.msc_in) { w.msc_in = b[o + 2] & 0x0f; w.msc_in_mps = m; last_burst = &w.msc_burst_in; } }
			else if (!w.msc_out) { w.msc_out = b[o + 2] & 0x0f; w.msc_out_mps = m; last_burst = &w.msc_burst_out; }
		} else if (type == 0x30 && last_burst && o + 3 <= total) { *last_burst = b[o + 2]; last_burst = nullptr; }   /* USB 3 companion */
		if (type == 5 && o + 7 <= total && in_kbd && !w.kbd_ep && (b[o + 2] & 0x80) && (b[o + 3] & 3) == 3) {
			w.kbd_ep = b[o + 2] & 0x0f; w.kbd_mps = (uint16_t)(b[o + 4] | b[o + 5] << 8); w.kbd_int = b[o + 6];
		}
		o += len;
	}
}
