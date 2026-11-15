/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Kosa (storehouse): the organ that reads the NVMe SSD.
 *
 * Svadharma, its one duty: read blocks from the disk when Sakshi asks, into one shared page.
 *
 * Maryada, its limits:
 *   - READ-ONLY BY CONSTRUCTION. The only I/O command it can build is Read (opcode 0x02), at most one page,
 *     into its data page. On the admin queue it can only Identify (0x06) and create its one I/O queue pair
 *     (0x05, 0x01). One gate (allowed, in kosa_cmd.h) checks every command twice, the second time in place
 *     just before the doorbell; a machine-checked proof shows nothing else can be released. No write,
 *     format, sanitize, firmware or feature command exists anywhere in this program.
 *   - Capabilities: 1 fault channel, 2 send-only channel to Sakshi, 3 its notification. Nothing else.
 *   - Memory: its own pages, the controller's first 16 KB of registers (uncached), six DMA pages and one
 *     window page shared with Sakshi. No interrupts: it polls, with time limits from the processor clock.
 *
 * Cikitsa: if it faults, Sakshi re-makes it from its seed; it then resets the controller and starts again.
 * Known gap: without an IOMMU, a confused controller could DMA anywhere. seL4 has no AMD IOMMU support yet.
 */
#include <stdint.h>
#include <sel4/sel4.h>
#include "kosa_window.h"

enum : seL4_CPtr { C_FAULT = 1, C_OUT = 2, C_NTFN = 3 };
enum : seL4_Word { L_READY = 2, L_DONE = 3 };
enum : seL4_Word { N_REQ = 4, N_FALL = 0x80 };

extern "C" { __attribute__((aligned(16), used)) unsigned char stack[16 * 1024]; }
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

extern "C" void *memset(void *d, int c, unsigned long n)
{ unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

extern "C" void kosa_main(seL4_Word gen, seL4_Word ipc);
extern "C" __attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile ("lea stack+16384(%rip), %rsp\n and $-16, %rsp\n call kosa_main\n 1: jmp 1b\n");
}

static void say(seL4_Word label, seL4_Word a, seL4_Word b)
{
	seL4_Word m0 = a, m1 = b, m2 = 0, m3 = 0;
	seL4_SendWithMRs(C_OUT, seL4_MessageInfo_new(label, 0, 0, 2), &m0, &m1, &m2, &m3);
}

