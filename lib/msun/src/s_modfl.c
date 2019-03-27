/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2007 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Derived from s_modf.c, which has the following Copyright:
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * $FreeBSD$
 */

#include <float.h>
#include <math.h>
#include <sys/types.h>

#include "fpmath.h"

#if LDBL_MANL_SIZE > 32
#define	MASK	((uint64_t)-1)
#else
#define	MASK	((uint32_t)-1)
#endif
/* Return the last n bits of a word, representing the fractional part. */
#define	GETFRAC(bits, n)	((bits) & ~(MASK << (n)))
/* The number of fraction bits in manh, not counting the integer bit */
#define	HIBITS	(LDBL_MANT_DIG - LDBL_MANL_SIZE)

static const long double zero[] = { 0.0L, -0.0L };

long double
modfl(long double x, long double *iptr)
{
	union IEEEl2bits ux;
	int e;

	ux.e = x;
	e = ux.bits.exp - LDBL_MAX_EXP + 1;
	if (e < HIBITS) {			/* Integer part is in manh. */
		if (e < 0) {			/* |x|<1 */
			*iptr = zero[ux.bits.sign];
			return (x);
		} else {
			if ((GETFRAC(ux.bits.manh, HIBITS - 1 - e) |
			     ux.bits.manl) == 0) {	/* X is an integer. */
				*iptr = x;
				return (zero[ux.bits.sign]);
			} else {
				/* Clear all but the top e+1 bits. */
				ux.bits.manh >>= HIBITS - 1 - e;
				ux.bits.manh <<= HIBITS - 1 - e;
				ux.bits.manl = 0;
				*iptr = ux.e;
				return (x - ux.e);
			}
		}
	} else if (e >= LDBL_MANT_DIG - 1) {	/* x has no fraction part. */
		*iptr = x;
		if (x != x)			/* Handle NaNs. */
			return (x);
		return (zero[ux.bits.sign]);
	} else {				/* Fraction part is in manl. */
		if (GETFRAC(ux.bits.manl, LDBL_MANT_DIG - 1 - e) == 0) {
			/* x is integral. */
			*iptr = x;
			return (zero[ux.bits.sign]);
		} else {
			/* Clear all but the top e+1 bits. */
			ux.bits.manl >>= LDBL_MANT_DIG - 1 - e;
			ux.bits.manl <<= LDBL_MANT_DIG - 1 - e;
			*iptr = ux.e;
			return (x - ux.e);
		}
	}
}
