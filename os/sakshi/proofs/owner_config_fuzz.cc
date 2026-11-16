/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - fuzzing the owner's-file parser (../owner_config.h) with libFuzzer, ASan and UBSan.
 *
 * The input is the owner's file, any bytes. The parser Sakshi runs is checked against a second, independent
 * reading of the same rules written with std::string (a reference model), for the real limits (16 names of
 * at most 63 bytes) and for a small instance (3 names of at most 5 bytes, so the limits are hit often):
 * the same names in the same order and the same counts. On top: every name in the list obeys the rules,
 * no two are the same, the list ends with null at count(), and every line is in exactly one count.
 *
 * Build: clang++ -std=c++20 -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
 *          -Wno-unknown-attributes -I.. owner_config_fuzz.cc -o owner_config_fuzz
 */
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "owner_config.h"
using namespace Turiya;

#define CHECK(c) do { if (!(c)) { __builtin_trap(); } } while (0)

struct Model { std::vector<std::string> names; usize lines = 0, com = 0, acc = 0, dup = 0, over = 0, skip = 0; };

static Model model(std::string const &f, usize max_names, usize name_max)
{
	Model m;
	std::vector<std::string> lines;
	usize a = 0;
	for (usize b; (b = f.find('\n', a)) != std::string::npos; a = b + 1) lines.push_back(f.substr(a, b - a));
	if (a < f.size()) lines.push_back(f.substr(a));
	for (std::string l : lines) {
		m.lines++;
		if (!l.empty() && l.back() == '\r') l.pop_back();
		usize const first = l.find_first_not_of(" \t");
		if (first == std::string::npos || l[first] == '#') { m.com++; continue; }
		if (l.compare(first, 6, "known:") != 0) { m.skip++; continue; }
		std::string rest = l.substr(first + 6);
		usize const s = rest.find_first_not_of(" \t"), e = rest.find_last_not_of(" \t");
		if (s == std::string::npos) { m.skip++; continue; }
		std::string name = rest.substr(s, e - s + 1);
		bool good = name.size() <= name_max;
		for (unsigned char c : name) if (!((c > 0x20 && c < 0x7f && c != '|') || c >= 0x80)) good = false;
		if (!good) { m.skip++; continue; }
		bool seen = false;
		for (auto const &n : m.names) seen |= n == name;
		if (seen) { m.dup++; continue; }
		if (m.names.size() == max_names) { m.over++; continue; }
		m.names.push_back(name); m.acc++;
	}
	return m;
}

template <typename CFG>
static void check(CFG &cfg, uint8_t const *data, size_t size)
{
	cfg.parse(data, size);
	Model const m = model(std::string((char const *)data, size), CFG::MAX, CFG::NAME);
	Owner_config_counts const c = cfg.counts();

	CHECK(c.lines == m.lines && c.comments == m.com && c.accepted == m.acc
	      && c.duplicates == m.dup && c.over_limit == m.over && c.skipped == m.skip);
	CHECK(c.lines == c.comments + c.accepted + c.duplicates + c.over_limit + c.skipped);
	CHECK(cfg.count() == m.names.size() && cfg.count() == c.accepted && cfg.count() <= (usize)CFG::MAX);

	Cstr const *k = cfg.known();
	CHECK(k != nullptr && k[cfg.count()] == nullptr && cfg.name(cfg.count()) == nullptr);
	for (usize i = 0; i < cfg.count(); i++) {
		usize const n = std::strlen(k[i]);
		CHECK(k[i] == cfg.name(i) && n >= 1 && n <= (usize)CFG::NAME && m.names[i] == k[i]);
		for (usize j = 0; j < n; j++) {
			unsigned char const b = (unsigned char)k[i][j];
			CHECK((b > 0x20 && b < 0x7f && b != '|') || b >= 0x80);
		}
		for (usize j = 0; j < i; j++) CHECK(std::strcmp(k[i], k[j]) != 0);
	}
}

static Owner_config big;
static Owner_config_t<3, 5> small;

extern "C" int LLVMFuzzerTestOneInput(uint8_t const *data, size_t size)
{
	/* the parser is given exactly 'size' bytes in its own allocation, so ASan sees any read past the end */
	uint8_t *copy = (uint8_t *)std::malloc(size ? size : 1);
	if (size) std::memcpy(copy, data, size);
	check(big, copy, size);
	check(small, copy, size);
	std::free(copy);
	return 0;
}