static uint64_t rdtsc(void) { uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

static KosaWindow *const W = (KosaWindow *)KOSA_WINDOW_VA;
static volatile uint8_t *R;                 /* controller registers */

static uint32_t r32(unsigned off) { return *(volatile uint32_t *)(R + off); }
static uint64_t r64(unsigned off) { return (uint64_t)r32(off) | (uint64_t)r32(off + 4) << 32; }
static void w32(unsigned off, uint32_t v) { __asm__ volatile ("sfence" ::: "memory"); *(volatile uint32_t *)(R + off) = v; }
static void w64(unsigned off, uint64_t v) { w32(off, (uint32_t)v); w32(off + 4, (uint32_t)(v >> 32)); }

/* wait until (CSTS & mask) == want, at most 'ms' milliseconds */
static bool wait_csts(uint32_t mask, uint32_t want, uint64_t ms)
{
	uint64_t t0 = rdtsc(), limit = (W->tsc_hz ? W->tsc_hz : 3000000000ULL) / 1000 * ms;
	while ((r32(0x1c) & mask) != want) {
		if (r32(0x1c) & 2) return false;                 /* controller fatal status */
		if (rdtsc() - t0 > limit) return false;
		__asm__ volatile ("pause");
	}
	return true;
}

[[noreturn]] static void fall(void) { *(volatile uint32_t *)0 = 0; __builtin_trap(); }   /* refuse by falling */
static void cpu_relax(void) { __asm__ volatile ("pause"); }

#include "kosa_cmd.h"                       /* the queues, the gate (allowed) and the read: shared with the proof */

static void copy_text(char *d, volatile uint8_t const *s, unsigned n)
{
	unsigned k = 0;
	for (unsigned i = 0; i < n; i++) d[k++] = (s[i] >= 32 && s[i] < 127) ? (char)s[i] : ' ';
	while (k && d[k - 1] == ' ') k--;
	d[k] = 0;
}

static uint32_t init(void)
{
	R = (volatile uint8_t *)W->bar_va;
	uint64_t cap = r64(0x00);
	dstrd = (unsigned)(cap >> 32) & 0xf;
	uint64_t to_ms = ((cap >> 24) & 0xff) * 500 + 500;
	W->version = r32(0x08);
	if (!((cap >> 37) & 1)) return 1;                   /* no NVM command set */
	if ((cap >> 48) & 0xf) return 2;                    /* smallest page larger than 4 KiB */
	if ((cap & 0xffff) + 1 < 2) return 3;

	/* reset: disable, set the admin queues, enable */
	if (r32(0x14) & 1) { w32(0x14, r32(0x14) & ~1u); }
	if (!wait_csts(1, 0, to_ms)) return 4;
	adminq = { (volatile Sqe *)W->va[KOSA_ASQ], (volatile Cqe *)W->va[KOSA_ACQ], 0, 0, 1, 0, 0 };
	ioq    = { (volatile Sqe *)W->va[KOSA_IOSQ], (volatile Cqe *)W->va[KOSA_IOCQ], 0, 0, 1, 1, 0 };
	memset((void *)W->va[KOSA_ASQ], 0, 4096); memset((void *)W->va[KOSA_ACQ], 0, 4096);
	memset((void *)W->va[KOSA_IOSQ], 0, 4096); memset((void *)W->va[KOSA_IOCQ], 0, 4096);
	w32(0x24, (QN - 1) << 16 | (QN - 1));
	w64(0x28, W->pa[KOSA_ASQ]);
	w64(0x30, W->pa[KOSA_ACQ]);
	w32(0x14, 4u << 20 | 6u << 16 | 1u);              /* IOCQES 16 B, IOSQES 64 B, 4 KiB pages, NVM, enable */
	if (!wait_csts(1, 1, to_ms)) return 5;

	volatile uint8_t *id = (volatile uint8_t *)W->va[KOSA_IDENT];
	Sqe e {};
	e.cdw0 = 0x06; e.prp1 = W->pa[KOSA_IDENT]; e.cdw10 = 1;               /* identify controller */
	if (uint32_t st = submit(adminq, e, 0)) return 0x10000 | st;
	copy_text(W->serial, id + 4, 20); copy_text(W->model, id + 24, 40); copy_text(W->firmware, id + 64, 8);
	W->namespaces = *(volatile uint32_t *)(id + 516);
	if (!W->namespaces) return 6;

	e = {}; e.cdw0 = 0x06; e.nsid = 1; e.prp1 = W->pa[KOSA_IDENT]; e.cdw10 = 0;   /* identify namespace 1 */
	if (uint32_t st = submit(adminq, e, 0)) return 0x20000 | st;
	W->blocks = *(volatile uint64_t *)(id + 0);
	unsigned flbas = id[26] & 0xf;
	unsigned lbads = (*(volatile uint32_t *)(id + 128 + 4 * flbas) >> 16) & 0xff;
	if (lbads < 9 || lbads > 12) return 7;              /* blocks of 512 B to 4 KiB only */
	W->block_size = 1u << lbads;

	e = {}; e.cdw0 = 0x05; e.prp1 = W->pa[KOSA_IOCQ]; e.cdw10 = (QN - 1) << 16 | 1; e.cdw11 = 1;          /* I/O CQ 1 */
	if (uint32_t st = submit(adminq, e, 0)) return 0x30000 | st;
	e = {}; e.cdw0 = 0x01; e.prp1 = W->pa[KOSA_IOSQ]; e.cdw10 = (QN - 1) << 16 | 1; e.cdw11 = 1u << 16 | 1;  /* I/O SQ 1 */
	if (uint32_t st = submit(adminq, e, 0)) return 0x40000 | st;
	return 0;
}

extern "C" void kosa_main(seL4_Word gen, seL4_Word ipc)
{
	seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc);
	uint32_t st = init();
	W->status = st;
	say(L_READY, gen, st);
	for (;;) {
		seL4_Word bits = 0;
		seL4_Wait(C_NTFN, &bits);
		if (bits & N_FALL) *(volatile uint32_t *)0x8000000000UL = 0;      /* the crash test */
		if ((bits & N_REQ) && !st) {
			uint32_t seq = W->req_seq;
			uint32_t r = read_blocks(W->req_lba, W->req_count);
			W->done_status = r; W->done_seq = seq;
			say(L_DONE, seq, r);
		}
	}
}
