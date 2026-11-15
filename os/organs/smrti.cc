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
static bool exfat;                                                  /* exFAT rather than FAT32 */

static uint32_t fat_next(uint32_t c)
{
	uint64_t off = (uint64_t)c * 4;
	if (!need(fat_lba + off / bs, 1)) return 0x0ffffff7;
	return u32(D + off % bs) & 0x0fffffff;
}

/* ---- mounting: find the FAT32 partition and read its boot sector ---- */
static uint32_t mount(void)
{
	W->label[0] = 0; W->fs[0] = 0;
	bs = W->block_size;
	if (bs < 512 || bs > 4096 || (bs & (bs - 1))) return 1;
	if (!need(0, 1)) return 2;
	part = 0;
	bool mbr = D[510] == 0x55 && D[511] == 0xaa;
	if (mbr && D[450] != 0xee) {
		for (int i = 0; i < 4 && !part; i++) {
			uint8_t t = D[446 + 16 * i + 4];
			if (t == 0x0b || t == 0x0c || t == 0x07) part = u32(D + 446 + 16 * i + 8);
		}
	} else if (mbr) {
		if (!need(1, 1)) return 3;
		static const char sig[] = "EFI PART";
		for (int i = 0; i < 8; i++) if (D[i] != (uint8_t)sig[i]) return 4;
		uint64_t elba = (uint64_t)u32(D + 72) | (uint64_t)u32(D + 76) << 32;
		uint32_t esz = u32(D + 84);
		if (esz < 128 || esz > bs || bs % esz) return 5;
		/* a basic-data partition, or else the EFI system partition (FAT too: the stick Turiya starts from) */
		static const uint8_t basic[16] = { 0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7 };
		static const uint8_t esp[16]   = { 0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b };
		uint64_t esp_at = 0;
		for (uint32_t blk = 0; blk < 4 && !part; blk++) {
			if (!need(elba + blk, 1)) return 6;
			for (uint32_t k = 0; k < bs / esz && !part; k++) {
				const volatile uint8_t *e = D + k * esz;
				bool b = true, x = true;
				for (int j = 0; j < 16; j++) { b &= e[j] == basic[j]; x &= e[j] == esp[j]; }
				uint64_t at = (uint64_t)u32(e + 32) | (uint64_t)u32(e + 36) << 32;
				if (b) part = at;
				else if (x && !esp_at) esp_at = at;
			}
		}
		if (!part) part = esp_at;
	}
	W->part_lba = part;
	if (part >= W->blocks) return 7;
	if (!need(part, 1)) return 8;
	if (D[510] != 0x55 || D[511] != 0xaa) return 9;
	static const char exsig[] = "EXFAT   ";
	exfat = true;
	for (int i = 0; i < 8; i++) exfat &= D[3 + i] == (uint8_t)exsig[i];
	if (exfat) {                                                                  /* exFAT */
		if (D[108] < 9 || D[108] > 12 || (1u << D[108]) != bs || D[109] > 16) return 15;
		spc = 1u << D[109];
		fat_lba = part + u32(D + 80);
		data_lba = part + u32(D + 88);
		total_clusters = u32(D + 92);
		root_cluster = u32(D + 96);
		if (!total_clusters || root_cluster < 2 || root_cluster >= total_clusters + 2) return 16;
		const char fsn[] = "exFAT";
		for (int i = 0; i < 6; i++) W->fs[i] = fsn[i];
		return 0;
	}
	if (u16(D + 11) != bs) return 10;
	const char f32[] = "FAT32";
	(void)f32;
	spc = D[13];
	uint32_t reserved = u16(D + 14), nfats = D[16];
	fat_size = u32(D + 36);
	root_cluster = u32(D + 44);
	uint32_t total = u16(D + 19) ? u16(D + 19) : u32(D + 32);
	if (!spc || (spc & (spc - 1)) || !nfats || nfats > 4 || !fat_size || u16(D + 22) != 0 || root_cluster < 2) return 11;
	for (int i = 0; i < 11; i++) W->label[i] = (D[71 + i] >= 32 && D[71 + i] < 127) ? (char)D[71 + i] : ' ';
	W->label[11] = 0;
	for (int i = 0; i < 8; i++) W->fs[i] = (D[82 + i] >= 32 && D[82 + i] < 127) ? (char)D[82 + i] : ' ';
	W->fs[8] = 0;
	fat_lba = part + reserved;
	data_lba = fat_lba + (uint64_t)nfats * fat_size;
	if (total <= data_lba - part) return 12;
	total_clusters = (uint32_t)((total - (data_lba - part)) / spc);
	if (root_cluster >= total_clusters + 2) return 13;
	return 0;
}

