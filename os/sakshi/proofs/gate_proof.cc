/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - machine-checked properties of the Kartr gate rule (kartr_rule.h), with CBMC.
 *
 * CBMC explores EVERY input up to the stated sizes: every byte value of type (up to 12 bytes), target (8),
 * amount (4) and approval (65), every state, with and without a key, truncated or not. Two things are left
 * open on purpose: the MAC is an unknown function of the message (the proof does not rely on SHA-256), and
 * the outside-content detector is an unknown predicate (the proof holds whatever it answers; its own memory
 * safety is checked by detector_proof.cc).
 *
 *   P1  an irreversible act is allowed only with the exact approval for that type|target|amount
 *   P1b every act allowed with an approval carried the exact approval
 *   P2  an act on a target planted by outside content is allowed only with the exact approval
 *   P3  deep sleep allows nothing; dreaming allows only thinking (summarise)
 *   P4  truncated input is always held as bad input
 *   P5  allowed WITHOUT approval only if reversible, in the mandate and not planted
 *   P6  no out-of-bounds access, no null dereference (CBMC's own checks), and the unwinding is complete
 *
 * Run (from this folder): cbmc gate_proof.cc -I.. --unwind 70 --unwinding-assertions --bounds-check --pointer-check
 */
#define TURIYA_PROOF 1
#define nullptr 0          /* CBMC's C++ front end predates C++11 */
namespace Turiya {
	void turiya_proof_mac(char const *msg, unsigned long n, unsigned char *out);
	bool turiya_proof_planted(char const *target, char const *outside);
}
#include "kartr_rule.h"
using namespace Turiya;

unsigned char proof_expect[32];        /* the (unknown) MAC of whatever message was asked about */
bool proof_planted;                    /* the (unknown) answer of the outside-content detector */
void Turiya::turiya_proof_mac(char const *, unsigned long, unsigned char *out) { for (int i = 0; i < 32; i++) out[i] = proof_expect[i]; }
bool Turiya::turiya_proof_planted(char const *, char const *) { return proof_planted; }

char nondet_char();
bool nondet_bool();
int nondet_int();
unsigned char nondet_uchar();

static void any_string(char *s, unsigned cap)
{
	for (unsigned i = 0; i + 1 < cap; i++) s[i] = nondet_char();
	s[cap - 1] = 0;
}

int main()
{
	for (int i = 0; i < 32; i++) proof_expect[i] = nondet_uchar();
	proof_planted = nondet_bool();

	char type[13], target[9], amount[5], approval[66];
	any_string(type, sizeof type); any_string(target, sizeof target); any_string(amount, sizeof amount);
	any_string(approval, sizeof approval);
	bool truncated = nondet_bool();
	int st = nondet_int();
	__CPROVER_assume(st >= 0 && st <= 2);
	Avastha state = (Avastha)st;
	u8 key[4] = { 1, 2, 3, 4 };
	usize key_len = nondet_bool() ? 4 : 0;

	Verdict v = decide(key, key_len, type, target, amount, approval, truncated, DEFAULT_MANDATE, "outside", state);
	bool allowed = v == Verdict::ALLOWED || v == Verdict::ALLOWED_APPROVED;

	u8 given[32];
	bool exact = key_len && unhex(approval, given, 32) == 32;
	for (int i = 0; i < 32 && exact; i++) exact = given[i] == proof_expect[i];

	__CPROVER_assert(!(irreversible(type) && allowed) || (v == Verdict::ALLOWED_APPROVED && exact),
	                 "P1 an irreversible act is allowed only with the exact approval");
	__CPROVER_assert(v != Verdict::ALLOWED_APPROVED || exact, "P1b every approval-allowed act carried the exact approval");
	__CPROVER_assert(!(proof_planted && allowed) || (v == Verdict::ALLOWED_APPROVED && exact),
	                 "P2 a planted target is allowed only with the exact approval");
	__CPROVER_assert(!(state == Avastha::SUSUPTI && allowed), "P3a deep sleep allows nothing");
	__CPROVER_assert(!(state == Avastha::SVAPNA && allowed) || in_mandate(THINKING, type), "P3b dreaming allows only thinking");
	__CPROVER_assert(!truncated || v == Verdict::HELD_BAD_INPUT, "P4 truncated input is held as bad input");
	__CPROVER_assert(v != Verdict::ALLOWED || (!irreversible(type) && in_mandate(DEFAULT_MANDATE, type) && !proof_planted),
	                 "P5 allowed without approval only for a reversible, in-mandate, unplanted act");
	return 0;
}
