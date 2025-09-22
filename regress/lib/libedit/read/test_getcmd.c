/*	$OpenBSD: test_getcmd.c,v 1.8 2017/07/05 15:31:45 bluhm Exp $	*/
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
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "read.c"
#include "glue.c"

#define N_KEYS 256

/*
 * Unit test steering program for editline/read.c, read_getcmd().
 */

int
main()
{
	EditLine	 el;
	struct macros	*ma;
	int		 irc;
	wchar_t		 ch;
	el_action_t	 cmdnum;

	if (setlocale(LC_CTYPE, "") == NULL)
		err(1, "setlocale");

	el.el_flags = CHARSET_IS_UTF8;
	el.el_infd = STDIN_FILENO;
	el.el_state.metanext = 0;

	el.el_map.alt = NULL;
	if ((el.el_map.key = calloc(N_KEYS, sizeof(el_action_t))) == NULL)
		err(1, NULL);
	el.el_map.key[(unsigned char)'c'] = ED_SEQUENCE_LEAD_IN;
	el.el_map.key[(unsigned char)'i'] = ED_INSERT;
	el.el_map.key[(unsigned char)'s'] = ED_SEQUENCE_LEAD_IN;
	el.el_map.current = el.el_map.key;
	if ((el.el_signal = calloc(1, sizeof(*el.el_signal))) == NULL)
		err(1, NULL);

	if (read_init(&el) != 0)
		err(1, "read_init");
	ma = &el.el_read->macros;
	el.el_read->read_errno = ENOMSG;

	do {
		irc = read_getcmd(&el, &cmdnum, &ch);
		switch (irc) {
		case 0:
			fputs("OK ", stdout);
			switch (cmdnum) {
			case ED_COMMAND:
				fputs("command", stdout);
				break;
			case ED_INSERT:
				fputs("insert", stdout);
				break;
			default:
				printf("cmdnum=%u", cmdnum);
				break;
			}
			printf(" L'%lc'", ch);
			break;
		case -1:
			fputs("EOF", stdout);
			break;
		default:
			printf("ret(%d)", irc);
			break;
		}
		if (el.el_read->read_errno != ENOMSG)
			printf(" read_errno=%d", el.el_read->read_errno);
		if (ma->level > -1)
			printf(" macro[%d]=%ls(%d)", ma->level,
			    *ma->macro, ma->offset);
		putchar('\n');
	} while (irc == 0);

	return 0;
}
