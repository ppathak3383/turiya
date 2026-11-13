/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - the Kartr rule, free of any Genode dependency so the exact code that runs in the
 * component is also unit-tested on a host (tests/test_kartr_rule.cc).
 *
 * An irreversible action (pay, transfer, delete, send_as_user, publish, share_file) is allowed only
 * with the owner's approval: HMAC-SHA256(key, "type|target|amount"). The fields come from an
 * agent-written ROM, so the rule also refuses input that could make one approval fit another
 * action: a field containing '|', an over-long (truncated) field, or an empty target.
 * Any other type runs only if the owner's mandate lists it exactly (as in Phase 0); an unknown
 * type, or a near-miss such as "Pay", is blocked, never treated as harmless.
 */
#ifndef _TURIYA__KARTR_RULE_H_
#define _TURIYA__KARTR_RULE_H_

namespace Turiya {
	typedef unsigned char      u8;
	typedef unsigned int       u32;
	typedef unsigned long long u64;
	typedef unsigned long      usize;

	enum { FIELD_MAX = 63 };   /* longest type/target/amount accepted (Genode String<64>) */

	enum class Verdict { ALLOWED, ALLOWED_APPROVED, HELD_NEEDS_APPROVAL, HELD_MISMATCH, HELD_BAD_INPUT,
	                     BLOCKED_OUTSIDE_MANDATE };

	inline char const *verdict_name(Verdict v)
	{
		switch (v) {
		case Verdict::ALLOWED:             return "allowed";
		case Verdict::ALLOWED_APPROVED:    return "allowed_with_approval";
		case Verdict::HELD_NEEDS_APPROVAL: return "held_needs_approval";
		case Verdict::HELD_MISMATCH:       return "held_approval_mismatch";
		case Verdict::HELD_BAD_INPUT:      return "held_bad_input";
		case Verdict::BLOCKED_OUTSIDE_MANDATE: return "blocked_outside_mandate";
		}
		return "held_bad_input";
	}

	inline bool is_held(Verdict v) { return v != Verdict::ALLOWED && v != Verdict::ALLOWED_APPROVED; }

	/* kept as a loop: without a C library there is no strlen for the compiler to turn this into */
	__attribute__((optimize("no-tree-loop-distribute-patterns")))
	inline usize slen(char const *s) { usize n = 0; while (s && s[n]) n++; return n; }

	inline bool streq(char const *a, char const *b)
	{
		usize i = 0;
		for (; a[i] && b[i]; i++) if (a[i] != b[i]) return false;
		return a[i] == b[i];
	}

