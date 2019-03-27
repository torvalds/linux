/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 * Portions of this software were developed by David Chisnall
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * This file defines common routines used by both printf and wprintf.
 * You must define CHAR to either char or wchar_t prior to including this.
 */


#ifndef NO_FLOATING_POINT

#define	dtoa		__dtoa
#define	freedtoa	__freedtoa

#include <float.h>
#include <math.h>
#include "floatio.h"
#include "gdtoa.h"

#define	DEFPREC		6

static int exponent(CHAR *, int, CHAR);

#endif /* !NO_FLOATING_POINT */

static CHAR	*__ujtoa(uintmax_t, CHAR *, int, int, const char *);
static CHAR	*__ultoa(u_long, CHAR *, int, int, const char *);

#define NIOV 8
struct io_state {
	FILE *fp;
	struct __suio uio;	/* output information: summary */
	struct __siov iov[NIOV];/* ... and individual io vectors */
};

static inline void
io_init(struct io_state *iop, FILE *fp)
{

	iop->uio.uio_iov = iop->iov;
	iop->uio.uio_resid = 0;
	iop->uio.uio_iovcnt = 0;
	iop->fp = fp;
}

/*
 * WARNING: The buffer passed to io_print() is not copied immediately; it must
 * remain valid until io_flush() is called.
 */
static inline int
io_print(struct io_state *iop, const CHAR * __restrict ptr, int len, locale_t locale)
{

	iop->iov[iop->uio.uio_iovcnt].iov_base = (char *)ptr;
	iop->iov[iop->uio.uio_iovcnt].iov_len = len;
	iop->uio.uio_resid += len;
	if (++iop->uio.uio_iovcnt >= NIOV)
		return (__sprint(iop->fp, &iop->uio, locale));
	else
		return (0);
}

/*
 * Choose PADSIZE to trade efficiency vs. size.  If larger printf
 * fields occur frequently, increase PADSIZE and make the initialisers
 * below longer.
 */
#define	PADSIZE	16		/* pad chunk size */
static const CHAR blanks[PADSIZE] =
{' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' '};
static const CHAR zeroes[PADSIZE] =
{'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};

/*
 * Pad with blanks or zeroes. 'with' should point to either the blanks array
 * or the zeroes array.
 */
static inline int
io_pad(struct io_state *iop, int howmany, const CHAR * __restrict with,
		locale_t locale)
{
	int n;

	while (howmany > 0) {
		n = (howmany >= PADSIZE) ? PADSIZE : howmany;
		if (io_print(iop, with, n, locale))
			return (-1);
		howmany -= n;
	}
	return (0);
}

/*
 * Print exactly len characters of the string spanning p to ep, truncating
 * or padding with 'with' as necessary.
 */
static inline int
io_printandpad(struct io_state *iop, const CHAR *p, const CHAR *ep,
	       int len, const CHAR * __restrict with, locale_t locale)
{
	int p_len;

	p_len = ep - p;
	if (p_len > len)
		p_len = len;
	if (p_len > 0) {
		if (io_print(iop, p, p_len, locale))
			return (-1);
	} else {
		p_len = 0;
	}
	return (io_pad(iop, len - p_len, with, locale));
}

static inline int
io_flush(struct io_state *iop, locale_t locale)
{

	return (__sprint(iop->fp, &iop->uio, locale));
}

/*
 * Convert an unsigned long to ASCII for printf purposes, returning
 * a pointer to the first character of the string representation.
 * Octal numbers can be forced to have a leading zero; hex numbers
 * use the given digits.
 */
static CHAR *
__ultoa(u_long val, CHAR *endp, int base, int octzero, const char *xdigs)
{
	CHAR *cp = endp;
	long sval;

	/*
	 * Handle the three cases separately, in the hope of getting
	 * better/faster code.
	 */
	switch (base) {
	case 10:
		if (val < 10) {	/* many numbers are 1 digit */
			*--cp = to_char(val);
			return (cp);
		}
		/*
		 * On many machines, unsigned arithmetic is harder than
		 * signed arithmetic, so we do at most one unsigned mod and
		 * divide; this is sufficient to reduce the range of
		 * the incoming value to where signed arithmetic works.
		 */
		if (val > LONG_MAX) {
			*--cp = to_char(val % 10);
			sval = val / 10;
		} else
			sval = val;
		do {
			*--cp = to_char(sval % 10);
			sval /= 10;
		} while (sval != 0);
		break;

	case 8:
		do {
			*--cp = to_char(val & 7);
			val >>= 3;
		} while (val);
		if (octzero && *cp != '0')
			*--cp = '0';
		break;

	case 16:
		do {
			*--cp = xdigs[val & 15];
			val >>= 4;
		} while (val);
		break;

	default:			/* oops */
		abort();
	}
	return (cp);
}

/* Identical to __ultoa, but for intmax_t. */
static CHAR *
__ujtoa(uintmax_t val, CHAR *endp, int base, int octzero, const char *xdigs)
{
	CHAR *cp = endp;
	intmax_t sval;

	/* quick test for small values; __ultoa is typically much faster */
	/* (perhaps instead we should run until small, then call __ultoa?) */
	if (val <= ULONG_MAX)
		return (__ultoa((u_long)val, endp, base, octzero, xdigs));
	switch (base) {
	case 10:
		if (val < 10) {
			*--cp = to_char(val % 10);
			return (cp);
		}
		if (val > INTMAX_MAX) {
			*--cp = to_char(val % 10);
			sval = val / 10;
		} else
			sval = val;
		do {
			*--cp = to_char(sval % 10);
			sval /= 10;
		} while (sval != 0);
		break;

	case 8:
		do {
			*--cp = to_char(val & 7);
			val >>= 3;
		} while (val);
		if (octzero && *cp != '0')
			*--cp = '0';
		break;

	case 16:
		do {
			*--cp = xdigs[val & 15];
			val >>= 4;
		} while (val);
		break;

	default:
		abort();
	}
	return (cp);
}

#ifndef NO_FLOATING_POINT

static int
exponent(CHAR *p0, int exp, CHAR fmtch)
{
	CHAR *p, *t;
	CHAR expbuf[MAXEXPDIG];

	p = p0;
	*p++ = fmtch;
	if (exp < 0) {
		exp = -exp;
		*p++ = '-';
	}
	else
		*p++ = '+';
	t = expbuf + MAXEXPDIG;
	if (exp > 9) {
		do {
			*--t = to_char(exp % 10);
		} while ((exp /= 10) > 9);
		*--t = to_char(exp);
		for (; t < expbuf + MAXEXPDIG; *p++ = *t++);
	}
	else {
		/*
		 * Exponents for decimal floating point conversions
		 * (%[eEgG]) must be at least two characters long,
		 * whereas exponents for hexadecimal conversions can
		 * be only one character long.
		 */
		if (fmtch == 'e' || fmtch == 'E')
			*p++ = '0';
		*p++ = to_char(exp);
	}
	return (p - p0);
}

#endif /* !NO_FLOATING_POINT */
