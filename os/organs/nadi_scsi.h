/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - all a USB stick can ever be told: Nadi's one road to a stick (bulk-only transport, SCSI).
 * READ-ONLY BY CONSTRUCTION.
 *
 * Shared, unchanged, by nadi.cc and by its machine-checked proof (os/organs/proofs/nadi_proof.cc). Whoever
 * includes this defines first:
 *   W      the window (NadiWindow *): pa[NADI_DATA], storage_slot, storage_block_size, storage_blocks
 *   buf    the control page (va, pa): the command block goes out from +0x100, the status comes back to +0x200
 *   msc    the stick's two bulk rings and their endpoint numbers; bulk(ring, dci, pa, len); fall()
 *
 * The limit, in one place (scsi): a command block leaves only if it is TEST UNIT READY (0x00), REQUEST SENSE
 * (0x03), INQUIRY (0x12), READ CAPACITY (0x25) or READ (0x28); data only ever comes in, into the data page,
 * never more than a page; and a READ asks for exactly the bytes the page was told to expect. The proof checks
 * this for every command block any caller could build, and every request Sakshi or Smrti could make.
 */
#pragma once

static bool scsi_reads(uint8_t op) { return op == 0x00 || op == 0x03 || op == 0x12 || op == 0x25 || op == 0x28; }

static void le32(volatile uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* is this command block, about to leave, one Nadi may send? (cb: the 16 command bytes; n: their length) */
static bool cbw_allowed(volatile uint8_t const *cb, unsigned n, uint32_t inlen, uint8_t flags)
{
	if (!n || n > 16 || !scsi_reads(cb[0]) || inlen > 4096) return false;
	if (flags != (inlen ? 0x80 : 0x00)) return false;              /* data in, or none: never out */
	if (cb[0] == 0x28) {                                           /* READ(10): exactly what the page expects */
		uint32_t bs = W->storage_block_size;
		uint32_t blocks = (uint32_t)cb[7] << 8 | cb[8];
		if (n != 10 || bs < 512 || bs > 4096 || !blocks || blocks > 4096 / bs || blocks * bs != inlen) return false;
	}
	return true;
}

/* the only way a command reaches the stick; data, if any, comes into the data page */
static int scsi(uint8_t const *cdb, unsigned cdblen, uint32_t inlen)
{
	if (!cbw_allowed(cdb, cdblen, inlen, inlen ? 0x80 : 0x00)) fall();   /* not a read: refuse by falling */
	volatile uint8_t *cbw = buf.va + 0x100, *csw = buf.va + 0x200;
	for (int i = 0; i < 31; i++) cbw[i] = 0;
	le32(cbw, 0x43425355); le32(cbw + 4, ++msc.tag); le32(cbw + 8, inlen);
	cbw[12] = inlen ? 0x80 : 0; cbw[14] = (uint8_t)cdblen;
	for (unsigned i = 0; i < cdblen; i++) cbw[15 + i] = cdb[i];
	uint32_t said = cbw[8] | cbw[9] << 8 | cbw[10] << 16 | (uint32_t)cbw[11] << 24;
	if (said != inlen || !cbw_allowed(cbw + 15, cbw[14], said, cbw[12])) fall();   /* checked again, in place */
	if (bulk(msc.out, msc.out_dci, buf.pa + 0x100, 31)) return 0x10;
	if (inlen && bulk(msc.in, msc.in_dci, W->pa[NADI_DATA], inlen)) return 0x20;
	if (bulk(msc.in, msc.in_dci, buf.pa + 0x200, 13)) return 0x30;
	uint32_t sig = csw[0] | csw[1] << 8 | csw[2] << 16 | (uint32_t)csw[3] << 24;
	uint32_t tag = csw[4] | csw[5] << 8 | csw[6] << 16 | (uint32_t)csw[7] << 24;
	if (sig != 0x53425355 || tag != msc.tag) return 0x40;
	return csw[12];                                                /* 0: passed */
}

/* blocks into the data page: never more than the page holds, never past the end of the stick */
static uint32_t storage_read(uint64_t lba, uint32_t count)
{
	uint32_t bs = W->storage_block_size;
	if (!W->storage_slot || bs < 512 || bs > 4096 || !count || count > 4096 / bs
	    || lba >= W->storage_blocks || count > W->storage_blocks - lba || lba + count - 1 > 0xffffffffULL)
		return 8;
	uint8_t rd[10] = { 0x28, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8), (uint8_t)lba,
	                   0, (uint8_t)(count >> 8), (uint8_t)count, 0 };
	return (uint32_t)scsi(rd, 10, count * bs);
}
