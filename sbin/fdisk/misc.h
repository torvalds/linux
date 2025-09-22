/*	$OpenBSD: misc.h,v 1.52 2025/07/31 13:37:06 krw Exp $	*/

/*
 * Copyright (c) 1997 Tobias Weingartner
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

struct unit_type {
	char		*ut_abbr;
	int64_t		 ut_conversion;
	char		*ut_lname;
};

#ifndef nitems
#define	nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

#define	WHITESPACE	" \f\n\r\t\v"
#define	UNTRIMMED	0
#define	TRIMMED		1
#define	LINEBUFSZ	100

extern int	verbosity;

double		 units_size(const char *, const uint64_t,
    const struct unit_type **);
void		 string_from_line(char *, const size_t, const int);
int		 ask_yn(const char *);
int		 hex_octet(char *);
uint64_t	 getuint64(const char *, uint64_t, const uint64_t,
    const uint64_t);

uint32_t	 string_to_uuid(const char *, struct uuid *);
