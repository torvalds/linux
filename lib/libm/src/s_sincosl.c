/*-
 * Copyright (c) 2007, 2010-2013 Steven G. Kargl
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
 *
 * s_sinl.c and s_cosl.c merged by Steven G. Kargl.
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <math.h>

#include "math_private.h"

#if LDBL_MANT_DIG == 64
#include "../ld80/k_sincosl.h"
#define	NX	3
#define	PREC	2
#elif LDBL_MANT_DIG == 113
#include "../ld128/k_sincosl.h"
#define	NX	5
#define	PREC	3
#else
#error "Unsupported long double format"
#endif

static const long double two24 = 1.67772160000000000000e+07L;

void
sincosl(long double x, long double *sn, long double *cs)
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

	/* Optimize the case where x is already within range. */
	if (z.e < M_PI_4) {
		/*
		 * If x = +-0 or x is a subnormal number, then sin(x) = x and
		 * cos(x) = 1.
		 */
		if (z.bits.ext_exp == 0) {
			*sn = x;
			*cs = 1;
		} else
			__kernel_sincosl(x, 0, 0, sn, cs);
		return;
	}

	/* If x = NaN or Inf, then sin(x) and cos(x) are NaN. */
	if (z.bits.ext_exp == 32767) {
		*sn = x - x;
		*cs = x - x;
		return;
	}

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
		__kernel_sincosl(hi, lo, 1, sn, cs);
		break;
	case 1:
		__kernel_sincosl(hi, lo, 1, cs, sn);
		*cs = -*cs;
		break;
	case 2:
		__kernel_sincosl(hi, lo, 1, sn, cs);
		*sn = -*sn;
		*cs = -*cs;
		break;
	default:
		__kernel_sincosl(hi, lo, 1, cs, sn);
		*sn = -*sn;
	}
}
