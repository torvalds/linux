/*	$OpenBSD: c16rtomb.c,v 1.1 2023/08/20 15:02:51 schwarze Exp $ */
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

#include <errno.h>
#include <uchar.h>
#include <wchar.h>

/*
 * Keep this structure compatible with
 * struct _utf8_state in the file citrus/citrus_utf8.c.
 */
struct _utf16_state {
	wchar_t	ch;
	int	want;
};

size_t
c16rtomb(char *s, char16_t c16, mbstate_t *ps)
{
	static mbstate_t	 mbs;
	struct _utf16_state	*us;
	wchar_t			 wc;

	if (ps == NULL)
		ps = &mbs;

	/*
	 * Handle the special case of NULL output first
	 * to avoid inspecting c16 and ps and possibly drawing
	 * bogus conclusions from whatever those may contain.
	 * Instead, just restore the initial conversion state.
	 * The return value represents the length of the NUL byte
	 * corresponding to the NUL wide character, even though
	 * there is no place to write that NUL byte to.
	 */
	if (s == NULL) {
		memset(ps, 0, sizeof(*ps));
		return 1;
	}

	us = (struct _utf16_state *)ps;

	if (us->want == (size_t)-3) {

		/*
		 * The previous call read a high surrogate,
		 * so expect a low surrogate now.
		 */
		if ((c16 & 0xfc00) != 0xdc00) {
			errno = EILSEQ;
			return -1;
		}

		/*
		 * Assemble the full code point for processing
		 * by wcrtomb(3).  Since we do not support
		 * state-dependent encodings, our wcrtomb(3)
		 * always expects the initial conversion state,
		 * so clearing the state here is just fine.
		 */
		wc = us->ch + (c16 & 0x3ff);
		us->ch = 0;
		us->want = 0;

	} else if ((c16 & 0xfc00) == 0xd800) {

		/*
		 * Got a high surrogate while being in the initial
		 * conversion state.  Remeber its contribution to
		 * the codepoint and defer encoding to the next call.
		 */
		us->ch = 0x10000 + ((c16 & 0x3ff) << 10);
		us->want = -3;

		/* Nothing was written to *s just yet. */
		return 0;

	} else
		wc = c16;

	/*
	 * The following correctly returns an error when a low
	 * surrogate is encountered without a preceding high one.
	 */
	return wcrtomb(s, wc, ps);
}
