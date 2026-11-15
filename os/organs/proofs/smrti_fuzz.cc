/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - fuzzing Smrti's reading of file systems (libFuzzer, AddressSanitizer, UndefinedBehaviorSanitizer).
 *
 * A stick's file system is outside content: whoever made the stick chose every byte. This harness gives
 * smrti_fs.h (the very code Smrti runs) an arbitrary disk and checks:
 *   - no read or write outside what it was given: each block it asks for arrives in a fresh allocation of
 *     exactly that many bytes, so ASan catches a read past the block, or through a pointer kept too long;
 *   - no undefined behaviour (overflowing shifts, bad casts ...);
 *   - it never asks for more than one page at a time, and never more than its budget of blocks;
 *   - what it hands back to Sakshi is well formed: counts within their arrays, every name and label ended.
 *
 * Input: 4 bytes of setting (block size, list or read, which path, whether the disk ends where the input
 * ends), then the disk image from block 0; blocks past the input read as zeros.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "smrti_window.h"

static SmrtiWindow Wm;
static SmrtiWindow *const W = &Wm;
static uint8_t *dpage;
#define D ((const volatile uint8_t *)dpage)

static const uint8_t *img;
static size_t img_len;
static unsigned needs;

static bool need(uint64_t lba, uint32_t count)
{
	needs++;
	if (!count || (uint64_t)count * W->block_size > 4096) abort();         /* more than one page at a time */
	if (lba >= W->blocks || count > W->blocks - lba) return false;         /* the device organ refuses */
	free(dpage);
	size_t n = (size_t)count * W->block_size;
	dpage = (uint8_t *)malloc(n);
	for (size_t i = 0; i < n; i++) {
		uint64_t at = lba * W->block_size + i;
		dpage[i] = (lba < (1ULL << 40) && at < img_len) ? img[at] : 0;
	}
	return true;
}

#include "smrti_fs.h"

static const char *const PATHS[] = {
	"", "HELLO.TXT", "DOCS", "DOCS/NOTE.TXT", "Readme.txt", "Docs", "Docs/A long name.txt", "A long name.txt",
	"a/b/c/d/e/f/g/h/i/j", "..", "DOCS/..", "/DOCS//NOTE.TXT/", "DOCS/../DOCS/NOTE.TXT", "HELLO.TXT/x",
};

static void must_end(const char *s, size_t n) { if (!memchr(s, 0, n)) abort(); }

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 4) return 0;
	memset(&Wm, 0, sizeof Wm);
	static const uint32_t BS[4] = { 512, 1024, 2048, 4096 };
	Wm.block_size = BS[data[0] & 3];
	Wm.mode = data[1] & 1;
	const char *p = PATHS[data[2] % (sizeof PATHS / sizeof *PATHS)];
	strncpy(Wm.path, p, sizeof Wm.path - 1);
	img = data + 4; img_len = size - 4;
	Wm.blocks = (data[3] & 0x80) ? (img_len + Wm.block_size - 1) / Wm.block_size : (1ULL << 32);
	needs = 0;
	uint32_t st = serve();
	Wm.status = st;
	if (needs > SMRTI_BUDGET) abort();                                       /* the budget holds */
	if (Wm.count > SMRTI_MAX || Wm.count > Wm.total || Wm.text_len > SMRTI_TEXT) abort();
	if (Wm.mode == 1 && Wm.text_len > Wm.file_size) abort();
	for (unsigned i = 0; i < Wm.count; i++) must_end(Wm.e[i].name, sizeof Wm.e[i].name);
	must_end(Wm.label, sizeof Wm.label);
	must_end(Wm.fs, sizeof Wm.fs);
	free(dpage); dpage = nullptr;
	return 0;
}
