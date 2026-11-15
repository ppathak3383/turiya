/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Smrti (memory, what is remembered): the organ that reads FAT32 and exFAT file systems.
 *
 * Svadharma, its one duty: find the FAT32 or exFAT partition on a disk or stick, list its folders, read a file's start.
 *
 * Maryada, its limits: 1 fault channel, 2 send-only channel to Sakshi, 3 its notification. A READ-ONLY view of
 * the block device's data page, and a window shared with Sakshi. It cannot reach any device: when it needs a
 * block it asks Sakshi, which asks the device's organ (Nadi or Kosa) and tells Smrti when the block is there.
 * A file system on a stick is outside content: whatever it holds, Smrti can only read it and describe it,
 * and if it is malformed in a way that trips Smrti, Smrti falls and Sakshi heals it.
 */
#include <stdint.h>
#include <sel4/sel4.h>
#include "smrti_window.h"

enum : seL4_CPtr { C_FAULT = 1, C_OUT = 2, C_NTFN = 3 };
enum : seL4_Word { L_READY = 2, L_DONE = 3, L_NEED = 7 };
enum : seL4_Word { N_REQ = 4, N_GIVE = 8, N_FALL = 0x80 };

extern "C" { __attribute__((aligned(16), used)) unsigned char stack[16 * 1024]; }
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

extern "C" void *memset(void *d, int c, unsigned long n)
{ unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

extern "C" void smrti_main(seL4_Word gen, seL4_Word ipc);
extern "C" __attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile ("lea stack+16384(%rip), %rsp\n and $-16, %rsp\n call smrti_main\n 1: jmp 1b\n");
}

static void say(seL4_Word label, seL4_Word a, seL4_Word b)
{
	seL4_Word m0 = a, m1 = b, m2 = 0, m3 = 0;
	seL4_SendWithMRs(C_OUT, seL4_MessageInfo_new(label, 0, 0, 2), &m0, &m1, &m2, &m3);
}

static SmrtiWindow *const W = (SmrtiWindow *)SMRTI_WINDOW_VA;
static const volatile uint8_t *const D = (const volatile uint8_t *)SMRTI_DATA_VA;

/* ask Sakshi for blocks [lba, lba+count) of at most one page; they appear at D */
static bool need(uint64_t lba, uint32_t count)
{
	W->need_lba = lba; W->need_count = count;
	say(L_NEED, (seL4_Word)lba, count);
	seL4_Word bits = 0;
	for (;;) {
		seL4_Wait(C_NTFN, &bits);
		if (bits & N_FALL) *(volatile uint32_t *)0x8000000000UL = 0;    /* the crash test */
		if (bits & N_GIVE) return W->give_ok;
	}
}

#include "smrti_fs.h"                        /* the file-system reading itself: shared with the fuzzer */

extern "C" void smrti_main(seL4_Word gen, seL4_Word ipc)
{
	seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc);
	say(L_READY, gen, 0);
	for (;;) {
		seL4_Word bits = 0;
		seL4_Wait(C_NTFN, &bits);
		if (bits & N_FALL) *(volatile uint32_t *)0x8000000000UL = 0;            /* the crash test */
		if (bits & N_REQ) {
			uint32_t seq = W->req_seq;
			W->status = serve();
			W->done_seq = seq;
			say(L_DONE, seq, W->status);
		}
	}
}
