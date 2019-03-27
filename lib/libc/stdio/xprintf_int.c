/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005 Poul-Henning Kamp
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
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

#include <namespace.h>
#include <err.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <locale.h>
#include <stdint.h>
#include <assert.h>
#include <namespace.h>
#include <string.h>
#include <wchar.h>
#include <un-namespace.h>

#include "printf.h"

/* private stuff -----------------------------------------------------*/

union arg {
	int			intarg;
	u_int			uintarg;
	long			longarg;
	u_long			ulongarg;
	intmax_t 		intmaxarg;
	uintmax_t 		uintmaxarg;
};

/*
 * Macros for converting digits to letters and vice versa
 */
#define	to_char(n)	((n) + '0')

/* various globals ---------------------------------------------------*/

/*
 * The size of the buffer we use for integer conversions.
 * Technically, we would need the most space for base 10
 * conversions with thousands' grouping characters between
 * each pair of digits: 60 digits for 128 bit intmax_t.
 * Use a bit more for better alignment of stuff.
 */
#define	BUF	64

/* misc --------------------------------------------------------------*/

/*
 * Convert an unsigned long to ASCII for printf purposes, returning
 * a pointer to the first character of the string representation.
 * Octal numbers can be forced to have a leading zero; hex numbers
 * use the given digits.
 */
static char *
__ultoa(u_long val, char *endp, int base, const char *xdigs,
	int needgrp, char thousep, const char *grp)
{
	char *cp = endp;
	long sval;
	int ndig;

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
		ndig = 0;
		/*
		 * On many machines, unsigned arithmetic is harder than
		 * signed arithmetic, so we do at most one unsigned mod and
		 * divide; this is sufficient to reduce the range of
		 * the incoming value to where signed arithmetic works.
		 */
		if (val > LONG_MAX) {
			*--cp = to_char(val % 10);
			ndig++;
			sval = val / 10;
		} else
			sval = val;
		do {
			*--cp = to_char(sval % 10);
			ndig++;
			/*
			 * If (*grp == CHAR_MAX) then no more grouping
			 * should be performed.
			 */
			if (needgrp && ndig == *grp && *grp != CHAR_MAX
					&& sval > 9) {
				*--cp = thousep;
				ndig = 0;
				/*
				 * If (*(grp+1) == '\0') then we have to
				 * use *grp character (last grouping rule)
				 * for all next cases
				 */
				if (*(grp+1) != '\0')
					grp++;
			}
			sval /= 10;
		} while (sval != 0);
		break;

	case 8:
		do {
			*--cp = to_char(val & 7);
			val >>= 3;
		} while (val);
		break;

	case 16:
		do {
			*--cp = xdigs[val & 15];
			val >>= 4;
		} while (val);
		break;

	default:			/* oops */
		assert(base == 16);
	}
	return (cp);
}


