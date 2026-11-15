/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - the window page shared by Sakshi and Nadi (the USB host organ). One 4 KiB page.
 */
#pragma once
#include <stdint.h>

enum : uint64_t {
	NADI_REGS_VA   = 0x7000000,     /* controller registers, up to 64 KiB, uncached */
	NADI_DMA_VA    = 0x7100000,     /* DMA pages, one after another */
	NADI_WINDOW_VA = 0x7200000,     /* this page */
};
enum { NADI_NDMA = 112, NADI_REG_PAGES = 16, NADI_MAXDEV = 8, NADI_DATA = NADI_NDMA - 1 };   /* the last DMA page: data Sakshi reads */

struct NadiDevice
{
	uint8_t port, speed, slot, cls, sub, proto;     /* class/sub/proto of the interesting interface */
	uint16_t vendor, product;
	char what[24];                                  /* "keyboard", "storage", "hub", ... */
};

struct NadiWindow
{
	/* written by Sakshi before Nadi starts */
	uint64_t bar_va;
	uint64_t pa[NADI_NDMA], va[NADI_NDMA];
	uint64_t tsc_hz;
	uint32_t reg_bytes;                              /* how much of the register space is mapped */
	/* written by Nadi */
	uint32_t status, version, max_slots, max_ports, scratchpads, ctx_size;
	uint32_t ports_connected, ndev;
	NadiDevice dev[NADI_MAXDEV];
	uint32_t keyboard_slot;                          /* 0: no keyboard being listened to */
	uint32_t step;                                   /* where init stopped, if it did */
	/* a USB stick (mass storage, bulk-only, SCSI): read-only */
	uint32_t storage_slot, storage_status;           /* 0: none ready */
	char storage_vendor[9], storage_product[17];
	uint64_t storage_blocks;
	uint32_t storage_block_size;
	uint64_t req_lba;                                /* one request at a time, as with Kosa */
	uint32_t req_count, req_seq, done_seq, done_status;
};
static_assert(sizeof(NadiWindow) <= 4096, "the window is one page");