	struct Sha256
	{
		u32 h[8] { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		           0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
		u8  buf[64] { };
		u32 len = 0;
		u64 total = 0;

		static u32 rotr(u32 x, unsigned n) { return (x >> n) | (x << (32 - n)); }

		void block(u8 const *p)
		{
			static u32 const k[64] = {
				0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
				0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
				0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
				0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
				0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
				0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
				0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
				0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
			u32 w[64];
			for (unsigned i = 0; i < 16; i++)
				w[i] = (u32)p[4*i] << 24 | (u32)p[4*i+1] << 16 | (u32)p[4*i+2] << 8 | p[4*i+3];
			for (unsigned i = 16; i < 64; i++) {
				u32 s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
				u32 s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19)  ^ (w[i-2] >> 10);
				w[i] = w[i-16] + s0 + w[i-7] + s1;
			}
			u32 a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
			for (unsigned i = 0; i < 64; i++) {
				u32 t1 = hh + (rotr(e,6) ^ rotr(e,11) ^ rotr(e,25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
				u32 t2 = (rotr(a,2) ^ rotr(a,13) ^ rotr(a,22)) + ((a & b) ^ (a & c) ^ (b & c));
				hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
			}
			h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
		}

		void add(u8 const *p, usize n)
		{
			total += n;
			while (n--) { buf[len++] = *p++; if (len == 64) { block(buf); len = 0; } }
		}

		void finish(u8 out[32])
		{
			u64 bits = total * 8;
			u8 one = 0x80, zero = 0;
			add(&one, 1);
			while (len != 56) add(&zero, 1);
			u8 l[8];
			for (unsigned i = 0; i < 8; i++) l[i] = (u8)(bits >> (56 - 8*i));
			add(l, 8);
			for (unsigned i = 0; i < 8; i++) {
				out[4*i] = (u8)(h[i] >> 24); out[4*i+1] = (u8)(h[i] >> 16);
				out[4*i+2] = (u8)(h[i] >> 8); out[4*i+3] = (u8)h[i];
			}
		}
	};

	inline int nib(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	}

	/* lowercase hex -> bytes; returns byte count, 0 on any error */
	inline usize unhex(char const *s, u8 *out, usize max)
	{
		usize n = slen(s);
		if (n == 0 || n % 2 || n / 2 > max) return 0;
		for (usize i = 0; i < n / 2; i++) {
			int a = nib(s[2*i]), b = nib(s[2*i+1]);
			if (a < 0 || b < 0) return 0;
			out[i] = (u8)(a << 4 | b);
		}
		return n / 2;
	}

	inline void hmac(u8 const *key, usize key_len, char const *msg, usize msg_len, u8 out[32])
	{
		u8 k[64] { };
		for (usize i = 0; i < key_len && i < 64; i++) k[i] = key[i];
		u8 ipad[64], opad[64];
		for (unsigned i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
		u8 inner[32];
		Sha256 a; a.add(ipad, 64); a.add((u8 const *)msg, msg_len); a.finish(inner);
		Sha256 b; b.add(opad, 64); b.add(inner, 32); b.finish(out);
	}

	inline bool irreversible(char const *t)
	{
		return streq(t, "pay") || streq(t, "transfer") || streq(t, "delete") || streq(t, "send_as_user")
		    || streq(t, "publish") || streq(t, "share_file");
	}

	inline bool field_ok(char const *f, bool may_be_empty)
	{
		usize n = slen(f);
		if (n > FIELD_MAX || (!n && !may_be_empty)) return false;
		for (usize i = 0; i < n; i++) {
			unsigned char c = (unsigned char)f[i];
			if (c == '|' || c < 0x20 || c == 0x7f) return false;
		}
		return true;
	}

	/* the owner's mandate: reversible types this agent may use; a null-terminated list */
	static char const * const DEFAULT_MANDATE[] = { "summarise", "draft_reply", "add_reminder", nullptr };

	inline bool in_mandate(char const * const *mandate, char const *t)
	{
		for (; mandate && *mandate; mandate++) if (streq(*mandate, t)) return true;
		return false;
	}

	/*
	 * Decide one action. 'type', 'target', 'amount' and 'approval_hex' come from the agent's ROM;
	 * 'key' only from the owner's config. 'truncated' is set by the caller when the ROM attribute
	 * did not fit, so an over-long field is held rather than silently shortened.
	 */
	inline Verdict decide(u8 const *key, usize key_len,
	                      char const *type, char const *target, char const *amount,
	                      char const *approval_hex, bool truncated,
	                      char const * const *mandate = DEFAULT_MANDATE)
	{
		if (truncated || !field_ok(type, false) || !field_ok(amount, false))
			return Verdict::HELD_BAD_INPUT;

		if (!irreversible(type)) {
			if (!in_mandate(mandate, type)) return Verdict::BLOCKED_OUTSIDE_MANDATE;
			return field_ok(target, true) ? Verdict::ALLOWED : Verdict::HELD_BAD_INPUT;
		}

		if (!field_ok(target, false))
			return Verdict::HELD_BAD_INPUT;

		u8 given[32] { };
		if (!key_len || unhex(approval_hex, given, 32) != 32)
			return Verdict::HELD_NEEDS_APPROVAL;

		char msg[3 * FIELD_MAX + 3];
		usize m = 0;
		for (char const *p = type;   *p; p++) msg[m++] = *p;
		msg[m++] = '|';
		for (char const *p = target; *p; p++) msg[m++] = *p;
		msg[m++] = '|';
		for (char const *p = amount; *p; p++) msg[m++] = *p;

		u8 expect[32];
		hmac(key, key_len, msg, m, expect);
		u8 diff = 0;                                  /* constant-time compare */
		for (unsigned i = 0; i < 32; i++) diff |= (u8)(expect[i] ^ given[i]);
		return diff ? Verdict::HELD_MISMATCH : Verdict::ALLOWED_APPROVED;
	}
}

#endif /* _TURIYA__KARTR_RULE_H_ */
