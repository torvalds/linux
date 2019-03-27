/* @(#)s_tanh.c 5.1 93/09/24 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Tanh(x)
 * Return the Hyperbolic Tangent of x
 *
 * Method :
 *				       x    -x
 *				      e  - e
 *	0. tanh(x) is defined to be -----------
 *				       x    -x
 *				      e  + e
 *	1. reduce x to non-negative by tanh(-x) = -tanh(x).
 *	2.  0      <= x <  2**-28 : tanh(x) := x with inexact if x != 0
 *					        -t
 *	    2**-28 <= x <  1      : tanh(x) := -----; t = expm1(-2x)
 *					       t + 2
 *						     2
 *	    1      <= x <  22     : tanh(x) := 1 - -----; t = expm1(2x)
 *						   t + 2
 *	    22     <= x <= INF    : tanh(x) := 1.
 *
 * Special cases:
 *	tanh(NaN) is NaN;
 *	only tanh(0)=0 is exact for finite argument.
 */

#include <float.h>

#include "math.h"
#include "math_private.h"

static const volatile double tiny = 1.0e-300;
static const double one = 1.0, two = 2.0, huge = 1.0e300;

double
tanh(double x)
{
	double t,z;
	int32_t jx,ix;

	GET_HIGH_WORD(jx,x);
	ix = jx&0x7fffffff;

    /* x is INF or NaN */
	if(ix>=0x7ff00000) {
	    if (jx>=0) return one/x+one;    /* tanh(+-inf)=+-1 */
	    else       return one/x-one;    /* tanh(NaN) = NaN */
	}

    /* |x| < 22 */
	if (ix < 0x40360000) {		/* |x|<22 */
	    if (ix<0x3e300000) {	/* |x|<2**-28 */
		if(huge+x>one) return x; /* tanh(tiny) = tiny with inexact */
	    }
	    if (ix>=0x3ff00000) {	/* |x|>=1  */
		t = expm1(two*fabs(x));
		z = one - two/(t+two);
	    } else {
	        t = expm1(-two*fabs(x));
	        z= -t/(t+two);
	    }
    /* |x| >= 22, return +-1 */
	} else {
	    z = one - tiny;		/* raise inexact flag */
	}
	return (jx>=0)? z: -z;
}

#if (LDBL_MANT_DIG == 53)
__weak_reference(tanh, tanhl);
#endif
