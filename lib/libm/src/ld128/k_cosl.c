/*	$OpenBSD: k_cosl.c,v 1.1 2008/12/09 20:00:35 martynas Exp $	*/
/* From: @(#)k_cos.c 1.3 95/01/18 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2008 Steven G. Kargl, David Schultz, Bruce D. Evans.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

/*
 * ld128 version of k_cos.c.  See ../k_cos.c for most comments.
 */

#include "math_private.h"

/*
 * Domain [-0.7854, 0.7854], range ~[-1.80e-37, 1.79e-37]:
 * |cos(x) - c(x))| < 2**-122.0
 *
 * 113-bit precision requires more care than 64-bit precision, since
 * simple methods give a minimax polynomial with coefficient for x^2
 * that is 1 ulp below 0.5, but we want it to be precisely 0.5.  See
 * ../ld80/k_cosl.c for more details.
 */
static const double
one = 1.0;

static const long double
C1 =  0.04166666666666666666666666666666658424671L,
C2 = -0.001388888888888888888888888888863490893732L,
C3 =  0.00002480158730158730158730158600795304914210L,
C4 = -0.2755731922398589065255474947078934284324e-6L,
C5 =  0.2087675698786809897659225313136400793948e-8L,
C6 = -0.1147074559772972315817149986812031204775e-10L,
C7 =  0.4779477332386808976875457937252120293400e-13L;

static const double
C8 = -0.1561920696721507929516718307820958119868e-15,
C9 =  0.4110317413744594971475941557607804508039e-18,
C10 = -0.8896592467191938803288521958313920156409e-21,
C11 =  0.1601061435794535138244346256065192782581e-23;

long double
__kernel_cosl(long double x, long double y)
{
	long double hz,z,r,w;

	z  = x*x;
	r  = z*(C1+z*(C2+z*(C3+z*(C4+z*(C5+z*(C6+z*(C7+
	    z*(C8+z*(C9+z*(C10+z*C11))))))))));
	hz = 0.5*z;
	w  = one-hz;
	return w + (((one-w)-hz) + (z*r-x*y));
}
