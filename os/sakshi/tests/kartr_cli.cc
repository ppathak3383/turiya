// host CLI: kartr_cli <type> <target> <amount> <outside-file>  -> prints the verdict name
#include <cstdio>
#include <cstring>
#include "kartr_rule.h"
int main(int argc, char **argv)
{
	if (argc < 5) return 2;
	static char outside[16384]; FILE *f = fopen(argv[4], "rb"); if (!f) return 3;
	size_t n = fread(outside, 1, sizeof outside - 1, f); fclose(f); outside[n] = 0;
	Turiya::u8 key[64];
	Turiya::usize klen = Turiya::unhex("6b6b6b6b6b6b6b6b6b6b6b6b6b6b6b6ba1b2c3d4a1b2c3d4a1b2c3d4a1b2c3d4", key, sizeof key);
	bool trunc = strlen(argv[2]) > Turiya::FIELD_MAX;
	Turiya::Verdict v = Turiya::decide(key, klen, argv[1], argv[2], argv[3], "", trunc, Turiya::DEFAULT_MANDATE, outside);
	printf("%s\n", Turiya::verdict_name(v));
	return 0;
}
