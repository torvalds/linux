/*	$OpenBSD: mbrtoc16.c,v 1.1 2023/08/20 15:02:51 schwarze Exp $ */
/*
 * Copyright (c) 2022 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdint.h>
#include <uchar.h>
#include <wchar.h>

/*
 * Keep this structure compatible with
 * struct _utf8_state in the file citrus/citrus_utf8.c.
 * In particular, only use values for the "want" field
 * that do not collide with values used by the function
 * _citrus_utf8_ctype_mbrtowc().
 */
struct _utf16_state {
	wchar_t	ch;
	int	want;
};

size_t
mbrtoc16(char16_t *pc16, const char *s, size_t n, mbstate_t *ps)
{
	static mbstate_t	 mbs;
	struct _utf16_state	*us;
	size_t			 rv;
	wchar_t			 wc;

	/*
	 * Fall back to a state object local to this function
	 * and do not use the fallback object in mbrtowc(3)
	 * because an application program might mix calls to mbrtowc(3)
	 * and mbrtoc16(3) decoding different strings, and they must
	 * not clobber each other's state.
	 */
	if (ps == NULL)
		ps = &mbs;

	us = (struct _utf16_state *)ps;

	/*
	 * Handle the special case of NULL input first such that
	 * a low surrogate left over from a previous call does not
	 * clobber an object pointed to by the pc16 argument.
	 */
	if (s == NULL) {
		s = "";
		n = 1;
		pc16 = NULL;
	}

	/*
	 * If the previous call stored a high surrogate,
	 * store the corresponding low surrogate now
	 * and do not inspect any further input yet.
	 */
	if (us->want == (size_t)-3) {
		if (pc16 != NULL)
			*pc16 = 0xdc00 + (us->ch & 0x3ff);
		us->ch = 0;
		us->want = 0;
		return -3;
	}

	/*
	 * Decode the multibyte character.
	 * All the mbrtowc(3) use cases can be reached from here,
	 * including continuing an imcomplete character started earlier,
	 * decoding a NUL character, a valid complete character,
	 * an incomplete character to be continued later,
	 * or a decoding error.
	 */
	rv = mbrtowc(&wc, s, n, ps);

	if (rv < (size_t)-2) {
		/* A new character that is valid and complete. */
		if (wc > UINT16_MAX) {
			/* Store a high surrogate. */
			if (pc16 != NULL)
				*pc16 = 0xd7c0 + (wc >> 10);
			/* Remember that the low surrogate is pending. */
			us->ch = wc;
			us->want = -3;
		} else if (pc16 != NULL)
			/* Store a basic multilingual plane codepoint. */
			*pc16 = wc;
	}
	return rv;
}
