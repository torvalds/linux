/*	$OpenBSD: ecvt.c,v 1.11 2019/01/25 00:19:25 millert Exp $	*/

/*
 * Copyright (c) 2002, 2006 Todd C. Miller <millert@openbsd.org>
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gdtoa.h"

static char *__cvt(double, int, int *, int *, int, int);

static char *
__cvt(double value, int ndigit, int *decpt, int *sign, int fmode, int pad)
{
	static char *s;
	char *p, *rve, c;
	size_t siz;

	if (ndigit == 0) {
		*sign = value < 0.0;
		*decpt = 0;
		return ("");
	}

	free(s);
	s = NULL;

	if (ndigit < 0)
		siz = -ndigit + 1;
	else
		siz = ndigit + 1;


	/* __dtoa() doesn't allocate space for 0 so we do it by hand */
	if (value == 0.0) {
		*decpt = 1 - fmode;	/* 1 for 'e', 0 for 'f' */
		*sign = 0;
		if ((rve = s = malloc(siz)) == NULL)
			return(NULL);
		*rve++ = '0';
		*rve = '\0';
	} else {
		p = __dtoa(value, fmode + 2, ndigit, decpt, sign, &rve);
		if (p == NULL)
			return (NULL);
		if (*decpt == 9999) {
			/* Infinity or Nan, convert to inf or nan like printf */
			*decpt = 0;
			c = *p;
			__freedtoa(p);
			return(c == 'I' ? "inf" : "nan");
		}
		/* Make a local copy and adjust rve to be in terms of s */
		if (pad && fmode)
			siz += *decpt;
		if ((s = malloc(siz)) == NULL) {
			__freedtoa(p);
			return(NULL);
		}
		(void) strlcpy(s, p, siz);
		rve = s + (rve - p);
		__freedtoa(p);
	}

	/* Add trailing zeros */
	if (pad) {
		siz -= rve - s;
		while (--siz)
			*rve++ = '0';
		*rve = '\0';
	}

	return(s);
}

char *
ecvt(double value, int ndigit, int *decpt, int *sign)
{
	return(__cvt(value, ndigit, decpt, sign, 0, 1));
}

char *
fcvt(double value, int ndigit, int *decpt, int *sign)
{
	return(__cvt(value, ndigit, decpt, sign, 1, 1));
}
