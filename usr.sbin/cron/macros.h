/*	$OpenBSD: macros.h,v 1.16 2023/05/06 23:06:27 millert Exp $	*/

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define CRON_VERSION	"V5.0"

#define TRUE		1
#define FALSE		0

#define READ_PIPE	0	/* which end of a pipe pair do you read? */
#define WRITE_PIPE	1	/*   or write to? */
#define	MAX_COMMAND	1000	/* max length of internally generated cmd */
#define	MAX_ENVSTR	1000	/* max length of envvar=value\0 strings */
#define	MAX_TEMPSTR	100	/* obvious */
#define	MAX_UNAME	(_PW_NAME_LEN+1)	/* max length of username, should be overkill */

#define	MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

#define	Skip_Blanks(c, f) \
			while (c == '\t' || c == ' ') \
				c = get_char(f);

#define	Skip_Nonblanks(c, f) \
			while (c!='\t' && c!=' ' && c!='\n' && c != EOF) \
				c = get_char(f);

#define	Set_LineNum(ln)	{ LineNumber = ln; }

/* Data values used on cron socket */
#define	RELOAD_CRON	0x2
#define	RELOAD_AT	0x4

#define	get_gmtoff(c, t)        ((t)->tm_gmtoff)

#define	SECONDS_PER_MINUTE	60

#define	FIRST_MINUTE	0
#define	LAST_MINUTE	59
#define	MINUTE_COUNT	(LAST_MINUTE - FIRST_MINUTE + 1)

#define	FIRST_HOUR	0
#define	LAST_HOUR	23
#define	HOUR_COUNT	(LAST_HOUR - FIRST_HOUR + 1)

#define	FIRST_DOM	1
#define	LAST_DOM	31
#define	DOM_COUNT	(LAST_DOM - FIRST_DOM + 1)

#define	FIRST_MONTH	1
#define	LAST_MONTH	12
#define	MONTH_COUNT	(LAST_MONTH - FIRST_MONTH + 1)

/* note on DOW: 0 and 7 are both Sunday, for compatibility reasons. */
#define	FIRST_DOW	0
#define	LAST_DOW	7
#define	DOW_COUNT	(LAST_DOW - FIRST_DOW + 1)
