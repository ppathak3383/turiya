// SPDX-License-Identifier: AGPL-3.0-or-later
// Host test of the owner's-file parser Sakshi runs (../owner_config.h).
// Build: g++ -std=c++20 -Wall -Wextra -Werror -Weffc++ -I.. test_owner_config.cc
#include <cstdio>
#include <cstring>
#include <string>
#include "owner_config.h"
using namespace Turiya;

static int pass = 0, fail = 0;
static void ok(char const *name, bool c) { (c ? pass : fail)++; std::printf("%s %s\n", c ? "PASS" : "FAIL", name); }

static Owner_config cfg;
static void P(std::string const &s) { cfg.parse((u8 const *)s.data(), s.size()); }

/* the list, as "a,b,c" */
static std::string L()
{
	std::string r;
	for (Cstr const *k = cfg.known(); *k; k++) { if (!r.empty()) r += ','; r += *k; }
	return r;
}
static bool C(usize lines, usize com, usize acc, usize dup, usize over, usize skip)
{
	Owner_config_counts const c = cfg.counts();
	return c.lines == lines && c.comments == com && c.accepted == acc && c.duplicates == dup
	    && c.over_limit == over && c.skipped == skip;
}
static void one(char const *name, std::string const &text, char const *want, usize skipped)
{
	P(text);
	ok(name, L() == want && cfg.counts().skipped == skipped);
}

