/* $FreeBSD$ */
/* $NetBSD: _strtoul.h,v 1.1 2008/08/20 12:42:26 joerg Exp $ */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
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
 * NetBSD: src/lib/libc/locale/_wcstoul.h,v 1.2 2003/08/07 16:43:03 agc Exp
 */

/*
 * function template for strtoul, strtoull and strtoumax.
 *
 * parameters:
 *	_FUNCNAME  : function name
 *      __UINT     : return type
 *      __UINT_MAX : upper limit of the return type
 */

__UINT
_FUNCNAME(const char *nptr, char **endptr, int base)
{
	const char *s;
	__UINT acc, cutoff;
	unsigned char c;
	int any, cutlim, i, neg;

	/* check base value */
	if (base && (base < 2 || base > 36)) {
#if !defined(_KERNEL) && !defined(_STANDALONE)
		errno = EINVAL;
		return (0);
#else
		panic("%s: invalid base %d", __func__, base);
#endif
	}

	/*
	 * Skip white space and pick up leading +/- sign if any.
	 * If base is 0, allow 0x for hex and 0 for octal, else
	 * assume decimal; if base is already 16, allow 0x.
	 */
	s = nptr;
	do {
		c = *s++;
	} while (isspace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else {
		neg = 0;
		if (c == '+')
			c = *s++;
	}
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = (c == '0' ? 8 : 10);

	/*
	 * See strtol for comments as to the logic used.
	 */
	cutoff = __UINT_MAX / (__UINT)base;
	cutlim = (int)(__UINT_MAX % (__UINT)base);
	for (acc = 0, any = 0;; c = *s++) {
		if (isdigit(c))
			i = c - '0';
		else if (isalpha(c))
			i = c - (isupper(c) ? 'A' - 10 : 'a' - 10);
		else
			break;
		if (i >= base)
			break;
		if (any < 0)
			continue;
		if (acc > cutoff || (acc == cutoff && i > cutlim)) {
			acc = __UINT_MAX;
#if !defined(_KERNEL) && !defined(_STANDALONE)
			any = -1;
			errno = ERANGE;
#else
			any = 0;
			break;
#endif
		} else {
			any = 1;
			acc *= (__UINT)base;
			acc += i;
		}
	}
	if (neg && any > 0)
		acc = -acc;
	if (endptr != NULL)
		/* LINTED interface specification */
		*endptr = __DECONST(void *, any ? s - 1 : nptr);
	return (acc);
}
