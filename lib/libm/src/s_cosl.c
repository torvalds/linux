/*	$OpenBSD: s_cosl.c,v 1.2 2016/09/12 19:47:02 guenther Exp $	*/
/*-
 * Copyright (c) 2007 Steven G. Kargl
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Compute cos(x) for x where x is reduced to y = x - k * pi / 2.
 * Limited testing on pseudorandom numbers drawn within [-2e8:4e8] shows
 * an accuracy of <= 0.7412 ULP.
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <math.h>

#include "math_private.h"

#if LDBL_MANT_DIG == 64
#define	NX	3
#define	PREC	2
#elif LDBL_MANT_DIG == 113
#define	NX	5
#define	PREC	3
#else
#error "Unsupported long double format"
#endif

static const long double two24 = 1.67772160000000000000e+07L;

long double
cosl(long double x)
{
	union {
		long double e;
		struct ieee_ext bits;
	} z;
	int i, e0;
	double xd[NX], yd[PREC];
	long double hi, lo;

	z.e = x;
	z.bits.ext_sign = 0;

	/* If x = +-0 or x is a subnormal number, then cos(x) = 1 */
	if (z.bits.ext_exp == 0)
		return (1.0);

	/* If x = NaN or Inf, then cos(x) = NaN. */
	if (z.bits.ext_exp == 32767)
		return ((x - x) / (x - x));

	/* Optimize the case where x is already within range. */
	if (z.e < M_PI_4)
		return (__kernel_cosl(z.e, 0));

	/* Split z.e into a 24-bit representation. */
	e0 = ilogbl(z.e) - 23;
	z.e = scalbnl(z.e, -e0);
	for (i = 0; i < NX; i++) {
		xd[i] = (double)((int32_t)z.e);
		z.e = (z.e - xd[i]) * two24;
	}
	
	/* yd contains the pieces of xd rem pi/2 such that |yd| < pi/4. */
	e0 = __kernel_rem_pio2(xd, yd, e0, NX, PREC);

#if PREC == 2
	hi = (long double)yd[0] + yd[1];
	lo = yd[1] - (hi - yd[0]);
#else /* PREC == 3 */
	long double t;
	t = (long double)yd[2] + yd[1];
	hi = t + yd[0];
	lo = yd[0] - (hi - t);
#endif

	switch (e0 & 3) {
	case 0:
	    hi = __kernel_cosl(hi, lo);
	    break;
	case 1:
	    hi = - __kernel_sinl(hi, lo, 1);
	    break;
	case 2:
	    hi = - __kernel_cosl(hi, lo);
	    break;
	case 3:
	    hi = __kernel_sinl(hi, lo, 1);
	    break;
	}
	
	return (hi);
}
DEF_STD(cosl);
