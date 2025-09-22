/*	$OpenBSD: test_get.c,v 1.5 2017/07/07 23:55:21 bluhm Exp $	*/
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
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

#include <err.h>

#include "keymacro.c"

/*
 * Glue for unit tests of libedit/keymacro.c.
 * Rather than linking in all the various libedit modules,
 * provide dummies for those functions called in keymacro.c.
 * Most aren't actually called in keymacro_get().
 */

#define EL EditLine *el __attribute__((__unused__))
#define UU __attribute__((__unused__))

ssize_t ct_encode_char(char *dst UU, size_t len UU, wchar_t c UU) { return -1; }

char *ct_encode_string(const wchar_t *s UU, ct_buffer_t *conv UU)
{
	return NULL;
}

ssize_t
ct_visual_char(wchar_t *dst, size_t len UU, wchar_t c)
{
	*dst = c;
	return 1;
}

int
el_wgetc(EL, wchar_t *cp) {
	static const wchar_t *const input_buffer = L"adalixi";
	static const wchar_t *input_ptr = input_buffer;

	*cp = *input_ptr;
	if (*cp == '\0') {
		input_ptr = input_buffer;
		return 0;
	} else {
		input_ptr++;
		return 1;
	}
}

#undef EL
#undef UU

/*
 * Unit test steering program for editline/keymacro.c, keymacro_get().
 */

int
main()
{
	EditLine el;
	keymacro_value_t val;
	wchar_t repl[] = L"repl";
	wchar_t ch;
	int irc;

	if (keymacro_init(&el) == -1)
		err(1, "keymacro_init");
	keymacro_add(&el, L"ad", keymacro_map_cmd(&el, VI_ADD), XK_CMD);
	keymacro_add(&el, L"al", keymacro_map_str(&el, repl), XK_STR);
	keymacro_add(&el, L"in", keymacro_map_cmd(&el, ED_INSERT), XK_CMD);

	if (el_wgetc(&el, &ch) == 0)
		errx(1, "el_wgetc ad");
	irc = keymacro_get(&el, &ch, &val);
	if (irc != XK_CMD)
		errx(1, "ad %d != XK_CMD", irc);
	if (val.cmd != VI_ADD)
		errx(1, "ad %u != VI_ADD", val.cmd);
	if (ch != L'd')
		errx(1, "ad %lc != d", ch);

	if (el_wgetc(&el, &ch) == 0)
		errx(1, "el_wgetc al");
	irc = keymacro_get(&el, &ch, &val);
	if (irc != XK_STR)
		errx(1, "al %d != XK_STR", irc);
	if (wcscmp(val.str, L"repl") != 0)
		errx(1, "al %ls != repl", val.str);
	if (ch != L'\0')
		errx(1, "al %lc != 0", ch);

	if (el_wgetc(&el, &ch) == 0)
		errx(1, "el_wgetc ix");
	irc = keymacro_get(&el, &ch, &val);
	if (irc != XK_STR)
		errx(1, "ix %d != XK_STR", irc);
	if (val.str != NULL)
		errx(1, "ix %ls != NULL", val.str);
	if (ch != L'x')
		errx(1, "ix %lc != x", ch);

	if (el_wgetc(&el, &ch) == 0)
		errx(1, "el_wgetc i");
	irc = keymacro_get(&el, &ch, &val);
	if (irc != XK_NOD)
		errx(1, "eof %d != XK_NOD", irc);
	return 0;
}
