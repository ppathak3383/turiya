/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - the window page shared by Sakshi and Smrti (the file reader). One 4 KiB page.
 */
#pragma once
#include <stdint.h>

enum : uint64_t {
	SMRTI_DATA_VA   = 0x7100000,    /* read-only view of the block device's data page (4 KiB) */
	SMRTI_WINDOW_VA = 0x7200000,    /* this page */
};
enum { SMRTI_MAX = 56 };

struct SmrtiEntry { char name[48]; uint32_t size; uint8_t dir, pad[3]; };

struct SmrtiWindow
{
	/* written by Sakshi */
	uint64_t blocks;                 /* the device's size in blocks */
	uint32_t block_size, req_seq;
	uint32_t give_ok;                /* the last block request succeeded */
	/* written by Smrti: a block it needs (it asks with a message, Sakshi fetches, then signals) */
	uint64_t need_lba;
	uint32_t need_count;
	/* the answer */
	uint32_t status, count, total, done_seq;
	uint64_t part_lba;
	char label[12], fs[9];
	SmrtiEntry e[SMRTI_MAX];
};
static_assert(sizeof(SmrtiWindow) <= 4096, "the window is one page");