/* Identical to __ultoa, but for intmax_t. */
static char *
__ujtoa(uintmax_t val, char *endp, int base, const char *xdigs, 
	int needgrp, char thousep, const char *grp)
{
	char *cp = endp;
	intmax_t sval;
	int ndig;

	switch (base) {
	case 10:
		if (val < 10) {
			*--cp = to_char(val % 10);
			return (cp);
		}
		ndig = 0;
		if (val > INTMAX_MAX) {
			*--cp = to_char(val % 10);
			ndig++;
			sval = val / 10;
		} else
			sval = val;
		do {
			*--cp = to_char(sval % 10);
			ndig++;
			/*
			 * If (*grp == CHAR_MAX) then no more grouping
			 * should be performed.
			 */
			if (needgrp && *grp != CHAR_MAX && ndig == *grp
					&& sval > 9) {
				*--cp = thousep;
				ndig = 0;
				/*
				 * If (*(grp+1) == '\0') then we have to
				 * use *grp character (last grouping rule)
				 * for all next cases
				 */
				if (*(grp+1) != '\0')
					grp++;
			}
			sval /= 10;
		} while (sval != 0);
		break;

	case 8:
		do {
			*--cp = to_char(val & 7);
			val >>= 3;
		} while (val);
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


/* 'd' ---------------------------------------------------------------*/

int
__printf_arginfo_int(const struct printf_info *pi, size_t n, int *argt)
{
	assert (n > 0);
	argt[0] = PA_INT;
	if (pi->is_ptrdiff)
		argt[0] |= PA_FLAG_PTRDIFF;
	else if (pi->is_size)
		argt[0] |= PA_FLAG_SIZE;
	else if (pi->is_long)
		argt[0] |= PA_FLAG_LONG;
	else if (pi->is_intmax)
		argt[0] |= PA_FLAG_INTMAX;
	else if (pi->is_quad)
		argt[0] |= PA_FLAG_QUAD;
	else if (pi->is_long_double)
		argt[0] |= PA_FLAG_LONG_LONG;
	else if (pi->is_short)
		argt[0] |= PA_FLAG_SHORT;
	else if (pi->is_char)
		argt[0] = PA_CHAR;
	return (1);
}

int
__printf_render_int(struct __printf_io *io, const struct printf_info *pi, const void *const *arg)
{
	const union arg *argp;
	char buf[BUF];
	char *p, *pe;
	char ns;
	int l, ngrp, rdx, sign, zext;
	const char *nalt, *digit;
	char thousands_sep;	/* locale specific thousands separator */
	const char *grouping;	/* locale specific numeric grouping rules */
	uintmax_t uu;
	int ret;

	ret = 0;
	nalt = NULL;
	digit = __lowercase_hex;
	ns = '\0';
	pe = buf + sizeof buf - 1;

	if (pi->group) {
		thousands_sep = *(localeconv()->thousands_sep);
		grouping = localeconv()->grouping;
		ngrp = 1;
	} else {
		thousands_sep = 0;
		grouping = NULL;
		ngrp = 0;
	}

	switch(pi->spec) {
	case 'd':
	case 'i':
		rdx = 10;
		sign = 1;
		break;
	case 'X':
		digit = __uppercase_hex;
		/*FALLTHOUGH*/
	case 'x':
		rdx = 16;
		sign = 0;
		break;
	case 'u':
	case 'U':
		rdx = 10;
		sign = 0;
		break;
	case 'o':
	case 'O':
		rdx = 8;
		sign = 0;
		break;
	default:
		fprintf(stderr, "pi->spec = '%c'\n", pi->spec);
		assert(1 == 0);
	}
	argp = arg[0];

	if (sign)
		ns = pi->showsign;

	if (pi->is_long_double || pi->is_quad || pi->is_intmax ||
	    pi->is_size || pi->is_ptrdiff) {
		if (sign && argp->intmaxarg < 0) {
			uu = -argp->intmaxarg;
			ns = '-';
		} else
			uu = argp->uintmaxarg;
	} else if (pi->is_long) {
		if (sign && argp->longarg < 0) {
			uu = (u_long)-argp->longarg;
			ns = '-';
		} else 
			uu = argp->ulongarg;
	} else if (pi->is_short) {
		if (sign && (short)argp->intarg < 0) {
			uu = -(short)argp->intarg;
			ns = '-';
		} else 
			uu = (unsigned short)argp->uintarg;
	} else if (pi->is_char) {
		if (sign && (signed char)argp->intarg < 0) {
			uu = -(signed char)argp->intarg;
			ns = '-';
		} else 
			uu = (unsigned char)argp->uintarg;
	} else {
		if (sign && argp->intarg < 0) {
			uu = (unsigned)-argp->intarg;
			ns = '-';
		} else
			uu = argp->uintarg;
	}
	if (uu <= ULONG_MAX)
		p = __ultoa(uu, pe, rdx, digit, ngrp, thousands_sep, grouping);
	else
		p = __ujtoa(uu, pe, rdx, digit, ngrp, thousands_sep, grouping);

	l = 0;
	if (uu == 0) {
		/*-
		 * ``The result of converting a zero value with an
		 * explicit precision of zero is no characters.''
		 *      -- ANSI X3J11
		 *
		 * ``The C Standard is clear enough as is.  The call
		 * printf("%#.0o", 0) should print 0.''
		 *      -- Defect Report #151
		 */
			;
		if (pi->prec == 0 && !(pi->alt && rdx == 8))
			p = pe;
	} else if (pi->alt) {
		if (rdx == 8) 
			*--p = '0';
		if (rdx == 16) {
			if (pi->spec == 'x')
				nalt = "0x";
			else
				nalt = "0X";
			l += 2;
		}
	}
	l += pe - p;
	if (ns)
		l++;

	/*-
	 * ``... diouXx conversions ... if a precision is
	 * specified, the 0 flag will be ignored.''
	 *      -- ANSI X3J11
	 */
	if (pi->prec > (pe - p))
		zext = pi->prec - (pe - p);
	else if (pi->prec != -1)
		zext = 0;
	else if (pi->pad == '0' && pi->width > l && !pi->left)
		zext = pi->width - l;
	else
		zext = 0;

	l += zext;

	while (zext > 0 && p > buf) {
		*--p = '0';
		zext--;
	}

	if (l < BUF) {
		if (ns) {
			*--p = ns;
		} else if (nalt != NULL) {
			*--p = nalt[1];
			*--p = nalt[0];
		}
		if (pi->width > (pe - p) && !pi->left) {
			l = pi->width - (pe - p);
			while (l > 0 && p > buf) {
				*--p = ' ';
				l--;
			}
			if (l)
				ret += __printf_pad(io, l, 0);
		}
	} else {
		if (!pi->left && pi->width > l)
			ret += __printf_pad(io, pi->width - l, 0);
		if (ns != '\0')
			ret += __printf_puts(io, &ns, 1);
		else if (nalt != NULL)
			ret += __printf_puts(io, nalt, 2);
		if (zext > 0)
			ret += __printf_pad(io, zext, 1);
	}
	
	ret += __printf_puts(io, p, pe - p);
	if (pi->width > ret && pi->left) 
		ret += __printf_pad(io, pi->width - ret, 0);
	__printf_flush(io);
	return (ret);
}

/* 'p' ---------------------------------------------------------------*/

int
__printf_arginfo_ptr(const struct printf_info *pi __unused, size_t n, int *argt)
{

	assert (n > 0);
	argt[0] = PA_POINTER;
	return (1);
}

int
__printf_render_ptr(struct __printf_io *io, const struct printf_info *pi, const void *const *arg)
{
	struct printf_info p2;
	uintmax_t u;
	const void *p;

	/*-
	 * ``The argument shall be a pointer to void.  The
	 * value of the pointer is converted to a sequence
	 * of printable characters, in an implementation-
	 * defined manner.''
	 *      -- ANSI X3J11
	 */
	u = (uintmax_t)(uintptr_t) *((void **)arg[0]);
	p2 = *pi;

	p2.spec = 'x';
	p2.alt = 1;
	p2.is_long_double = 1;
	p = &u;
	return (__printf_render_int(io, &p2, &p));
}
