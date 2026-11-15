/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Vibhaga (division): reading a partition table, GPT or MBR, from any block device Sakshi can
 * read through an organ (Kosa's SSD, Nadi's stick). The table is outside content: every number in it is
 * checked before it is used, and nothing in it can make Sakshi read past the device or the page.
 *
 * Shared, unchanged, by Sakshi and by its fuzzing harness (os/sakshi/proofs/vibhaga_fuzz.cc). Whoever
 * includes this defines first: out, out_dec, out_pad, out_hexw, out_size, colour, cur_x, FG, DIM, RED,
 * rd32, rd64.
 */
#pragma once

static const char *gpt_type(const uint8_t *g)
{
	static const struct { uint8_t id[16]; const char *name; } T[] = {
		{ { 0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b }, "EFI system" },
		{ { 0x16,0xe3,0xc9,0xe3,0x5c,0x0b,0xb8,0x4d,0x81,0x7d,0xf9,0x2d,0xf0,0x02,0x15,0xae }, "Microsoft reserved" },
		{ { 0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7 }, "basic data (Windows)" },
		{ { 0xa4,0xbb,0x94,0xde,0xd1,0x06,0x40,0x4d,0xa1,0x6a,0xbf,0xd5,0x01,0x79,0xd6,0xac }, "Windows recovery" },
		{ { 0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4 }, "Linux" },
		{ { 0x6d,0xfd,0x57,0x06,0xab,0xa4,0xc4,0x43,0x84,0xe5,0x09,0x33,0xc8,0x4b,0x4f,0x4f }, "Linux swap" },
	};
	for (auto &t : T) {
		unsigned k = 0;
		while (k < 16 && t.id[k] == g[k]) k++;
		if (k == 16) return t.name;
	}
	return "other";
}

/* the partition table of any block device Sakshi can read through an organ: GPT, else MBR */
static void list_partitions(bool (*rd)(uint64_t, uint32_t), const uint8_t *data, uint32_t bs, uint64_t blocks)
{
	if (bs < 512 || bs > 4096 || (bs & (bs - 1))) { colour = RED; out("  an unusual block size\n"); colour = FG; return; }
	if (blocks < 2) { colour = RED; out("  the device is too small for a partition table\n"); colour = FG; return; }
	if (!rd(0, 1)) { colour = RED; out("  block 0 could not be read\n"); colour = FG; return; }
	bool gpt = data[510] == 0x55 && data[511] == 0xaa && data[450] == 0xee;
	if (data[510] == 0x55 && data[511] == 0xaa && !gpt) {
		colour = DIM; out("  partitions (MBR):\n"); colour = FG;
		uint8_t mbr[64]; for (int i = 0; i < 64; i++) mbr[i] = data[446 + i];
		unsigned found = 0;
		for (int k = 0; k < 4; k++) {
			const uint8_t *e = mbr + 16 * k;
			if (!e[4]) continue;
			found++;
			uint64_t n = rd32(e + 12);
			colour = DIM; out("    "); out_dec(k + 1); out("   "); colour = FG;
			const char *t = e[4] == 0x0c || e[4] == 0x0b ? "FAT32" : e[4] == 0x07 ? "NTFS or exFAT" : e[4] == 0x83 ? "Linux"
			              : e[4] == 0x06 || e[4] == 0x0e ? "FAT16" : e[4] == 0xef ? "EFI system" : "other";
			out_pad(t, 30);
			unsigned c0 = cur_x; out_size(n * bs); while (cur_x < c0 + 11) out(" ");
			colour = DIM; out("type "); out_hexw(e[4], 2); out(e[0] == 0x80 ? ", bootable\n" : "\n"); colour = FG;
		}
		if (!found) { colour = DIM; out("    none\n"); colour = FG; }
		return;
	}
	if (!rd(1, 1)) { colour = RED; out("  block 1 could not be read\n"); colour = FG; return; }
	static const char sig[] = "EFI PART";
	for (int i = 0; i < 8; i++) if (data[i] != (uint8_t)sig[i]) { colour = DIM; out("  no partition table\n"); colour = FG; return; }
	uint64_t elba = rd64(data + 72);
	uint32_t n = rd32(data + 80), esz = rd32(data + 84);
	if (esz < 128 || esz > 4096 || (4096 % esz) || !n || elba < 2 || elba >= blocks) { colour = RED; out("  the partition table looks damaged\n"); colour = FG; return; }
	if (n > 128) n = 128;
	unsigned per = 4096 / esz, found = 0;
	colour = DIM; out("  partitions (GPT):\n"); colour = FG;
	for (uint32_t first = 0; first < n; first += per) {
		uint64_t off = (uint64_t)first * esz / bs;                     /* at most 128 x 4096 bytes in */
		uint32_t cnt = 4096 / bs;
		if (off >= blocks - elba || cnt > blocks - elba - off) break;  /* elba < blocks: neither can wrap */
		uint64_t lba = elba + off;
		if (!rd(lba, cnt)) { colour = RED; out("  the partition entries could not be read\n"); colour = FG; return; }
		for (unsigned k = 0; k < per && first + k < n; k++) {
			const uint8_t *e = data + k * esz;
			bool used = false;
			for (int j = 0; j < 16; j++) used |= e[j] != 0;
			if (!used) continue;
			found++;
			uint64_t lo = rd64(e + 32), hi = rd64(e + 40);
			char name[37]; unsigned m = 0;
			for (unsigned j = 0; j < 36; j++) {
				uint16_t ch = (uint16_t)(e[56 + 2 * j] | e[57 + 2 * j] << 8);
				if (!ch) break;
				name[m++] = (ch >= 32 && ch < 127) ? (char)ch : '?';
			}
			name[m] = 0;
			colour = DIM; out("    "); out_dec(first + k + 1); out(first + k + 1 < 10 ? "   " : "  "); colour = FG;
			out_pad(m ? name : "(no name)", 30);
			unsigned long long bytes = (hi >= lo ? hi - lo + 1 : 0) * (unsigned long long)bs;
			unsigned c0 = cur_x; out_size(bytes); while (cur_x < c0 + 11) out(" ");
			colour = DIM; out(gpt_type(e)); out("\n"); colour = FG;
		}
	}
	if (!found) { colour = DIM; out("    none\n"); colour = FG; }
}

