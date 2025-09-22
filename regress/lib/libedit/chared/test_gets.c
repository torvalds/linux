/*	$OpenBSD: test_gets.c,v 1.5 2017/07/07 23:55:21 bluhm Exp $	*/
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

#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <wchar.h>
#include <err.h>

#include "chared.c"

/*
 * Glue for unit tests of libedit/chared.c.
 * Rather than linking in all the various libedit modules,
 * provide dummies for those functions called in chared.c.
 * Most aren't actually called in c_gets().
 */

#define EL EditLine *el __attribute__((__unused__))
#define UU __attribute__((__unused__))

int hist_enlargebuf(EL, size_t oldsz UU, size_t newsz UU) { return 1; }
void re_refresh(EL) { }
void re_refresh_cursor(EL) { }
void terminal_beep(EL) { putchar('\a'); }

el_action_t
ed_end_of_file(EditLine *el, wint_t c UU) {
	*el->el_line.lastchar = '\0';
	return CC_EOF;
}

int
el_wgetc(EL, wchar_t *cp) {
	return (*cp = getwchar()) != WEOF ? 1 : feof(stdin) ? 0 : -1;
}

#undef EL
#undef UU

/*
 * Unit test steering program for editline/chared.c, c_gets().
 */

int
main()
{
	EditLine el;
	wchar_t buf[EL_BUFSIZ];
	int i, len;

	if (setlocale(LC_CTYPE, "") == NULL)
		err(1, "setlocale");
	if (ch_init(&el) == -1)
		err(1, "ch_init");
	while (feof(stdin) == 0) {
		errno = 0;
		if ((len = c_gets(&el, buf, L"$")) == -1) {
			if (feof(stdin))
				fputs("eof:", stdout);
			if (ferror(stdin)) {
				fputs("error:", stdout);
				clearerr(stdin);
			}
			printf("%d:", errno);
		}
		printf("%d:", len);
		if (len > 0) {
			for (i = 0; i < len; i++)
				putwchar(buf[i]);
			putchar(':');
			for (i = 1; i <= len; i++)
				putwchar(el.el_line.buffer[i]);
			puts(":");
		} else
			puts("::");
		assert(el.el_line.buffer[0] == '\0');
		assert(el.el_line.lastchar == el.el_line.buffer);
		assert(el.el_line.cursor == el.el_line.buffer);
	}
	return 0;
}