int main()
{
	/* the file the owner is expected to write */
	P("# people the owner knows\nknown: inbox\nknown: sender\nknown: papa\nknown: ramesh.kumar@ybl\n");
	ok("the stand-in list, read from a file", L() == "inbox,sender,papa,ramesh.kumar@ybl" && C(5, 1, 4, 0, 0, 0));
	ok("count and name() agree", cfg.count() == 4 && !std::strcmp(cfg.name(3), "ramesh.kumar@ybl") && !cfg.name(4));

	/* empty and comment-only files give an empty list (default-deny), never no list */
	P("");                       ok("empty file: empty list, 0 lines", cfg.known() && !*cfg.known() && C(0, 0, 0, 0, 0, 0));
	P("\n\n");                   ok("two blank lines are comments", L() == "" && C(2, 2, 0, 0, 0, 0));
	P("# only\n   # indented\n\t\n"); ok("comment-only file", L() == "" && C(3, 3, 0, 0, 0, 0));
	cfg.parse(nullptr, 99);      ok("null text read as empty", cfg.known() && !*cfg.known() && C(0, 0, 0, 0, 0, 0));

	/* line endings and blanks */
	one("last line without newline",            "known: papa",                     "papa", 0);
	one("CRLF line endings",                    "known: papa\r\nknown: maa\r\n",   "papa,maa", 0);
	one("tab and spaces around the name",       " \tknown:\t papa \t\n",           "papa", 0);
	one("no blank after the colon",             "known:papa\n",                    "papa", 0);
	one("a lone CR inside a line is not an end", "known: papa\rknown: maa\n",      "", 1);
	one("CR on a last line without newline",   "known: papa\r",                  "papa", 0);
	one("two CRs: one dropped, one left (skip)", "known: papa\r\r\n",              "", 1);

	/* fail closed: the whole line is skipped, nothing is shortened or cleaned */
	one("'Known:' is not the keyword",          "Known: papa\n",                   "", 1);
	one("'known' without a colon",              "known papa\n",                    "", 1);
	one("'known :' with a blank before colon",  "known : papa\n",                  "", 1);
	one("other keyword",                        "trusted: papa\n",                 "", 1);
	one("empty name",                           "known:\nknown:   \n",             "", 2);
	one("two words",                            "known: papa ji\n",                "", 1);
	one("trailing comment is not stripped",     "known: papa # dad\n",             "", 1);
	one("'|' inside a name",                    "known: papa|kyc\n",               "", 1);
	one("'|' alone",                            "known: |\n",                      "", 1);
	one("NUL inside a name",                    std::string("known: pa\0pa\n", 13), "", 1);
	one("DEL inside a name",                    "known: pa\x7fpa\n",               "", 1);
	one("ESC (control) inside a name",          "known: pa\x1b[2Jpa\n",            "", 1);
	one("vertical tab is not a blank",          "known:\vpapa\n",                  "", 1);
	one("a line of junk",                       "\x01\x02\x03\n",                  "", 1);
	one("good lines around a bad one kept",     "known: a\nknown: b c\nknown: d\n", "a,d", 1);

	/* lengths: 63 bytes is the most */
	std::string n63(63, 'x'), n64(64, 'y');
	one("63-byte name accepted",                "known: " + n63 + "\n",            n63.c_str(), 0);
	one("64-byte name skipped",                 "known: " + n64 + "\n",            "", 1);
	one("1-byte name accepted",                 "known: z\n",                      "z", 0);
	std::string huge = "known: " + std::string(100000, 'q') + "\nknown: ok\n";
	one("100 000-byte line skipped, next kept", huge,                              "ok", 1);

	/* names are kept byte for byte: case, UTF-8, look-alikes are different names */
	one("case kept: papa and Papa both listed", "known: papa\nknown: Papa\n",      "papa,Papa", 0);
	one("UTF-8 kept as bytes (Cyrillic рара)",  "known: \xd1\x80\xd0\xb0\xd1\x80\xd0\xb0\n", "\xd1\x80\xd0\xb0\xd1\x80\xd0\xb0", 0);
	one("non-breaking space is part of name",   "known: pa\xc2\xa0pa\n",           "pa\xc2\xa0pa", 0);

	/* duplicates are ignored and counted, not skipped */
	P("known: papa\nknown: papa\n known: papa \n");
	ok("duplicates ignored and counted", L() == "papa" && C(3, 0, 1, 2, 0, 0));
	P("known: pap\nknown: papa\nknown: pa\n");
	ok("prefixes are not duplicates", L() == "pap,papa,pa" && C(3, 0, 3, 0, 0, 0));

	/* at most 16 names */
	std::string t;
	for (int i = 0; i < 20; i++) t += "known: n" + std::to_string(i) + "\n";
	t += "known: n3\n";
	P(t);
	ok("16 names kept, 4 over the limit", cfg.count() == 16 && C(21, 0, 16, 1, 4, 0)
	   && !std::strcmp(cfg.name(15), "n15") && cfg.known()[16] == nullptr);
	ok("a duplicate after the list is full counts as a duplicate", cfg.counts().duplicates == 1);

	/* a second parse replaces the first */
	P("known: a\nknown: b\n"); P("known: c\n");
	ok("parse replaces the previous list", L() == "c" && C(1, 0, 1, 0, 0, 0));

	/* the parsed list drives the gate exactly as the compiled-in list did */
	u8 key[32]; usize const kl = sizeof key; for (usize i = 0; i < kl; i++) key[i] = (u8)(0x5a ^ i);   /* any key: both lists see the same */
	static Cstr const STATIC_KNOWN[] = { "inbox", "sender", "papa", "ramesh.kumar@ybl", nullptr };
	P("known: inbox\nknown: sender\nknown: papa\nknown: ramesh.kumar@ybl\n");
	char const *targets[] = { "", "papa", "Papa", "papa ", "stranger@x.in", "ramesh.kumar@ybl", "inbox", "sender", "maa" };
	char const *types[]   = { "summarise", "draft_reply", "add_reminder", "send_email", "pay" };
	char const *mail = "From: friend@example.com\nhello";
	int same = 0, total = 0;
	for (char const *ty : types) for (char const *tg : targets) for (int st = 0; st < 3; st++) {
		Verdict a = decide(key, kl, ty, tg, "0", "", false, DEFAULT_MANDATE, mail, (Avastha)st, STATIC_KNOWN);
		Verdict b = decide(key, kl, ty, tg, "0", "", false, DEFAULT_MANDATE, mail, (Avastha)st, cfg.known());
		total++; same += a == b;
	}
	std::printf("     gate verdicts, compiled list vs parsed list: %d of %d the same\n", same, total);
	ok("parsed list gives the gate the same verdicts", same == total);

	P("# nothing yet\n");
	ok("empty parsed list: a named target is held as not known",
	   decide(key, kl, "draft_reply", "papa", "0", "", false, DEFAULT_MANDATE, mail, Avastha::JAGRAT, cfg.known())
	   == Verdict::HELD_UNKNOWN_TARGET);

	std::printf("%d passed, %d failed\n", pass, fail);
	return fail ? 1 : 0;
}
