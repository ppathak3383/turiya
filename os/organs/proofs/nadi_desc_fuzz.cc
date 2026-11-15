/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - fuzzing Nadi's walk of what a USB device says about itself (libFuzzer, ASan, UBSan).
 *
 * Whatever is plugged in chooses every byte of its configuration descriptor. nadi_desc.h, the code Nadi
 * runs, is given arbitrary bytes in an allocation of exactly their length (so ASan reports the first byte
 * read past them), and its findings are checked: endpoint numbers 1-15 (or none), the keyboard and the
 * stick found only from interfaces that say they are one, and the walk always ends.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "nadi_desc.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	unsigned total = size > 1024 ? 1024 : (unsigned)size;           /* Nadi reads at most 1024 bytes */
	uint8_t *b = (uint8_t *)malloc(total ? total : 1);
	memcpy(b, data, total);
	DescWalk w;
	walk_config(b, total, w);
	if (w.kbd_ep > 15 || w.msc_in > 15 || w.msc_out > 15) abort();
	if (w.kbd_if < -1 || w.kbd_if > 255) abort();
	if (w.kbd_ep && w.kbd_if < 0) abort();                         /* an endpoint only inside a keyboard interface */
	if (!w.named && (w.cls || w.sub || w.proto)) abort();
	bool any_msc = false;                                          /* a stick's endpoints only under a bulk-only SCSI interface */
	for (unsigned o = 0; o + 9 <= total; o++)
		if (b[o + 1] == 4 && b[o + 5] == 8 && b[o + 6] == 6 && b[o + 7] == 0x50) any_msc = true;
	if ((w.msc_in || w.msc_out) && !any_msc) abort();
	free(b);
	return 0;
}
