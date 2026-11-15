// SPDX-License-Identifier: AGPL-3.0-or-later
// Turiya - fuzz the outside-content detector and the whole gate with AddressSanitizer and UBSan.
// Input bytes: [state][flags][type\0][target\0][amount\0][approval\0][outside...]
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "kartr_rule.h"
using namespace Turiya;

static const char *take(const uint8_t *&p, const uint8_t *e, char *buf, size_t cap)
{
	size_t n = 0;
	while (p < e && *p && n + 1 < cap) buf[n++] = (char)*p++;
	if (p < e) p++;
	buf[n] = 0;
	return buf;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (size < 2) return 0;
	const uint8_t *p = data + 2, *e = data + size;
	char type[80], target[80], amount[80], appr[80], outside[4096];
	take(p, e, type, sizeof type); take(p, e, target, sizeof target);
	take(p, e, amount, sizeof amount); take(p, e, appr, sizeof appr);
	size_t n = (size_t)(e - p) < sizeof outside - 1 ? (size_t)(e - p) : sizeof outside - 1;
	memcpy(outside, p, n); outside[n] = 0;
	Avastha st = (Avastha)(data[0] % 3);
	u8 key[32]; for (int i = 0; i < 32; i++) key[i] = (u8)i;
	bool trunc = data[1] & 1;
	Verdict v = decide(key, (data[1] & 2) ? 32 : 0, type, target, amount, appr, trunc,
	                   DEFAULT_MANDATE, (data[1] & 4) ? outside : nullptr, st);
	// the same properties the CBMC proof states, checked again on the real detector and the real MAC
	bool allowed = v == Verdict::ALLOWED || v == Verdict::ALLOWED_APPROVED;
	if (st == Avastha::SUSUPTI && allowed) __builtin_trap();
	if (st == Avastha::SVAPNA && allowed && !in_mandate(THINKING, type)) __builtin_trap();
	if (v == Verdict::ALLOWED && (irreversible(type) || ((data[1] & 4) && target_from_outside(target, outside)))) __builtin_trap();
	if (trunc && v != Verdict::HELD_BAD_INPUT) __builtin_trap();
	// P7: a target written as it is after the first line is detected
	char body[200]; size_t tl = strlen(target);
	if (tl && tl < 150 && entity_like(target)) {
		body[0] = 'x'; body[1] = '\n'; memcpy(body + 2, target, tl + 1);
		if (!target_from_outside(target, body)) __builtin_trap();
	}
	return 0;
}
