/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Tvak (touch), the first organ outside Sakshi.
 *
 * Svadharma, its one duty: feel the keyboard and pass each character to Sakshi.
 *
 * Maryada, its limits: a CSpace of its own with exactly five capabilities
 *   1  where its faults are reported (to Sakshi)    2  a send-only channel to Sakshi
 *   3  its own notification (interrupts, pulses)     4  the handler for IRQ 1
 *   5  the keyboard controller's I/O ports 0x60-0x64
 * and a VSpace with only its own pages (code read-only), a TLS page and an IPC buffer.
 * No screen, no memory to allocate, no gate, no other device.
 *
 * Cikitsa, its recovery, is done to it, not by it: if Tvak faults, Sakshi re-makes it from its seed
 * (the SHA-256-checked image) and starts it again. Tvak simply begins afresh and re-arms the controller.
 */
#include <stdint.h>
#include <sel4/sel4.h>
#include "kbd_i8042.h"

enum : seL4_CPtr { C_FAULT = 1, C_OUT = 2, C_NTFN = 3, C_IRQ = 4, C_PORTS = 5 };
enum : seL4_Word { L_KEY = 1, L_READY = 2 };                   /* what Tvak may say */
enum : seL4_Word { N_IRQ = 1, N_PULSE = 2, N_FALL = 0x80 };    /* what wakes it */

extern "C" { __attribute__((aligned(16), used)) unsigned char stack[16 * 1024]; }
__thread seL4_IPCBuffer *__sel4_ipc_buffer;

extern "C" void *memset(void *d, int c, unsigned long n)
{ unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
extern "C" void *memcpy(void *d, const void *s, unsigned long n)
{ unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

extern "C" void tvak_main(seL4_Word gen, seL4_Word ipc);

extern "C" __attribute__((naked, noreturn)) void _start(void)
{
	/* rdi = generation, rsi = IPC buffer address (set by Sakshi); the TLS base is set by Sakshi too */
	__asm__ volatile (
		"lea stack+16384(%rip), %rsp\n"
		"and $-16, %rsp\n"
		"call tvak_main\n"
		"1: jmp 1b\n");
}

static uint8_t in8(uint16_t port)
{
	seL4_X86_IOPort_In8_t r = seL4_X86_IOPort_In8(C_PORTS, port);
	return r.error ? 0xff : r.result;
}
static void out8(uint16_t port, uint8_t v) { seL4_X86_IOPort_Out8(C_PORTS, port, v); }

static void say(seL4_Word label, seL4_Word a, seL4_Word b)
{
	seL4_Word m0 = a, m1 = b, m2 = 0, m3 = 0;
	seL4_SendWithMRs(C_OUT, seL4_MessageInfo_new(label, 0, 0, 2), &m0, &m1, &m2, &m3);
}

extern "C" void tvak_main(seL4_Word gen, seL4_Word ipc)
{
	seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc);
	Kbd::Port port { in8, out8 };
	bool present = Kbd::init(port);
	seL4_IRQHandler_Ack(C_IRQ);
	say(L_READY, gen, present);

	Kbd::Decoder dec;
	unsigned long irqs = 0;
	for (;;) {
		seL4_Word bits = 0;
		seL4_Wait(C_NTFN, &bits);
		if (bits & N_FALL) {
			/* the 'crash tvak' test: write to where Sakshi keeps the screen. Tvak holds no such page,
			 * so seL4 stops it at this instruction and tells Sakshi. */
			*(volatile uint32_t *)0x8000000000UL = 0x00ffffff;
		}
		if (bits & N_IRQ) irqs++;
		if (present) Kbd::drain(port, dec, [&](char c) { say(L_KEY, (unsigned char)c, irqs); });
		if (bits & N_IRQ) seL4_IRQHandler_Ack(C_IRQ);
	}
}
