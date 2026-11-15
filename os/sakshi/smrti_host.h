/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Sakshi's side of Smrti (the FAT32 reader). Smrti never touches a device: it asks Sakshi for blocks,
 * Sakshi asks the stick's Nadi, and Smrti reads them from a read-only view of Nadi's data page.
 * Included after nadi_host.h. Made only when 'files' is typed.
 */

static SmrtiWindow *swin;
static int smrti_k = -1;               /* whose data page Smrti reads */
static uint32_t files_seq;

static bool make_smrti(int k)
{
	if (!make_organ(smrti, 0, NO_IRQ, 0, 140)) return false;
	uptr view = smrti.scratch + 0x800000;
	seL4_CPtr wf = alloc_slot();
	if (!wf || retype(ram_ut, seL4_X86_4K, 0, wf, 1)) return false;
	if (map_in(seL4_CapInitThreadVSpace, wf, view, seL4_ReadWrite, seL4_X86_Default_VMAttributes)
	    || organ_map(smrti, wf, SMRTI_WINDOW_VA, seL4_ReadWrite, seL4_X86_Default_VMAttributes)) return false;
	swin = (SmrtiWindow *)view;
	memset(swin, 0, 4096);
	if (!ndata_caps[k] || organ_map(smrti, ndata_caps[k], SMRTI_DATA_VA, seL4_CanRead, seL4_X86_Default_VMAttributes)) return false;
	if (!remake(smrti) || start(smrti)) return false;
	smrti.made = true; smrti_k = k;
	return true;
}

static void do_files(const char *path, unsigned mode)
{
	if (!wake_nadis()) return;
	stick_k = -1;
	for (unsigned k = 0; k < nxhci && stick_k < 0; k++)
		if (NADIS[k]->awake && nwins[k] && nwins[k]->storage_slot) stick_k = (int)k;
	colour = SAFFRON; out("  Smrti reads the files "); colour = DIM; out("(FAT32; it only reads, and only through Nadi)\n"); colour = FG;
	if (stick_k < 0) { colour = DIM; out("  no USB stick is ready; 'usb' shows what is plugged in\n"); colour = FG; return; }
	if (smrti.made && smrti_k != stick_k) { colour = DIM; out("  the stick moved to the other USB controller; restart to read it\n"); colour = FG; return; }
	if (!smrti.made) {
		if (!make_smrti(stick_k)) { colour = RED; out("  Smrti could not be made: "); out(smrti.step ? smrti.step : "?"); out("\n"); colour = FG; return; }
		seL4_Word gen = 0, st = 0;
		if (!wait_for(smrti, L_READY, 5, &gen, &st)) { colour = RED; out("  no word from Smrti.\n"); colour = FG; return; }
		smrti.awake = true;
	}
	if (!smrti.awake) { colour = DIM; out("  Smrti is not awake.\n"); colour = FG; return; }
	NadiWindow *w = nwins[stick_k];
	swin->blocks = w->storage_blocks; swin->block_size = w->storage_block_size; swin->req_seq = ++files_seq;
	swin->mode = mode;
	{ unsigned n = 0; while (path[n] && n < sizeof swin->path - 1) { swin->path[n] = path[n]; n++; } swin->path[n] = 0; }
	seL4_Signal(smrti.req);
	for (unsigned served = 0; served < 2000; served++) {
		seL4_Word a = 0, b = 0;
		seL4_Word l = wait_any(smrti, 1UL << L_NEED | 1UL << L_DONE, 10, &a, &b);
		if (!l) { colour = RED; out("  Smrti did not finish.\n"); colour = FG; return; }
		if (l == L_DONE) break;
		swin->give_ok = stick_read(swin->need_lba, swin->need_count);      /* Smrti asked for a block */
		seL4_Signal(smrti.give);
	}
	if (swin->status) {
		colour = RED;
		switch (swin->status) {
		case 20: out("  a name in the path is a file, not a folder\n"); break;
		case 21: out("  there is no such name\n"); break;
		case 22: out("  that is a file: type 'read' and its name\n"); break;
		case 23: out("  that is a folder: type 'files' and its name\n"); break;
		default: out("  no FAT32 file system was found (code "); out_dec(swin->status); out(")\n");
		}
		colour = FG; return;
	}
	if (mode == 1) {
		colour = DIM; out("  "); out(path); out(", "); out_size(swin->file_size);
		out(swin->file_size > swin->text_len ? ", its start:\n" : ":\n"); colour = FG;
		out("  ");
		for (unsigned i = 0; i < swin->text_len && i < SMRTI_TEXT; i++) {
			char c = swin->text[i];
			if (c == '\n') out("\n  ");
			else { char t[2] = { (c >= 32 && c < 127) ? c : '.', 0 }; out(t); }
		}
		out("\n");
		return;
	}
	colour = DIM; out("  volume     "); colour = FG; out(swin->label); colour = DIM; out("  "); out(swin->fs); out(" at block "); out_dec(swin->part_lba);
	out(", "); out(path[0] ? path : "top folder"); out(": "); out_dec(swin->total); out(swin->total == 1 ? " entry\n" : " entries\n"); colour = FG;
	for (unsigned i = 0; i < swin->count && i < SMRTI_MAX; i++) {
		SmrtiEntry &e = swin->e[i];
		e.name[sizeof e.name - 1] = 0;                                   /* Smrti's answer is outside content too */
		out("    "); out_pad(e.name, 40);
		if (e.dir) { colour = DIM; out("folder"); colour = FG; }
		else { unsigned c0 = cur_x; out_size(e.size); while (cur_x < c0 + 10) out(" "); }
		out("\n");
	}
	if (swin->total > swin->count) { colour = DIM; out("    ... and "); out_dec(swin->total - swin->count); out(" more\n"); colour = FG; }
}
