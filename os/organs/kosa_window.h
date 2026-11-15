/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - the window page shared by Sakshi and Kosa (the NVMe organ). One 4 KiB page.
 */
#pragma once
#include <stdint.h>

enum : uint64_t {
	KOSA_REGS_VA   = 0x7000000,     /* controller registers, 16 KiB, uncached */
	KOSA_DMA_VA    = 0x7100000,     /* six DMA pages, one after another */
	KOSA_WINDOW_VA = 0x7200000,     /* this page */
};
enum { KOSA_ASQ, KOSA_ACQ, KOSA_IOSQ, KOSA_IOCQ, KOSA_IDENT, KOSA_DATA, KOSA_NDMA };

struct KosaWindow
{
	/* written by Sakshi before Kosa starts */
	uint64_t bar_va;                 /* where Kosa sees the registers */
	uint64_t pa[KOSA_NDMA];          /* physical addresses of the DMA pages */
	uint64_t va[KOSA_NDMA];          /* Kosa's addresses for them */
	uint64_t tsc_hz;                 /* processor clock, for time limits */
	/* written by Kosa */
	char model[41], serial[21], firmware[9];
	uint32_t version, namespaces, block_size, status;
	uint64_t blocks;
	/* one request at a time: Sakshi fills req_*, signals; Kosa answers with done_* and a message */
	uint64_t req_lba;
	uint32_t req_count, req_seq, done_seq, done_status;
};
static_assert(sizeof(KosaWindow) <= 4096, "the window is one page");
