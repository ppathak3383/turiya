/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - a machine-checked proof (CBMC) that a USB stick can only be read through Nadi.
 *
 * It includes nadi_scsi.h, the very code Nadi runs, and stands in for the stick and the controller: every
 * bulk transfer comes here (bulk), any of them may fail, and falling ends the path, as on the machine.
 * At each transfer it checks a statement of the limit written again here, byte by byte:
 *
 *   C  any command block, from any caller:
 *        - exactly one command block goes out per command, the 31 bytes at the control page's +0x100;
 *        - it asks only TEST UNIT READY, REQUEST SENSE, INQUIRY, READ CAPACITY or READ(10);
 *        - its direction is IN (or no data): nothing is ever written to the stick;
 *        - data comes in only to the data page, never more than 4096 bytes, never more than announced;
 *        - a READ(10) announces exactly blocks x block size bytes.
 *   D  any request Sakshi or Smrti could make (any block, any count, any stick geometry):
 *        - what goes out reads exactly the blocks asked for, all inside the stick;
 *        - a reasonable request is still read.
 *
 * Run:   cbmc nadi_proof.cc -I<organs> -DTHEOREM_C --unwind 33 --unwinding-assertions   (and -DTHEOREM_D)
 * No loop is cut. Each also runs with -DVACUITY, which must FAIL.
 */
#include <stdint.h>
#include <assert.h>
#define nullptr 0
#define static_assert(c, m)
#include "nadi_window.h"

uint32_t nondet_u32(void);
uint64_t nondet_u64(void);
uint8_t nondet_u8(void);
int nondet_int(void);

static NadiWindow Wm;
static NadiWindow *const W = &Wm;

struct Page { volatile uint8_t *va; uint64_t pa; };
struct Ring { int which; };
struct Msc { unsigned slot; uint8_t in_dci, out_dci; Ring in, out; uint32_t tag; };
static uint8_t control_page[4096];
static Page buf;
static Msc msc;

static unsigned outs, data_ins, status_ins;
static uint32_t announced;
static bool theorem_d; static uint64_t req_lba; static uint32_t req_count;

static void fall(void) { __CPROVER_assume(0); }          /* the organ stops here: nothing after it happens */

static uint32_t rd_le32(volatile uint8_t const *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* the stick and the controller: each transfer is checked, and may fail */
static int bulk(Ring &r, uint8_t dci, uint64_t pa, uint32_t len)
{
	if (&r == &msc.out) {
		outs++;
		assert(outs == 1);                                   /* one command block per command */
		assert(dci == msc.out_dci && pa == buf.pa + 0x100 && len == 31);
		volatile uint8_t *c = buf.va + 0x100;
		assert(rd_le32(c) == 0x43425355);                    /* a command block wrapper */
		uint32_t dlen = rd_le32(c + 8);
		uint8_t flags = c[12], n = c[14], op = c[15];
		assert(op == 0x00 || op == 0x03 || op == 0x12 || op == 0x25 || op == 0x28);   /* reading commands only */
		assert(n >= 1 && n <= 16);
		assert(c[13] == 0);                                  /* logical unit 0 */
		assert(dlen <= 4096);                                /* no more than the data page */
		assert(flags == (dlen ? 0x80 : 0x00));               /* data comes IN, or there is none: never OUT */
		if (op == 0x28) {
			uint64_t blocks = (uint64_t)c[15 + 7] << 8 | c[15 + 8];
			assert(n == 10 && blocks >= 1);
			assert(blocks * (uint64_t)W->storage_block_size == dlen);   /* announces exactly what it reads */
			if (theorem_d) {
				uint64_t lba = (uint64_t)c[15 + 2] << 24 | (uint64_t)c[15 + 3] << 16 | (uint64_t)c[15 + 4] << 8 | c[15 + 5];
				assert(lba == req_lba && blocks == req_count);              /* exactly the blocks asked for */
				assert(lba < W->storage_blocks && blocks <= W->storage_blocks - lba);   /* inside the stick */
			}
		}
		if (theorem_d) assert(op == 0x28);                   /* a request becomes a READ, nothing else */
		announced = dlen;
	} else if (&r == &msc.in) {
		assert(dci == msc.in_dci);
		assert(outs == 1);                                   /* nothing comes in before a command went out */
		bool data = pa == W->pa[NADI_DATA] && len == announced && len >= 1 && len <= 4096 && data_ins == 0 && status_ins == 0;
		bool status = pa == buf.pa + 0x200 && len == 13 && status_ins == 0;
		assert(data || status);                              /* into the data page, or the 13-byte status */
		if (data) data_ins++; else status_ins++;
	} else {
		assert(0);                                           /* no other ring exists for the stick */
	}
	return nondet_int();                                     /* the stick may fail any transfer */
}

#include "nadi_scsi.h"

static void arbitrary_world(void)
{
	buf.va = control_page; buf.pa = nondet_u64();
	Wm.pa[NADI_DATA] = nondet_u64();
	Wm.storage_slot = nondet_u32(); Wm.storage_block_size = nondet_u32(); Wm.storage_blocks = nondet_u64();
	msc.in_dci = nondet_u8(); msc.out_dci = nondet_u8(); msc.tag = nondet_u32(); msc.slot = nondet_u32();
	msc.in.which = 1; msc.out.which = 2;
}

int main(void)
{
	arbitrary_world();
#ifdef THEOREM_C
	uint8_t cdb[16];                                         /* any command block any caller could build */
	cdb[0] = nondet_u8(); cdb[1] = nondet_u8(); cdb[2] = nondet_u8(); cdb[3] = nondet_u8();
	cdb[4] = nondet_u8(); cdb[5] = nondet_u8(); cdb[6] = nondet_u8(); cdb[7] = nondet_u8();
	cdb[8] = nondet_u8(); cdb[9] = nondet_u8(); cdb[10] = nondet_u8(); cdb[11] = nondet_u8();
	cdb[12] = nondet_u8(); cdb[13] = nondet_u8(); cdb[14] = nondet_u8(); cdb[15] = nondet_u8();
	unsigned cdblen = nondet_u32(); __CPROVER_assume(cdblen <= 16);   /* the callers' arrays are 16 bytes */
	uint32_t inlen = nondet_u32();
	scsi(cdb, cdblen, inlen);
#ifdef VACUITY
	assert(outs == 0);                                       /* must FAIL: command blocks really go out */
#endif
#endif
#ifdef THEOREM_D
	theorem_d = true;
	req_lba = nondet_u64(); req_count = nondet_u32();       /* any request */
	uint32_t r = storage_read(req_lba, req_count);
	if (r == 8) assert(outs == 0);                          /* a refused request sends nothing */
	uint64_t bs = W->storage_block_size;
	bool reasonable = W->storage_slot && bs >= 512 && bs <= 4096 && req_count >= 1 && (uint64_t)req_count * bs <= 4096
	               && req_lba < W->storage_blocks && req_count <= W->storage_blocks - req_lba
	               && req_lba + req_count - 1 <= 0xffffffffULL;
	if (reasonable) assert(outs == 1);                       /* and a reasonable one is read */
#ifdef VACUITY
	assert(!reasonable);                                     /* must FAIL */
#endif
#endif
	return 0;
}
