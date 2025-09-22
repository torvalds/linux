/* @(#)s_modf.c 5.1 93/09/24 */
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

/*
 * modfl(long double x, long double *iptr)
 * return fraction part of x, and return x's integral part in *iptr.
 * Method:
 *	Bit twiddling.
 *
 * Exception:
 *	No exception.
 */

#include <math.h>

#include "math_private.h"

static const long double one = 1.0;

long double
modfl(long double x, long double *iptr)
{
	int32_t i0,i1,jj0;
	u_int32_t i,se;
	GET_LDOUBLE_WORDS(se,i0,i1,x);
	jj0 = (se&0x7fff)-0x3fff;	/* exponent of x */
	if(jj0<32) {			/* integer part in high x */
	    if(jj0<0) {			/* |x|<1 */
		SET_LDOUBLE_WORDS(*iptr,se&0x8000,0,0);	/* *iptr = +-0 */
		return x;
	    } else {
		i = (0x7fffffff)>>jj0;
		if(((i0&i)|i1)==0) {		/* x is integral */
		    *iptr = x;
		    SET_LDOUBLE_WORDS(x,se&0x8000,0,0);	/* return +-0 */
		    return x;
		} else {
		    SET_LDOUBLE_WORDS(*iptr,se,i0&(~i),0);
		    return x - *iptr;
		}
	    }
	} else if (jj0>63) {		/* no fraction part */
	    *iptr = x*one;
	    /* We must handle NaNs separately.  */
	    if (jj0 == 0x4000 && ((i0 & 0x7fffffff) | i1))
	      return x*one;
	    SET_LDOUBLE_WORDS(x,se&0x8000,0,0);	/* return +-0 */
	    return x;
	} else {			/* fraction part in low x */
	    i = ((u_int32_t)(0x7fffffff))>>(jj0-32);
	    if((i1&i)==0) {		/* x is integral */
		*iptr = x;
		SET_LDOUBLE_WORDS(x,se&0x8000,0,0);	/* return +-0 */
		return x;
	    } else {
		SET_LDOUBLE_WORDS(*iptr,se,i0,i1&(~i));
		return x - *iptr;
	    }
	}
}
