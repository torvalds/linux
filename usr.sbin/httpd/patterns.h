/*	$OpenBSD: patterns.h,v 1.3 2015/12/12 19:59:43 mmcc Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
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

#ifndef PATTERNS_H
#define PATTERNS_H

#include <sys/types.h>

#define MAXCAPTURES	32	/* Max no. of allowed captures in pattern */
#define MAXCCALLS	200	/* Max recusion depth in pattern matching */
#define MAXREPETITION	0xfffff	/* Max for repetition items */

struct str_find {
	off_t		 sm_so;	/* start offset of match */
	off_t		 sm_eo;	/* end offset of match */
};

struct str_match {
	char		**sm_match; /* allocated array of matched strings */
	unsigned int	 sm_nmatch; /* number of elements in array */
};

__BEGIN_DECLS
int	 str_find(const char *, const char *, struct str_find *, size_t,
	    const char **);
int	 str_match(const char *, const char *, struct str_match *,
	    const char **);
void	 str_match_free(struct str_match *);
__END_DECLS

#endif /* PATTERNS_H */
