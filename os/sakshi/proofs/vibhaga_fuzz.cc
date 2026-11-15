/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - fuzzing Vibhaga, Sakshi's partition-table reader (libFuzzer, ASan, UBSan).
 *
 * The table on a stick or a disk is outside content. vibhaga.h, the code Sakshi runs, is given an arbitrary
 * device and checked: no byte outside what was read is ever used (the rest of the page is poisoned after
 * each read, so ASan reports the first look at it), no undefined behaviour, no read past the device's end,
 * never more than a page, a bounded number of reads, and the screen loop always ends.
 *
 * Input: 2 bytes of setting (block size; whether the device ends where the input ends), then the device.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sanitizer/asan_interface.h>

typedef uint32_t u32b;
static const u32b FG = 1, DIM = 2, RED = 3;
static u32b colour;
static unsigned cur_x, chars, reads;
enum { COLS = 80 };

static void out(const char *s)
{
	for (; *s; s++) {
		if (++chars > 100000) abort();                       /* the screen loop must end */
		if (*s == '\n') cur_x = 0;
		else if (++cur_x >= COLS) cur_x = 0;                 /* wraps, as the screen does */
	}
}
static void out_dec(unsigned long v) { char b[24]; int i = 23; b[i] = 0; do { b[--i] = (char)('0' + v % 10); v /= 10; } while (v); out(b + i); }
static void out_hexw(unsigned long v, int w) { char b[17]; b[w] = 0; for (int i = w - 1; i >= 0; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15]; out(b); }
static void out_pad(const char *s, unsigned w) { unsigned n = (unsigned)strlen(s); out(s); while (n++ < w) out(" "); }
static void out_size(uint64_t bytes) { out_dec(bytes / 1000); out(" KB"); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t *p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

#include "vibhaga.h"

static uint8_t page[4096] __attribute__((aligned(64)));
static const uint8_t *img; static size_t img_len;
static uint32_t dev_bs; static uint64_t dev_blocks;

static bool rd(uint64_t lba, uint32_t count)
{
	if (++reads > 64) abort();                               /* a bounded number of reads */
	if (!count || (uint64_t)count * dev_bs > 4096) abort(); /* never more than a page */
	if (lba >= dev_blocks || count > dev_blocks - lba) abort();   /* never past the end: Vibhaga checks first */
	ASAN_UNPOISON_MEMORY_REGION(page, sizeof page);
	size_t n = (size_t)count * dev_bs;
	for (size_t i = 0; i < n; i++) { uint64_t at = lba * dev_bs + i; page[i] = (lba < (1ULL << 40) && at < img_len) ? img[at] : 0; }
	ASAN_POISON_MEMORY_REGION(page + n, sizeof page - n);   /* what was not read cannot be looked at */
	return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 2) return 0;
	static const uint32_t BS[8] = { 512, 1024, 2048, 4096, 520, 4000, 256, 8192 };
	dev_bs = BS[data[0] & 7];
	img = data + 2; img_len = size - 2;
	dev_blocks = (data[1] & 0x80) ? (img_len + dev_bs - 1) / dev_bs + (data[1] & 0x7f) : (1ULL << (32 + (data[1] & 31)));
	cur_x = 0; chars = 0; reads = 0;
	ASAN_POISON_MEMORY_REGION(page, sizeof page);
	list_partitions(rd, page, dev_bs, dev_blocks);
	ASAN_UNPOISON_MEMORY_REGION(page, sizeof page);
	return 0;
}
