/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - a machine-checked proof (CBMC) that Kosa can only read, and only into the pages it was given.
 *
 * It includes kosa_cmd.h, the very code Kosa runs, and stands in for the hardware: every register write
 * comes here (w32), time is arbitrary (rdtsc), and falling ends the path, as it does on the machine, where
 * the organ stops and is re-made. At each doorbell it checks a statement of the limit written again here,
 * independently of the code (64-bit arithmetic, field by field):
 *
 *   A  any command, from any caller, from any queue state:
 *        - a doorbell releases exactly one entry: the one just written;
 *        - I/O queue: Read only, into the data page, no more bytes than the page holds, no extra flags;
 *        - admin queue: Identify into the identify page, or the I/O queue pair in its own pages,
 *          physically contiguous and no larger than a page;
 *        - nothing fused, PRP1 alone; no other register is written.
 *   B  any request Sakshi could make (any block, any count, any disk geometry):
 *        - what is released reads exactly the blocks asked for, all inside the disk;
 *        - a reasonable request is still read (the gate is not so tight that Kosa stops reading).
 *
 * Run:   cbmc kosa_proof.cc -I<organs> -DTHEOREM_A --unwind 2   (and -DTHEOREM_B)
 * Each also runs with -DVACUITY, which must FAIL: it proves the checked paths are really reached.
 */
#include <stdint.h>
#include <assert.h>
#define nullptr 0
#define static_assert(c, m)                                    /* checked by the real compiler; CBMC lacks it */
#include "kosa_window.h"

uint32_t nondet_u32(void);
uint64_t nondet_u64(void);
uint16_t nondet_u16(void);

static KosaWindow Wm;
static KosaWindow *const W = &Wm;

struct Queue;
static Queue *cur;
static unsigned tail0, released, other_writes;
static uint64_t req_lba; static uint32_t req_count; static bool theorem_b;

static void w32(unsigned off, uint32_t v);
static uint64_t rdtsc(void) { return nondet_u64(); }
static void cpu_relax(void) {}
static void fall(void) { __CPROVER_assume(0); }          /* the organ stops here: nothing after it happens */

#include "kosa_cmd.h"

/* the limit, stated again: checked on the entry a doorbell releases */
static void check_release(Queue &q, uint32_t v)
{
	released++;
	assert(released == 1);                                  /* one doorbell per command */
	assert(v == (tail0 + 1) % QN);                          /* it releases exactly the entry just written */
	volatile Sqe *s = &q.sq[tail0];
	uint32_t cdw0 = s->cdw0;
	uint8_t opc = (uint8_t)(cdw0 & 0xff);
	assert(((cdw0 >> 8) & 0xff) == 0);                      /* not fused; PRPs, not SGLs */
	assert(s->mptr == 0 && s->prp2 == 0);                   /* PRP1 alone names the memory */
	if (q.qid != 0) {
		assert(opc == 0x02);                                 /* READ: the only I/O command */
		assert(s->prp1 == W->pa[KOSA_DATA]);                 /* into the data page */
		assert((s->cdw12 >> 16) == 0);                       /* no FUA, limited retry, protection or directives */
		uint64_t bytes = ((uint64_t)(s->cdw12 & 0xffff) + 1) * (uint64_t)W->block_size;
		assert(W->block_size >= 512 && bytes <= 4096);       /* no more than the page holds */
		assert(s->cdw13 == 0 && s->cdw14 == 0 && s->cdw15 == 0);
		if (theorem_b) {
			uint64_t slba = (uint64_t)s->cdw10 | (uint64_t)s->cdw11 << 32, nlb = s->cdw12 & 0xffff;
			assert(slba == req_lba && nlb + 1 == req_count);   /* exactly the blocks asked for */
			assert(slba < W->blocks && nlb < W->blocks - slba);  /* all inside the disk */
		}
	} else {
		assert(opc == 0x06 || opc == 0x05 || opc == 0x01);  /* identify; create the I/O CQ; create the I/O SQ */
		if (opc == 0x06) { assert(s->prp1 == W->pa[KOSA_IDENT]); assert((s->cdw10 & 0xff) <= 1); }
		if (opc == 0x05) { assert(s->prp1 == W->pa[KOSA_IOCQ]); assert(s->cdw11 & 1); assert(((uint64_t)(s->cdw10 >> 16) + 1) * 16 <= 4096); }
		if (opc == 0x01) { assert(s->prp1 == W->pa[KOSA_IOSQ]); assert(s->cdw11 & 1); assert(((uint64_t)(s->cdw10 >> 16) + 1) * 64 <= 4096); }
	}
}