static bool cluster_ok(uint32_t c) { return c >= 2 && c < total_clusters + 2; }

/* ---- walking a folder: fn(name, attr, first cluster, size) for every entry; fn returns false to stop ---- */
struct Found { char name[48]; uint8_t attr; uint32_t cluster, size; bool contig; uint64_t length; };
typedef bool (*Visit)(Found const &f, void *ctx);

/* exFAT: entry sets (file 0x85, stream 0xc0, names 0xc1); a folder may be one contiguous run of clusters */
static uint32_t walk_ex(uint32_t dir, bool contig, uint64_t length, Visit fn, void *ctx)
{
	Found f; unsigned left = 0, nlen = 0, k = 0; bool in_set = false;
	uint64_t csize = (uint64_t)spc * bs, nclus = contig ? (length + csize - 1) / csize : 256;
	uint32_t c = dir;
	for (unsigned hops = 0; hops < 256 && hops < nclus && cluster_ok(c); hops++) {
		uint64_t first = data_lba + (uint64_t)(c - 2) * spc;
		for (uint32_t s = 0; s < spc; s++) {
			if (!need(first + s, 1)) return 14;
			for (uint32_t off = 0; off + 32 <= bs; off += 32) {
				const volatile uint8_t *e = D + off;
				uint8_t t = e[0];
				if (t == 0) return 0;                                              /* the end of the folder */
				if (t == 0x83) {                                                   /* the volume label */
					unsigned n = e[1] < 11 ? e[1] : 11;
					for (unsigned j = 0; j < n; j++) { uint16_t ch = u16(e + 2 + 2 * j); W->label[j] = (ch >= 32 && ch < 127) ? (char)ch : '?'; }
					W->label[n] = 0;
					continue;
				}
				if (t == 0x85) {                                                   /* a file or folder begins */
					memset(&f, 0, sizeof f);
					left = e[1]; f.attr = (u16(e + 4) & 0x10) ? 0x10 : 0x20; in_set = left >= 2; nlen = 0; k = 0;
					continue;
				}
				if (!in_set || t < 0xc0) { in_set = false; continue; }
				if (t == 0xc0) {
					nlen = e[3];
					f.cluster = u32(e + 20);
					f.length = (uint64_t)u32(e + 24) | (uint64_t)u32(e + 28) << 32;
					f.size = f.length > 0xffffffffULL ? 0xffffffffu : (uint32_t)f.length;
					f.contig = e[1] & 2;
				} else if (t == 0xc1) {
					for (unsigned j = 0; j < 15 && k < nlen && k < sizeof f.name - 1; j++, k++) {
						uint16_t ch = u16(e + 2 + 2 * j);
						f.name[k] = (ch >= 32 && ch < 127) ? (char)ch : '?';
					}
				}
				if (--left == 0) {                                                 /* the set is complete */
					in_set = false;
					f.name[k < sizeof f.name ? k : sizeof f.name - 1] = 0;
					if (!fn(f, ctx)) return 0;
				}
			}
		}
		c = contig ? c + 1 : fat_next(c);
	}
	return 0;
}

static uint32_t walk(uint32_t dir, Visit fn, void *ctx)
{
	char lfn[48]; bool have_lfn = false;
	uint32_t c = dir;
	for (unsigned hops = 0; hops < 256 && cluster_ok(c); hops++) {
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
					if (!seq || seq > 3) { have_lfn = false; continue; }           /* names under 40 characters */
					static const uint8_t at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
					for (unsigned j = 0; j < 13; j++) {
						uint16_t ch = u16(e + at[j]);
						if (!ch || ch == 0xffff) break;
						unsigned pos = (seq - 1) * 13 + j;
						if (pos < sizeof lfn - 1) lfn[pos] = (ch >= 32 && ch < 127) ? (char)ch : '?';
					}
					continue;
				}
				if (attr & 0x08) { have_lfn = false; continue; }                   /* the volume label */
				Found f;
				unsigned k = 0;
				if (have_lfn && lfn[0]) { for (unsigned j = 0; j < sizeof lfn - 1 && lfn[j]; j++) f.name[k++] = lfn[j]; }
				else {
					for (int j = 0; j < 8 && e[j] != ' '; j++) f.name[k++] = (e[j] >= 32 && e[j] < 127) ? (char)e[j] : '?';
					if (e[8] != ' ') { f.name[k++] = '.'; for (int j = 8; j < 11 && e[j] != ' '; j++) f.name[k++] = (e[j] >= 32 && e[j] < 127) ? (char)e[j] : '?'; }
				}
				f.name[k] = 0;
				f.attr = attr; f.size = u32(e + 28);
				f.cluster = (uint32_t)u16(e + 20) << 16 | u16(e + 26);
				have_lfn = false;
				bool go_on = fn(f, ctx);
				if (!go_on) return 0;
			}
		}
		c = fat_next(c);
	}
	return 0;
}

