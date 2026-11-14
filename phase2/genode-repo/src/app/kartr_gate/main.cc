/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - Kartr gate as a Genode component (Phase 1, step 1d).
 *
 * Input:  an "actions" ROM written by the agent component (it may be rewritten at any time; the
 *         gate re-reads it on every update). Example:
 *           <actions> <action id="a1" type="pay" target="ramesh.kumar@ybl" amount="15000"
 *                             approval="194d..."/> </actions>
 * Output: a "verdicts" report for the owner's screen, one <verdict> per action.
 * Owner:  the key and the mandate come only from the config ROM, which the agent cannot write:
 *           <config key="..."> <mandate type="summarise"/> <mandate type="draft_reply"/> </config>
 *
 * The decision itself is kartr_rule.h, the same code the host test runs (tests/test_kartr_rule.cc).
 * The component holds no capabilities except its two ROMs, the report and the log. It cannot pay,
 * delete or send anything itself; it only decides.
 */
#include <base/component.h>
#include <base/log.h>
#include <base/attached_rom_dataspace.h>
#include <os/reporter.h>
#include <util/string.h>
#include "kartr_rule.h"

namespace Turiya { struct Main; }

struct Turiya::Main
{
	Genode::Env &_env;

	/* noncopyable (holds pointers into its own storage) */
	Main(Main const &) = delete;
	Main &operator = (Main const &) = delete;

	Genode::Attached_rom_dataspace _config  { _env, "config"  };
	Genode::Attached_rom_dataspace _actions { _env, "actions" };   /* written by the agent */
	Genode::Attached_rom_dataspace _outside { _env, "outside" };   /* what the agent read, from the fetcher */

	enum { OUTSIDE_MAX = 16*1024 };
	char _outside_text[OUTSIDE_MAX + 1] { };

	Genode::Expanding_reporter _verdicts { _env, "verdicts", "verdicts" };

	/* one char more than FIELD_MAX so an over-long value is seen as over-long, not truncated */
	typedef Genode::String<FIELD_MAX + 2> Field;
	typedef Genode::String<80>            Hex;

	enum { MANDATE_MAX = 8 };
	Field        _mandate_store[MANDATE_MAX];
	char const  *_mandate[MANDATE_MAX + 1] { };

	u8    _key[64] { };
	usize _key_len = 0;

	Genode::Signal_handler<Main> _actions_handler { _env.ep(), *this, &Main::_handle_actions };

	void _read_config()
	{
		Genode::Node const config = _config.node();
		_key_len = unhex(config.attribute_value("key", Hex()).string(), _key, sizeof(_key));
		if (!_key_len)
			Genode::warning("no owner key configured: every irreversible action will be held");

		unsigned n = 0;
		config.for_each_sub_node("mandate", [&] (Genode::Node const &m) {
			if (n < MANDATE_MAX) _mandate_store[n++] = m.attribute_value("type", Field()); });
		if (n == 0)
			for (char const * const *d = DEFAULT_MANDATE; *d && n < MANDATE_MAX; d++)
				_mandate_store[n++] = Field(*d);
		for (unsigned i = 0; i < n; i++) _mandate[i] = _mandate_store[i].string();
		_mandate[n] = nullptr;
	}

	void _handle_actions()
	{
		_actions.update();
		_outside.update();
		{
			Genode::size_t n = Genode::min(_outside.size(), (Genode::size_t)OUTSIDE_MAX);
			char const *src = _outside.local_addr<char const>();
			Genode::size_t i = 0;
			for (; i < n && src[i]; i++) _outside_text[i] = src[i];
			_outside_text[i] = 0;
		}
		Genode::Node const actions = _actions.node();
		unsigned allowed = 0, held = 0;

		_verdicts.generate([&] (Genode::Generator &g) {
			actions.for_each_sub_node("action", [&] (Genode::Node const &a) {
				Field const id     = a.attribute_value("id",       Field("?"));
				Field const type   = a.attribute_value("type",     Field());
				Field const target = a.attribute_value("target",   Field());
				Field const amount = a.attribute_value("amount",   Field("0"));
				Hex   const appr   = a.attribute_value("approval", Hex());

				/* an approval longer than 64 hex chars is truncated by Hex: flag it */
				bool const truncated = slen(appr.string()) > 64;

				Verdict const v = decide(_key, _key_len, type.string(), target.string(),
				                         amount.string(), appr.string(), truncated, _mandate, _outside_text);
				is_held(v) ? held++ : allowed++;

				Genode::log("[", id, "] ", verdict_name(v), ": ", type, " ", target, " ", amount);
				g.node("verdict", [&] {
					g.attribute("id",      id);
					g.attribute("type",    type);
					g.attribute("target",  target);
					g.attribute("amount",  amount);
					g.attribute("verdict", verdict_name(v));
				});
			});
		});
		Genode::log("kartr_gate: ", allowed, " allowed, ", held, " held");
	}

	Main(Genode::Env &env) : _env(env)
	{
		_read_config();
		_actions.sigh(_actions_handler);
		_outside.sigh(_actions_handler);
		_handle_actions();
	}
};

void Component::construct(Genode::Env &env) { static Turiya::Main main(env); }
