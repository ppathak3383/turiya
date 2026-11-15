/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Kosa's one road to the NVMe controller: the queues, the gate every command passes, and the read.
 *
 * Shared, unchanged, by kosa.cc and by its machine-checked proof (os/organs/proofs/kosa_proof.cc), so the
 * proof is about the code that runs. Whoever includes this defines first:
 *   W              the window (KosaWindow *): tsc_hz, block_size, blocks, pa[]
 *   w32(off, v)    a register write; rdtsc(); cpu_relax(); fall() (never returns)
 *
 * The limit, in one place (allowed): a command reaches the controller only if it is
 *   on the I/O queue:   Read (0x02), into the data page, at most one page, nothing fused, PRP1 alone;
 *   on the admin queue: Identify (0x06) into the identify page, or the creation of the one I/O queue pair
 *                       (0x05, 0x01) in their own pages, physically contiguous, no larger than a page.
 * Anything else falls before the doorbell. The proof checks this for every command any caller could build,
 * from every queue state, and that each doorbell releases exactly the one entry just checked.
 */
#pragma once

struct Sqe { uint32_t cdw0, nsid, rsvd2, rsvd3; uint64_t mptr, prp1, prp2; uint32_t cdw10, cdw11, cdw12, cdw13, cdw14, cdw15; };
struct Cqe { uint32_t dw0, dw1; uint16_t sqhd, sqid, cid, status; };
static_assert(sizeof(Sqe) == 64 && sizeof(Cqe) == 16, "NVMe entry sizes");

enum { QN = 64 };                          /* entries per queue: 64 x 64 B is exactly one page */
struct Queue { volatile Sqe *sq; volatile Cqe *cq; unsigned tail, head, phase, qid; uint16_t cid; };
static Queue adminq, ioq;
static unsigned dstrd;                     /* doorbell stride, from the capability register */

static unsigned sq_doorbell(unsigned qid) { return 0x1000 + (2 * qid) * (4u << dstrd); }
static unsigned cq_doorbell(unsigned qid) { return 0x1000 + (2 * qid + 1) * (4u << dstrd); }

/* may this command reach the controller? (qid 0 is the admin queue) */
static bool allowed(unsigned qid, Sqe const &s)
{
	uint8_t opc = (uint8_t)s.cdw0;
	if ((s.cdw0 >> 8) & 0xff) return false;                      /* nothing fused; PRPs, not lists */
	if (s.mptr || s.prp2) return false;                           /* one page, named by PRP1 alone */
	if (qid != 0) {
		uint32_t bs = W->block_size;
		return opc == 0x02 && s.prp1 == W->pa[KOSA_DATA] && (s.cdw12 >> 16) == 0
		    && bs >= 512 && bs <= 4096 && (s.cdw12 & 0xffff) < 4096 / bs;   /* blocks fit the page */
	}
	if (opc == 0x06) return s.prp1 == W->pa[KOSA_IDENT] && (s.cdw10 & 0xff) <= 1;          /* identify */
	if (opc == 0x05) return s.prp1 == W->pa[KOSA_IOCQ] && (s.cdw10 >> 16) < QN && (s.cdw11 & 1);
	if (opc == 0x01) return s.prp1 == W->pa[KOSA_IOSQ] && (s.cdw10 >> 16) < QN && (s.cdw11 & 1);
	return false;
}

static Sqe snapshot(volatile Sqe const *s)
{
	Sqe e;
	e.cdw0 = s->cdw0; e.nsid = s->nsid; e.rsvd2 = s->rsvd2; e.rsvd3 = s->rsvd3;
	e.mptr = s->mptr; e.prp1 = s->prp1; e.prp2 = s->prp2;
	e.cdw10 = s->cdw10; e.cdw11 = s->cdw11; e.cdw12 = s->cdw12; e.cdw13 = s->cdw13; e.cdw14 = s->cdw14; e.cdw15 = s->cdw15;
	return e;
}

/* the only way a command reaches the controller */
static int submit(Queue &q, Sqe const &e, uint32_t *dw0)
{
	if (!allowed(q.qid, e)) fall();                              /* refused by falling, never by sending */
	uint16_t cid = ++q.cid;
	volatile Sqe *s = &q.sq[q.tail];
	s->cdw0 = (e.cdw0 & 0xff) | (uint32_t)cid << 16; s->nsid = e.nsid; s->rsvd2 = 0; s->rsvd3 = 0;
	s->mptr = 0; s->prp1 = e.prp1; s->prp2 = 0;
	s->cdw10 = e.cdw10; s->cdw11 = e.cdw11; s->cdw12 = e.cdw12; s->cdw13 = 0; s->cdw14 = 0; s->cdw15 = 0;
	if (!allowed(q.qid, snapshot(s))) fall();                    /* checked again, in place, before the doorbell */
	q.tail = (q.tail + 1) % QN;
	w32(sq_doorbell(q.qid), q.tail);
	uint64_t t0 = rdtsc(), limit = (W->tsc_hz ? W->tsc_hz : 3000000000ULL) * 2;   /* 2 seconds */
	for (;;) {
		volatile Cqe *c = &q.cq[q.head];
		if ((c->status & 1) == q.phase) {
			uint16_t st = (uint16_t)(c->status >> 1);
			if (dw0) *dw0 = c->dw0;
			q.head = (q.head + 1) % QN;
			if (!q.head) q.phase ^= 1;
			w32(cq_doorbell(q.qid), q.head);
			return st ? 0x1000 | st : 0;
		}
		if (rdtsc() - t0 > limit) return 0x2000;                 /* timed out */
		cpu_relax();
	}
}

/* one read into the data page: never more than the page holds, never past the end of the disk */
static uint32_t read_blocks(uint64_t lba, uint32_t count)
{
	uint32_t bs = W->block_size;
	if (bs < 512 || bs > 4096 || !count || count > 4096 / bs || lba >= W->blocks || count > W->blocks - lba) return 8;
	Sqe e {};
	e.cdw0 = 0x02; e.nsid = 1; e.prp1 = W->pa[KOSA_DATA];
	e.cdw10 = (uint32_t)lba; e.cdw11 = (uint32_t)(lba >> 32); e.cdw12 = count - 1;
	return submit(ioq, e, 0);
}
