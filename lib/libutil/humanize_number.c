/*	$NetBSD: humanize_number.c,v 1.14 2008/04/28 20:22:59 martin Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, 1998, 1999, 2002 The NetBSD Foundation, Inc.
 * Copyright 2013 John-Mark Gurney <jmg@FreeBSD.org>
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center, by Luke Mewburn and by Tomas Svensson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <libutil.h>

static const int maxscale = 6;

int
humanize_number(char *buf, size_t len, int64_t quotient,
    const char *suffix, int scale, int flags)
{
	const char *prefixes, *sep;
	int	i, r, remainder, s1, s2, sign;
	int	divisordeccut;
	int64_t	divisor, max;
	size_t	baselen;

	/* Since so many callers don't check -1, NUL terminate the buffer */
	if (len > 0)
		buf[0] = '\0';

	/* validate args */
	if (buf == NULL || suffix == NULL)
		return (-1);
	if (scale < 0)
		return (-1);
	else if (scale > maxscale &&
	    ((scale & ~(HN_AUTOSCALE|HN_GETSCALE)) != 0))
		return (-1);
	if ((flags & HN_DIVISOR_1000) && (flags & HN_IEC_PREFIXES))
		return (-1);

	/* setup parameters */
	remainder = 0;

	if (flags & HN_IEC_PREFIXES) {
		baselen = 2;
		/*
		 * Use the prefixes for power of two recommended by
		 * the International Electrotechnical Commission
		 * (IEC) in IEC 80000-3 (i.e. Ki, Mi, Gi...).
		 *
		 * HN_IEC_PREFIXES implies a divisor of 1024 here
		 * (use of HN_DIVISOR_1000 would have triggered
		 * an assertion earlier).
		 */
		divisor = 1024;
		divisordeccut = 973;	/* ceil(.95 * 1024) */
		if (flags & HN_B)
			prefixes = "B\0\0Ki\0Mi\0Gi\0Ti\0Pi\0Ei";
		else
			prefixes = "\0\0\0Ki\0Mi\0Gi\0Ti\0Pi\0Ei";
	} else {
		baselen = 1;
		if (flags & HN_DIVISOR_1000) {
			divisor = 1000;
			divisordeccut = 950;
			if (flags & HN_B)
				prefixes = "B\0\0k\0\0M\0\0G\0\0T\0\0P\0\0E";
			else
				prefixes = "\0\0\0k\0\0M\0\0G\0\0T\0\0P\0\0E";
		} else {
			divisor = 1024;
			divisordeccut = 973;	/* ceil(.95 * 1024) */
			if (flags & HN_B)
				prefixes = "B\0\0K\0\0M\0\0G\0\0T\0\0P\0\0E";
			else
				prefixes = "\0\0\0K\0\0M\0\0G\0\0T\0\0P\0\0E";
		}
	}

#define	SCALE2PREFIX(scale)	(&prefixes[(scale) * 3])

	if (quotient < 0) {
		sign = -1;
		quotient = -quotient;
		baselen += 2;		/* sign, digit */
	} else {
		sign = 1;
		baselen += 1;		/* digit */
	}
	if (flags & HN_NOSPACE)
		sep = "";
	else {
		sep = " ";
		baselen++;
	}
	baselen += strlen(suffix);

	/* Check if enough room for `x y' + suffix + `\0' */
	if (len < baselen + 1)
		return (-1);

	if (scale & (HN_AUTOSCALE | HN_GETSCALE)) {
		/* See if there is additional columns can be used. */
		for (max = 1, i = len - baselen; i-- > 0;)
			max *= 10;

		/*
		 * Divide the number until it fits the given column.
		 * If there will be an overflow by the rounding below,
		 * divide once more.
		 */
		for (i = 0;
		    (quotient >= max || (quotient == max - 1 &&
		    (remainder >= divisordeccut || remainder >=
		    divisor / 2))) && i < maxscale; i++) {
			remainder = quotient % divisor;
			quotient /= divisor;
		}

		if (scale & HN_GETSCALE)
			return (i);
	} else {
		for (i = 0; i < scale && i < maxscale; i++) {
			remainder = quotient % divisor;
			quotient /= divisor;
		}
	}

	/* If a value <= 9.9 after rounding and ... */
	/*
	 * XXX - should we make sure there is enough space for the decimal
	 * place and if not, don't do HN_DECIMAL?
	 */
	if (((quotient == 9 && remainder < divisordeccut) || quotient < 9) &&
	    i > 0 && flags & HN_DECIMAL) {
		s1 = (int)quotient + ((remainder * 10 + divisor / 2) /
		    divisor / 10);
		s2 = ((remainder * 10 + divisor / 2) / divisor) % 10;
		r = snprintf(buf, len, "%d%s%d%s%s%s",
		    sign * s1, localeconv()->decimal_point, s2,
		    sep, SCALE2PREFIX(i), suffix);
	} else
		r = snprintf(buf, len, "%" PRId64 "%s%s%s",
		    sign * (quotient + (remainder + divisor / 2) / divisor),
		    sep, SCALE2PREFIX(i), suffix);

	return (r);
}
