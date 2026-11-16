/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * Turiya - the owner's own list of known targets, read from a small text file the owner writes.
 *
 * Until now the list was compiled into sakshi.cc (OWNER_KNOWN). This header turns the owner's file into the
 * same shape decide() takes (a null-terminated list of names). It is pure and freestanding: no allocation,
 * no library, no global state; Sakshi gives it the bytes and gets a list back.
 *
 * The file, one statement per line ('\n' ends a line; one '\r' at the end of a line is dropped):
 *
 *     # people and places the owner knows
 *     known: papa
 *     known: ramesh.kumar@ybl
 *
 *   - a blank line (spaces and tabs only) or a line whose first non-blank byte is '#' is a comment;
 *   - "known:" (lower case, exactly), then spaces or tabs, then one name, then spaces or tabs to the end;
 *   - a name is 1..63 bytes; every byte is printable ASCII (0x21-0x7e) other than '|', or a byte >= 0x80
 *     (UTF-8 is kept as it is: names are compared byte for byte, never folded);
 *   - at most 16 names; a name already in the list is ignored.
 *
 * It fails closed. Anything else on a line (another keyword, "Known:", a second word, a trailing comment,
 * a control byte, a NUL, a '|', an over-long name, an empty name) skips that whole line; it never shortens,
 * cleans or guesses a name. Names after the 16th are left out. Every line lands in exactly one count
 * (comment, accepted, duplicate, over_limit, skipped), so Sakshi can tell the owner what was not taken.
 * A file with no accepted name gives an empty list, which decide() treats as "no target is known"
 * (default-deny), never as "no list".
 *
 * The limits are template parameters so the proofs can check a small instance; Sakshi uses Owner_config.
 */
#pragma once
#include "kartr_rule.h"

namespace Turiya {

	static u8 const OWNER_KEY[6] = { 'k', 'n', 'o', 'w', 'n', ':' };   /* "known:" */
	inline bool owner_blank(u8 c) { return c == ' ' || c == '\t'; }
	inline bool owner_name_byte(u8 c) { return (c >= 0x21 && c <= 0x7e && c != '|') || c >= 0x80; }

	struct Owner_config_counts
	{
		usize lines;        /* lines seen (a last line without '\n' counts; an empty file has 0) */
		usize comments;     /* blank or '#' lines */
		usize accepted;     /* names taken into the list */
		usize duplicates;   /* valid names already in the list */
		usize over_limit;   /* valid new names after the list was full */
		usize skipped;      /* every other line */
	};

	template <unsigned MAX_NAMES, unsigned NAME_MAX>
	class Owner_config_t
	{
		public:
			enum { MAX = MAX_NAMES, NAME = NAME_MAX };

		private:
			char _name[MAX_NAMES][NAME_MAX + 1];
			Cstr _list[MAX_NAMES + 1];
			usize _count;
			Owner_config_counts _c;

			/* copying would leave the list pointing into the source */
			Owner_config_t(Owner_config_t const &);
			Owner_config_t &operator = (Owner_config_t const &);

			bool _have(u8 const *p, usize n) const
			{
				for (usize i = 0; i < _count; i++) {
					usize j = 0;
					while (j < n && _name[i][j] == (char)p[j]) j++;
					if (j == n && _name[i][n] == 0) return true;
				}
				return false;
			}

			/* one line, without its '\n' (and without the '\r' before it) */
			void _line(u8 const *p, usize n)
			{
				_c.lines++;
				usize i = 0;
				while (i < n && owner_blank(p[i])) i++;
				if (i == n || p[i] == '#') { _c.comments++; return; }

				for (usize k = 0; k < 6; k++, i++)
					if (i == n || p[i] != OWNER_KEY[k]) { _c.skipped++; return; }
				while (i < n && owner_blank(p[i])) i++;

				usize const s = i;
				while (i < n && owner_name_byte(p[i])) i++;
				usize const len = i - s;
				while (i < n && owner_blank(p[i])) i++;
				if (i != n || len == 0 || len > NAME_MAX) { _c.skipped++; return; }

				if (_have(p + s, len))    { _c.duplicates++; return; }
				if (_count == MAX_NAMES) { _c.over_limit++; return; }

				for (usize k = 0; k < len; k++) _name[_count][k] = (char)p[s + k];
				_name[_count][len] = 0;
				_list[_count] = _name[_count];
				_count++;
				_list[_count] = nullptr;
				_c.accepted++;
			}

		public:
			Owner_config_t() : _name(), _list(), _count(0), _c() { _list[0] = nullptr; }

			/* read the owner's file; 'text' may hold any bytes (NUL included); replaces what was read before */
			void parse(u8 const *text, usize len)
			{
				_count = 0; _list[0] = nullptr;
				_c.lines = _c.comments = _c.accepted = _c.duplicates = _c.over_limit = _c.skipped = 0;
				if (!text) len = 0;
				usize start = 0;
				for (usize i = 0; i < len; i++) {
					if (text[i] != '\n') continue;
					usize e = i;
					if (e > start && text[e - 1] == '\r') e--;
					_line(text + start, e - start);
					start = i + 1;
				}
				if (start < len) {                                    /* a last line without '\n' */
					usize e = len;
					if (text[e - 1] == '\r') e--;
					_line(text + start, e - start);
				}
			}

			/* the list decide() takes: null-terminated, never null itself, empty when nothing was accepted */
			Cstr const *known() const { return _list; }
			usize count() const { return _count; }
			Cstr name(usize i) const { if (i < _count) return _name[i]; return nullptr; }
			/* byte j of name i (0 past its end or past the list); lets the proof read names without pointers */
			char at(usize i, usize j) const { return i < _count && j <= NAME_MAX ? _name[i][j] : 0; }
			Owner_config_counts counts() const { return _c; }   /* a copy (also keeps CBMC's C++ parser on track) */
	};

	typedef Owner_config_t<16, FIELD_MAX> Owner_config;
}