static void w32(unsigned off, uint32_t v)
{
	assert(cur != 0);
	if (off == sq_doorbell(cur->qid)) { check_release(*cur, v); return; }
	assert(off == cq_doorbell(cur->qid));                   /* the only other register it may touch */
	other_writes++;
}

static Sqe sqm[QN];
static Cqe cqm[QN];

static void arbitrary_world(void)
{
	Wm.block_size = nondet_u32(); Wm.blocks = nondet_u64(); Wm.tsc_hz = nondet_u64();
	Wm.pa[0] = nondet_u64(); Wm.pa[1] = nondet_u64(); Wm.pa[2] = nondet_u64();
	Wm.pa[3] = nondet_u64(); Wm.pa[4] = nondet_u64(); Wm.pa[5] = nondet_u64();
	dstrd = nondet_u32() & 0xf;
}

static void arbitrary_queue(Queue &q, unsigned qid)
{
	q.sq = sqm; q.cq = cqm;
	q.tail = nondet_u32(); __CPROVER_assume(q.tail < QN);
	q.head = nondet_u32(); __CPROVER_assume(q.head < QN);
	q.phase = nondet_u32() & 1; q.qid = qid; q.cid = nondet_u16();
	cqm[q.head].status = nondet_u16(); cqm[q.head].dw0 = nondet_u32();
	sqm[q.tail].cdw0 = nondet_u32(); sqm[q.tail].prp1 = nondet_u64();    /* whatever was there before */
}

int main(void)
{
	arbitrary_world();
#ifdef THEOREM_A
	Queue q;
	arbitrary_queue(q, nondet_u32());                       /* any queue: admin (0) or any other number */
	Sqe e;                                                  /* any command any caller could build */
	e.cdw0 = nondet_u32(); e.nsid = nondet_u32(); e.rsvd2 = nondet_u32(); e.rsvd3 = nondet_u32();
	e.mptr = nondet_u64(); e.prp1 = nondet_u64(); e.prp2 = nondet_u64();
	e.cdw10 = nondet_u32(); e.cdw11 = nondet_u32(); e.cdw12 = nondet_u32();
	e.cdw13 = nondet_u32(); e.cdw14 = nondet_u32(); e.cdw15 = nondet_u32();
	cur = &q; tail0 = q.tail; released = 0;
	uint32_t dw0 = 0;
	submit(q, e, &dw0);
#ifdef VACUITY
	assert(!(released == 1 && q.qid != 0));                 /* must FAIL: an I/O read really is released */
#endif
#endif
#ifdef THEOREM_B
	arbitrary_queue(ioq, 1);
	theorem_b = true;
	req_lba = nondet_u64(); req_count = nondet_u32();      /* any request */
	cur = &ioq; tail0 = ioq.tail; released = 0;
	uint32_t r = read_blocks(req_lba, req_count);
	if (r == 8) assert(released == 0);                      /* a refused request sends nothing */
	uint64_t bs = W->block_size;
	bool reasonable = bs >= 512 && bs <= 4096 && req_count >= 1 && (uint64_t)req_count * bs <= 4096
	               && req_lba < W->blocks && req_count <= W->blocks - req_lba;
	if (reasonable) assert(released == 1);                  /* and a reasonable one is read */
#ifdef VACUITY
	assert(!reasonable);                                    /* must FAIL: reasonable requests exist */
#endif
#endif
	return 0;
}
