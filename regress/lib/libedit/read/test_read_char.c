/*	$OpenBSD: test_read_char.c,v 1.5 2017/07/05 15:31:45 bluhm Exp $	*/
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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "read.c"
#include "glue.c"

/*
 * Unit test steering program for editline/read.c, read_char().
 * Reads from standard input until read_char() returns 0.
 * Writes the code points read to standard output in %x format.
 * If EILSEQ is set after read_char(), indicating that there were some
 * garbage bytes before the character, the code point gets * prefixed.
 * The return value is indicated by appending to the code point:
 * a comma for 1, a full stop for 0, [%d] otherwise.
 * Errors out on unexpected failure (setlocale failure, malloc
 * failure, or unexpected errno).
 * Since ENOMSG is very unlikely to occur, it is used to make
 * sure that read_char() doesn't clobber errno.
 */

int
main(void)
{
	EditLine el;
	int irc;
	wchar_t cp;

	if (setlocale(LC_CTYPE, "") == NULL)
		err(1, "setlocale");
	el.el_flags = CHARSET_IS_UTF8;
	el.el_infd = STDIN_FILENO;
	if ((el.el_signal = calloc(1, sizeof(*el.el_signal))) == NULL)
		err(1, NULL);
	do {
		errno = ENOMSG;
		irc = read_char(&el, &cp);
		switch (errno) {
		case ENOMSG:
			break;
		case EILSEQ:
			putchar('*');
			break;
		default:
			err(1, NULL);
		}
		printf("%x", cp);
		switch (irc) {
		case 1:
			putchar(',');
			break;
		case 0:
			putchar('.');
			break;
		default:
			printf("[%d]", irc);
			break;
		}
	} while (irc != 0);
	return 0;
}
