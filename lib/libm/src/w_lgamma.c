/* @(#)w_lgamma.c 5.1 93/09/24 */
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

/* double lgamma(double x)
 * Return the logarithm of the Gamma function of x.
 *
 * Method: call lgamma_r
 */

#include <float.h>
#include <math.h>

#include "math_private.h"

extern int signgam;

double
lgamma(double x)
{
	return lgamma_r(x,&signgam);
}
DEF_STD(lgamma);
LDBL_MAYBE_CLONE(lgamma);