static uint32_t walk_any(Found const &dir, Visit fn, void *ctx)
{
	uint32_t c = dir.cluster ? dir.cluster : root_cluster;
	if (exfat) return walk_ex(c, dir.cluster ? dir.contig : false, dir.length, fn, ctx);
	return walk(c, fn, ctx);
}

static bool list_one(Found const &f, void *)
{
	if (f.name[0] == '.' && (!f.name[1] || (f.name[1] == '.' && !f.name[2]))) return true;    /* . and .. */
	W->total++;
	if (W->count < SMRTI_MAX) {
		SmrtiEntry &out = W->e[W->count++];
		unsigned k = 0;
		while (f.name[k] && k < sizeof out.name - 1) { out.name[k] = f.name[k]; k++; }
		out.name[k] = 0;
		out.size = f.size; out.dir = (f.attr & 0x10) ? 1 : 0;
	}
	return true;
}

/* find one name (any letter case) in a folder */
struct Want { const char *name; unsigned len; bool hit; Found f; };
static bool find_one(Found const &f, void *ctx)
{
	Want &w = *(Want *)ctx;
	unsigned k = 0;
	for (; k < w.len && f.name[k]; k++) {
		char a = f.name[k], b = w.name[k];
		if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
		if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
		if (a != b) return true;
	}
	if (k == w.len && !f.name[k]) { w.hit = true; w.f = f; return false; }
	return true;
}

/* follow a path; *out is what it names. 0, or a code */
static uint32_t resolve(const char *path, Found *out)
{
	memset(out, 0, sizeof *out);
	out->attr = 0x10; out->cluster = root_cluster;
	const char *p = path;
	for (unsigned depth = 0; *p && depth < 8; depth++) {
		while (*p == '/') p++;
		if (!*p) break;
		const char *q = p; while (*q && *q != '/') q++;
		if (!(out->attr & 0x10)) return 20;                                        /* a file is not a folder */
		Want w { p, (unsigned)(q - p), false, {} };
		if (uint32_t r = walk_any(*out, find_one, &w)) return r;
		if (!w.hit) return 21;                                                     /* no such name */
		*out = w.f;
		if ((out->attr & 0x10) && !out->cluster) out->cluster = root_cluster;      /* '..' to the top folder */
		p = q;
	}
	return 0;
}

static uint32_t serve(void)
{
	W->count = 0; W->total = 0; W->text_len = 0; W->file_size = 0;
	if (uint32_t r = mount()) return r;
	char path[64]; unsigned n = 0;
	while (n < sizeof path - 1 && W->path[n]) { path[n] = W->path[n]; n++; }
	path[n] = 0;
	Found f;
	if (uint32_t r = resolve(path, &f)) return r;
	if (W->mode == 0) {
		if (!(f.attr & 0x10)) return 22;                                           /* asked to list a file */
		return walk_any(f, list_one, nullptr);
	}
	if (f.attr & 0x10) return 23;                                                  /* asked to read a folder */
	W->file_size = f.size;
	uint32_t c = f.cluster, left = f.size < (uint32_t)SMRTI_TEXT ? f.size : (uint32_t)SMRTI_TEXT;
	for (unsigned hops = 0; left && hops < 16 && cluster_ok(c); hops++) {
		uint64_t first = data_lba + (uint64_t)(c - 2) * spc;
		for (uint32_t s = 0; s < spc && left; s++) {
			if (!need(first + s, 1)) return 14;
			for (uint32_t i = 0; i < bs && left; i++, left--) {
				uint8_t ch = D[i];
				W->text[W->text_len++] = (ch == '\n' || ch == '\t' || (ch >= 32 && ch < 127)) ? (char)ch : '.';
			}
		}
		c = (exfat && f.contig) ? c + 1 : fat_next(c);
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
			W->status = serve();
			W->done_seq = seq;
			say(L_DONE, seq, W->status);
		}
	}
}
