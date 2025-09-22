/*	$OpenBSD: frexp.c,v 1.10 2013/07/03 04:46:36 espie Exp $	*/

/*-
 * Copyright (c) 2004 David Schultz <das@FreeBSD.ORG>
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
 * $FreeBSD: frexp.c,v 1.1 2004/07/18 21:23:39 das Exp $
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <math.h>

double
frexp(double v, int *ex)
{
	union  {
		double v;
		struct ieee_double s;
	} u;

	u.v = v;
	switch (u.s.dbl_exp) {
	case 0:		/* 0 or subnormal */
		if ((u.s.dbl_fracl | u.s.dbl_frach) == 0) {
			*ex = 0;
		} else {
			/*
			 * The power of 2 is arbitrary, any value from 54 to
			 * 1024 will do.
			 */
			u.v *= 0x1.0p514;
			*ex = u.s.dbl_exp - (DBL_EXP_BIAS - 1 + 514);
			u.s.dbl_exp = DBL_EXP_BIAS - 1;
		}
		break;
	case DBL_EXP_INFNAN:	/* Inf or NaN; value of *ex is unspecified */
		break;
	default:	/* normal */
		*ex = u.s.dbl_exp - (DBL_EXP_BIAS - 1);
		u.s.dbl_exp = DBL_EXP_BIAS - 1;
		break;
	}
	return (u.v);
}

#if	LDBL_MANT_DIG == DBL_MANT_DIG
__strong_alias(frexpl, frexp);
#endif	/* LDBL_MANT_DIG == DBL_MANT_DIG */
