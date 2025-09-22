/*	$OpenBSD: s_logbl.c,v 1.1 2008/12/09 20:00:35 martynas Exp $	*/
/*
 * From: @(#)s_ilogb.c 5.1 93/09/24
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include <sys/types.h>
#include <machine/ieee.h>
#include <float.h>
#include <limits.h>
#include <math.h>

long double
logbl(long double x)
{
	union {
		long double e;
		struct ieee_ext bits;
	} u;
	unsigned long m;
	int b;

	u.e = x;
	if (u.bits.ext_exp == 0) {
		if ((u.bits.ext_fracl
#ifdef EXT_FRACLMBITS
			| u.bits.ext_fraclm
#endif /* EXT_FRACLMBITS */
#ifdef EXT_FRACHMBITS
			| u.bits.ext_frachm
#endif /* EXT_FRACHMBITS */
			| u.bits.ext_frach) == 0) {	/* x == 0 */
			u.bits.ext_sign = 1;
			return (1.0L / u.e);
		}
		/* denormalized */
		if (u.bits.ext_frach == 0
#ifdef EXT_FRACHMBITS
			&& u.bits.ext_frachm == 0
#endif
			) {
			m = 1lu << (EXT_FRACLBITS - 1);
			for (b = EXT_FRACHBITS; !(u.bits.ext_fracl & m); m >>= 1)
				b++;
#if defined(EXT_FRACHMBITS) && defined(EXT_FRACLMBITS)
			m = 1lu << (EXT_FRACLMBITS - 1);
			for (b += EXT_FRACHMBITS; !(u.bits.ext_fraclm & m);
				m >>= 1)
				b++;
#endif /* defined(EXT_FRACHMBITS) && defined(EXT_FRACLMBITS) */
		} else {
			m = 1lu << (EXT_FRACHBITS - 1);
			for (b = 0; !(u.bits.ext_frach & m); m >>= 1)
				b++;
#ifdef EXT_FRACHMBITS
			m = 1lu << (EXT_FRACHMBITS - 1);
			for (; !(u.bits.ext_frachm & m); m >>= 1)
				b++;
#endif /* EXT_FRACHMBITS */
		}
#ifdef EXT_IMPLICIT_NBIT
		b++;
#endif
		return ((long double)(LDBL_MIN_EXP - b - 1));
	}
	if (u.bits.ext_exp < (LDBL_MAX_EXP << 1) - 1)	/* normal */
		return ((long double)(u.bits.ext_exp - LDBL_MAX_EXP + 1));
	else						/* +/- inf or nan */
		return (x * x);
}
