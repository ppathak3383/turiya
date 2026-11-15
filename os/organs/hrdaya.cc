/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Hrdaya (heart): the organ that keeps time.
 *
 * Svadharma, its one duty: beat. It runs the timer (PIT, 50 Hz) and on every tick sends one signal into
 * Sakshi's notification. Sakshi counts the ticks into beats and passes pulses on to the other organs.
 *
 * Maryada, its limits: 1 fault channel, 2 send-only channel to Sakshi, 3 its notification, 4 the handler for
 * IRQ 0 (on IOAPIC pin 2), 5 the timer's ports 0x40-0x43, 6 one signal into Sakshi's notification (it can only
 * raise the heart bit). Its own pages, TLS, IPC buffer. Nothing else.
 *
 * Cikitsa: if it faults, Sakshi re-makes it from its seed. If it stops without faulting, Sakshi notices
 * with its own slow clock (the RTC, its breath, prana) and re-makes it too.
 */
#include <stdint.h>
#include <sel4/sel4.h>

enum : seL4_CPtr { C_FAULT = 1, C_OUT = 2, C_NTFN = 3, C_IRQ = 4, C_PORTS = 5, C_BEAT = 6 };
enum : seL4_Word { L_READY = 2 };
enum : seL4_Word { N_IRQ = 1, N_STOP = 0x40, N_FALL = 0x80 };

extern "C" { __attribute__((aligned(16), used)) unsigned char stack[8 * 1024]; }
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

extern "C" void *memset(void *d, int c, unsigned long n)
{ unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

extern "C" void hrdaya_main(seL4_Word gen, seL4_Word ipc);
extern "C" __attribute__((naked, noreturn)) void _start(void)
{
	__asm__ volatile ("lea stack+8192(%rip), %rsp\n and $-16, %rsp\n call hrdaya_main\n 1: jmp 1b\n");
}

static void out8(uint16_t port, uint8_t v) { seL4_X86_IOPort_Out8(C_PORTS, port, v); }

extern "C" void hrdaya_main(seL4_Word gen, seL4_Word ipc)
{
	seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc);
	/* PIT channel 0, rate generator, 50 Hz */
	unsigned div = 1193182 / 50;
	out8(0x43, 0x34);
	out8(0x40, (uint8_t)(div & 0xff));
	out8(0x40, (uint8_t)(div >> 8));
	seL4_IRQHandler_Ack(C_IRQ);
	seL4_Word m0 = gen, m1 = 0, m2 = 0, m3 = 0;
	seL4_SendWithMRs(C_OUT, seL4_MessageInfo_new(L_READY, 0, 0, 2), &m0, &m1, &m2, &m3);
	for (;;) {
		seL4_Word bits = 0;
		seL4_Wait(C_NTFN, &bits);
		if (bits & N_FALL) *(volatile uint32_t *)0x8000000000UL = 0;      /* the crash test */
		if (bits & N_STOP) for (;;) seL4_Wait(C_NTFN, &bits);               /* the stop test: alive, silent */
		if (bits & N_IRQ) { seL4_Signal(C_BEAT); seL4_IRQHandler_Ack(C_IRQ); }
	}
}
