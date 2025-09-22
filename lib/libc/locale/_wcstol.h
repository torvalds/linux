/*	$OpenBSD: _wcstol.h,v 1.3 2015/10/01 02:32:07 guenther Exp $	*/
/* $NetBSD: _wcstol.h,v 1.2 2003/08/07 16:43:03 agc Exp $ */

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 * Original version ID:
 * @(#)strtol.c	8.1 (Berkeley) 6/4/93
 * NetBSD: wcstol.c,v 1.1 2001/09/27 16:30:36 yamt Exp
 * Citrus: xpg4dl/FreeBSD/lib/libc/locale/wcstol.c,v 1.2 2001/09/21 16:11:41 yamt Exp
 */

/*
 * function template for wcstol, wcstoll and wcstoimax.
 *
 * parameters:
 *	FUNCNAME : function name
 *      int_type : return type
 *      MIN_VALUE : lower limit of the return type
 *      MAX_VALUE : upper limit of the return type
 */

int_type
FUNCNAME(const wchar_t *nptr, wchar_t **endptr, int base)
{
	const wchar_t *s;
	int_type acc, cutoff;
	wint_t wc;
	int i;
	int neg, any, cutlim;

	/* check base value */
	if (base && (base < 2 || base > 36)) {
		errno = EINVAL;
		return 0;
	}

	/*
	 * Skip white space and pick up leading +/- sign if any.
	 * If base is 0, allow 0x for hex and 0 for octal, else
	 * assume decimal; if base is already 16, allow 0x.
	 */
	s = nptr;
	do {
		wc = (wchar_t) *s++;
	} while (iswspace(wc));
	if (wc == L'-') {
		neg = 1;
		wc = *s++;
	} else {
		neg = 0;
		if (wc == L'+')
			wc = *s++;
	}
	if ((base == 0 || base == 16) &&
	    wc == L'0' && (*s == L'x' || *s == L'X')) {
		wc = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = wc == L'0' ? 8 : 10;

	/*
	 * See strtol for comments as to the logic used.
	 */
	cutoff = neg ? MIN_VALUE : MAX_VALUE;
	cutlim = (int)(cutoff % base);
	cutoff /= base;
	if (neg) {
		if (cutlim > 0) {
			cutlim -= base;
			cutoff += 1;
		}
		cutlim = -cutlim;
	}
	for (acc = 0, any = 0;; wc = (wchar_t) *s++) {
		i = wctoint(wc);
		if (i == -1)
			break;
		if (i >= base)
			break;
		if (any < 0)
			continue;
		if (neg) {
			if (acc < cutoff || (acc == cutoff && i > cutlim)) {
				any = -1;
				acc = MIN_VALUE;
				errno = ERANGE;
			} else {
				any = 1;
				acc *= base;
				acc -= i;
			}
		} else {
			if (acc > cutoff || (acc == cutoff && i > cutlim)) {
				any = -1;
				acc = MAX_VALUE;
				errno = ERANGE;
			} else {
				any = 1;
				acc *= base;
				acc += i;
			}
		}
	}
	if (endptr != 0)
		*endptr = (wchar_t *)(any ? s - 1 : nptr);
	return (acc);
}
DEF_STRONG(FUNCNAME);
