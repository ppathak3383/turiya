// SPDX-License-Identifier: AGPL-3.0-or-later
// Host test of the exact rule code the kartr_gate component runs (kartr_rule.h).
#include <cstdio>
#include <cstring>
#include "kartr_rule.h"
#include "vectors.h"
using namespace Turiya;

static int pass = 0, fail = 0;
static u8 key[64]; static usize key_len;

static void check(char const *name, Verdict got, Verdict want)
{
	bool ok = got == want;
	(ok ? pass : fail)++;
	std::printf("%s %-46s %s\n", ok ? "PASS" : "FAIL", name, verdict_name(got));
}
static Verdict d(char const *t, char const *tg, char const *am, char const *ap, usize kl = 0xffff)
{ return decide(key, kl == 0xffff ? key_len : kl, t, tg, am, ap, false); }

int main()
{
	key_len = unhex(KEY_HEX, key, sizeof key);
	{ Sha256 s; u8 o[32]; s.add((u8 const *)"abc", 3); s.finish(o);
	  bool ok = o[0] == 0xba && o[1] == 0x78 && o[31] == 0xad; (ok ? pass : fail)++;
	  std::printf("%s %-46s\n", ok ? "PASS" : "FAIL", "sha256(abc) known vector"); }

	/* the original 8 (kartr_gate 13 Nov) */
	check("summarise allowed",                         d("summarise", "inbox", "0", ""), Verdict::ALLOWED);
	check("draft_reply in mandate allowed",            d("draft_reply", "papa", "0", ""), Verdict::ALLOWED);
	check("pay without approval held",                 d("pay", "ramesh.kumar@ybl", "15000", ""), Verdict::HELD_NEEDS_APPROVAL);
	check("pay with exact approval allowed",           d("pay", "ramesh.kumar@ybl", "15000", APPR_PAY), Verdict::ALLOWED_APPROVED);
	check("same approval, Rs 50,000 held",             d("pay", "ramesh.kumar@ybl", "50000", APPR_PAY), Verdict::HELD_MISMATCH);
	check("same approval, other payee held",           d("pay", "kyc-desk@okaxis", "15000", APPR_PAY), Verdict::HELD_MISMATCH);
	check("delete without approval held",              d("delete", "photos/2019", "0", ""), Verdict::HELD_NEEDS_APPROVAL);
	check("share approval reused for pay held",        d("pay", "form16.pdf", "0", APPR_SHARE), Verdict::HELD_MISMATCH);
	check("forged approval held",                      d("pay", "ramesh.kumar@ybl", "15000",
	      "00000000000000000000000000000000000000000000000000000000000000ff"), Verdict::HELD_MISMATCH);

	/* new: the actions now come from an agent-written ROM */
	check("pay approval reused as transfer held",      d("transfer", "ramesh.kumar@ybl", "15000", APPR_PAY), Verdict::HELD_MISMATCH);
	check("'|' moves field boundary: held",            d("pay", "r@ybl", "x|5", APPR_SPLIT), Verdict::HELD_BAD_INPUT);
	check("'|' in target held",                        d("pay", "r@ybl|x", "5", APPR_SPLIT), Verdict::HELD_BAD_INPUT);
	char longt[80]; std::memset(longt, 'a', 64); longt[64] = 0;
	check("64-char target (would truncate) held",      d("pay", longt, "15000", APPR_PAY), Verdict::HELD_BAD_INPUT);
	check("caller-flagged truncation held",            decide(key, key_len, "pay", "ramesh.kumar@ybl", "15000", APPR_PAY, true), Verdict::HELD_BAD_INPUT);
	check("empty target on pay held",                  d("pay", "", "15000", APPR_PAY), Verdict::HELD_BAD_INPUT);
	check("newline in target held",                    d("pay", "ramesh.kumar@ybl\n", "15000", APPR_PAY), Verdict::HELD_BAD_INPUT);
	check("empty type held",                           d("", "x", "0", ""), Verdict::HELD_BAD_INPUT);
	check("'Pay' (capital) blocked, not allowed",       d("Pay", "ramesh.kumar@ybl", "15000", ""), Verdict::BLOCKED_OUTSIDE_MANDATE);
	check("'pay ' (trailing space) blocked",          d("pay ", "ramesh.kumar@ybl", "15000", APPR_PAY), Verdict::BLOCKED_OUTSIDE_MANDATE);
	check("unknown type open_link blocked",            d("open_link", "http://kyc.example", "0", ""), Verdict::BLOCKED_OUTSIDE_MANDATE);
	check("'|' in draft_reply target held",          d("draft_reply", "a|b", "0", ""), Verdict::HELD_BAD_INPUT);
	check("no owner key: valid-looking approval held", d("pay", "ramesh.kumar@ybl", "15000", APPR_PAY, 0), Verdict::HELD_NEEDS_APPROVAL);
	check("uppercase-hex approval held",               d("pay", "ramesh.kumar@ybl", "15000",
	      "ABCDEF0000000000000000000000000000000000000000000000000000000000"), Verdict::HELD_NEEDS_APPROVAL);

	std::printf("\n%d/%d pass\n", pass, pass + fail);
	return fail ? 1 : 0;
}
