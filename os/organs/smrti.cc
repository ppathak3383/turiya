/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Smrti (memory, what is remembered): the organ that reads a FAT32 file system.
 *
 * Svadharma, its one duty: find the FAT32 partition on a disk or stick and list the files in its top folder.
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

static uint16_t u16(const volatile uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t u32(const volatile uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

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

/* one sector's worth of data (block size is 512..4096; FAT sector size must equal it) */
static uint64_t part, fat_lba, data_lba;
static uint32_t bs, spc, root_cluster, fat_size, total_clusters;

static uint32_t fat_next(uint32_t c)
{
	uint64_t off = (uint64_t)c * 4;
	if (!need(fat_lba + off / bs, 1)) return 0x0ffffff7;
	return u32(D + off % bs) & 0x0fffffff;
}

static void add_name_char(char *n, unsigned &k, uint16_t ch)
{
	if (k + 1 >= sizeof(((SmrtiEntry *)0)->name)) return;
	n[k++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
}

static uint32_t list_root(void)
{
	W->count = 0; W->total = 0; W->label[0] = 0; W->fs[0] = 0;
	bs = W->block_size;
	if (bs < 512 || bs > 4096 || (bs & (bs - 1))) return 1;

	/* find a FAT partition: MBR types 0x0b/0x0c (FAT32), or a GPT basic-data partition, or a bare file system */
	if (!need(0, 1)) return 2;
	part = 0;
	bool mbr = D[510] == 0x55 && D[511] == 0xaa;
	if (mbr && D[450] != 0xee) {
		for (int i = 0; i < 4 && !part; i++) {
			uint8_t t = D[446 + 16 * i + 4];
			if (t == 0x0b || t == 0x0c) part = u32(D + 446 + 16 * i + 8);
		}
		if (!part && u16(D + 11) == bs && D[13]) part = 0;                     /* a file system without partitions */
	} else if (mbr) {
		if (!need(1, 1)) return 3;
		static const char sig[] = "EFI PART";
		for (int i = 0; i < 8; i++) if (D[i] != (uint8_t)sig[i]) return 4;
		uint64_t elba = (uint64_t)u32(D + 72) | (uint64_t)u32(D + 76) << 32;
		uint32_t esz = u32(D + 84);
		if (esz < 128 || esz > bs || bs % esz) return 5;
		static const uint8_t basic[16] = { 0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7 };
		for (uint32_t blk = 0; blk < 4 && !part; blk++) {
			if (!need(elba + blk, 1)) return 6;
			for (uint32_t k = 0; k < bs / esz && !part; k++) {
				const volatile uint8_t *e = D + k * esz;
				bool same = true;
				for (int j = 0; j < 16; j++) same &= e[j] == basic[j];
				if (same) part = (uint64_t)u32(e + 32) | (uint64_t)u32(e + 36) << 32;
			}
		}
	}
	W->part_lba = part;
	if (part >= W->blocks) return 7;

	/* the boot sector of the file system */
	if (!need(part, 1)) return 8;
	if (D[510] != 0x55 || D[511] != 0xaa) return 9;
	if (u16(D + 11) != bs) return 10;                       /* sector size must be the device's block size */
	spc = D[13];
	uint32_t reserved = u16(D + 14), nfats = D[16];
	fat_size = u32(D + 36);
	root_cluster = u32(D + 44);
	uint32_t total = u16(D + 19) ? u16(D + 19) : u32(D + 32);
	if (!spc || (spc & (spc - 1)) || !nfats || nfats > 4 || !fat_size || u16(D + 22) != 0 || root_cluster < 2) return 11;   /* FAT32 only */
	for (int i = 0; i < 11; i++) W->label[i] = (D[71 + i] >= 32 && D[71 + i] < 127) ? (char)D[71 + i] : ' ';
	W->label[11] = 0;
	for (int i = 0; i < 8; i++) W->fs[i] = (D[82 + i] >= 32 && D[82 + i] < 127) ? (char)D[82 + i] : ' ';
	W->fs[8] = 0;
	fat_lba = part + reserved;
	data_lba = fat_lba + (uint64_t)nfats * fat_size;
	if (total <= data_lba - part) return 12;
	total_clusters = (uint32_t)((total - (data_lba - part)) / spc);
	if (root_cluster >= total_clusters + 2) return 13;

	/* walk the top folder's clusters, at most 256 of them */
	char lfn[48]; unsigned lfn_k = 0; bool have_lfn = false;
	uint32_t c = root_cluster;
	for (unsigned hops = 0; hops < 256 && c >= 2 && c < total_clusters + 2; hops++) {
		uint64_t first = data_lba + (uint64_t)(c - 2) * spc;
		for (uint32_t s = 0; s < spc; s++) {
			if (!need(first + s, 1)) return 14;
			for (uint32_t off = 0; off + 32 <= bs; off += 32) {
				const volatile uint8_t *e = D + off;
				if (e[0] == 0) return 0;                                           /* the end of the folder */
				if (e[0] == 0xe5) { have_lfn = false; continue; }                  /* deleted */
				uint8_t attr = e[11];
				if (attr == 0x0f) {                                                /* a piece of a long name */
					unsigned seq = e[0] & 0x1f;
					if (e[0] & 0x40) { memset(lfn, 0, sizeof lfn); have_lfn = true; }
					if (!seq || seq > 3) { have_lfn = false; continue; }           /* keep names under 40 characters */
					char part13[13]; unsigned k = 0;
					static const uint8_t at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
					for (int j = 0; j < 13; j++) { uint16_t ch = u16(e + at[j]); if (!ch || ch == 0xffff) break; part13[k++] = (ch >= 32 && ch < 127) ? (char)ch : '?'; }
					for (unsigned j = 0; j < k && (seq - 1) * 13 + j < sizeof lfn - 1; j++) lfn[(seq - 1) * 13 + j] = part13[j];
					lfn_k = 1;
					continue;
				}
				if (attr & 0x08) { have_lfn = false; continue; }                   /* the volume label */
				W->total++;
				if (W->count < SMRTI_MAX) {
					SmrtiEntry &out = W->e[W->count++];
					unsigned k = 0;
					if (have_lfn && lfn_k && lfn[0]) { for (unsigned j = 0; j < sizeof lfn - 1 && lfn[j]; j++) add_name_char(out.name, k, (uint8_t)lfn[j]); }
					else {
						for (int j = 0; j < 8 && e[j] != ' '; j++) add_name_char(out.name, k, e[j]);
						if (e[8] != ' ') { add_name_char(out.name, k, '.'); for (int j = 8; j < 11 && e[j] != ' '; j++) add_name_char(out.name, k, e[j]); }
					}
					out.name[k] = 0;
					out.size = u32(e + 28);
					out.dir = (attr & 0x10) ? 1 : 0;
				}
				have_lfn = false; lfn_k = 0;
			}
		}
		c = fat_next(c);
	}
	return 0;
}

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
			W->status = list_root();
			W->done_seq = seq;
			say(L_DONE, seq, W->status);
		}
	}
}
