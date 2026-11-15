// prints what Smrti makes of one input (to check the seeds are real file systems)
#include <stdio.h>
#include "smrti_fuzz.cc"
int main(int argc, char **argv)
{
	for (int a = 1; a < argc; a++) {
		FILE *f = fopen(argv[a], "rb"); static uint8_t buf[1 << 20]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
		LLVMFuzzerTestOneInput(buf, n);
		printf("%-28s status %2u fs '%s' label '%s' part %llu total %u", strrchr(argv[a], '/') + 1, Wm.status, Wm.fs, Wm.label, (unsigned long long)Wm.part_lba, Wm.total);
		if (Wm.mode == 0) { printf(" ["); for (unsigned i = 0; i < Wm.count; i++) printf("%s%s%s", i ? ", " : "", Wm.e[i].name, Wm.e[i].dir ? "/" : ""); printf("]\n"); }
		else printf(" size %u text '%.*s'\n", Wm.file_size, (int)(Wm.text_len > 40 ? 40 : Wm.text_len), Wm.text);
	}
}
