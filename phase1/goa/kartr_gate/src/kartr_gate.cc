/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Kartr gate as a Genode component (Phase 1).
 *
 * The rule from Phase 0, now in its own protection domain: an agent may propose actions, but an
 * irreversible action (pay, transfer, delete, send_as_user, publish, share_file) is held unless it
 * carries the owner's approval: HMAC-SHA256 over "type|target|amount" with the owner's key.
 * An approval for one action does not fit another (a different amount or target is held).
 *
 * The component holds no capabilities except its config ROM and the log. It cannot pay, delete or
 * send anything itself; it only decides. Prototype: the key comes from the config; in Turiya it will
 * come from the Sakshi core through a session the agent can never reach.
 */
#include <base/component.h>
#include <base/log.h>
#include <base/attached_rom_dataspace.h>
#include <util/string.h>

namespace Turiya {
	using namespace Genode;
	typedef unsigned char      u8;
	typedef unsigned int       u32;
	typedef unsigned long long u64;
	struct Sha256;
	struct Main;
}

struct Turiya::Sha256
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

	void add(u8 const *p, size_t n)
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

struct Turiya::Main
{
	Env &_env;
	Attached_rom_dataspace _config { _env, "config" };

	typedef String<64>  Word;
	typedef String<128> Hex;

	static int _nib(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		return -1;
	}

	/* parse lowercase hex into out; returns number of bytes, 0 on error */
	static size_t _unhex(Hex const &hex, u8 *out, size_t max)
	{
		char const *s = hex.string();
		size_t n = strlen(s);
		if (n == 0 || n % 2 || n / 2 > max) return 0;
		for (size_t i = 0; i < n / 2; i++) {
			int a = _nib(s[2*i]), b = _nib(s[2*i+1]);
			if (a < 0 || b < 0) return 0;
			out[i] = (u8)(a << 4 | b);
		}
		return n / 2;
	}

	static void _hmac(u8 const *key, size_t key_len, char const *msg, u8 out[32])
	{
		u8 k[64] { };
		for (size_t i = 0; i < key_len && i < 64; i++) k[i] = key[i];
		u8 ipad[64], opad[64];
		for (unsigned i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
		u8 inner[32];
		Sha256 a; a.add(ipad, 64); a.add((u8 const *)msg, strlen(msg)); a.finish(inner);
		Sha256 b; b.add(opad, 64); b.add(inner, 32); b.finish(out);
	}

	static bool _irreversible(Word const &t)
	{
		return t == "pay" || t == "transfer" || t == "delete" || t == "send_as_user"
		    || t == "publish" || t == "share_file";
	}

	Main(Env &env) : _env(env)
	{
		Node const config = _config.node();

		u8 key[64] { };
		size_t const key_len = _unhex(config.attribute_value("key", Hex()), key, sizeof(key));
		if (!key_len)
			warning("no owner key configured: every irreversible action will be held");

		unsigned allowed = 0, held = 0;

		config.for_each_sub_node("action", [&] (Node const &a) {
			Word const id     = a.attribute_value("id",     Word("?"));
			Word const type   = a.attribute_value("type",   Word());
			Word const target = a.attribute_value("target", Word());
			Word const amount = a.attribute_value("amount", Word("0"));
			Hex  const appr   = a.attribute_value("approval", Hex());

			if (!_irreversible(type)) {
				log("[", id, "] allowed: ", type, " ", target);
				allowed++;
				return;
			}
			u8 given[32] { };
			if (!key_len || _unhex(appr, given, 32) != 32) {
				log("[", id, "] HELD: ", type, " ", target, " ", amount, " needs your approval");
				held++;
				return;
			}
			String<200> const msg(type, "|", target, "|", amount);
			u8 expect[32];
			_hmac(key, key_len, msg.string(), expect);
			u8 diff = 0;                          /* constant-time compare */
			for (unsigned i = 0; i < 32; i++) diff |= (u8)(expect[i] ^ given[i]);
			if (diff) {
				log("[", id, "] HELD: ", type, " ", target, " ", amount,
				    " approval does not match this exact action");
				held++;
			} else {
				log("[", id, "] allowed with your approval: ", type, " ", target, " ", amount);
				allowed++;
			}
		});
		log("kartr_gate: ", allowed, " allowed, ", held, " held");
	}
};

void Component::construct(Genode::Env &env) { static Turiya::Main main(env); }
