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
	int64_t i0,i1,jj0;
	u_int64_t i;
	GET_LDOUBLE_WORDS64(i0,i1,x);
	jj0 = ((i0>>48)&0x7fff)-0x3fff;	/* exponent of x */
	if(jj0<48) {			/* integer part in high x */
	    if(jj0<0) {			/* |x|<1 */
		/* *iptr = +-0 */
		SET_LDOUBLE_WORDS64(*iptr,i0&0x8000000000000000ULL,0);
		return x;
	    } else {
		i = (0x0000ffffffffffffLL)>>jj0;
		if(((i0&i)|i1)==0) {		/* x is integral */
		    *iptr = x;
		    /* return +-0 */
		    SET_LDOUBLE_WORDS64(x,i0&0x8000000000000000ULL,0);
		    return x;
		} else {
		    SET_LDOUBLE_WORDS64(*iptr,i0&(~i),0);
		    return x - *iptr;
		}
	    }
	} else if (jj0>111) {		/* no fraction part */
	    *iptr = x*one;
	    /* We must handle NaNs separately.  */
	    if (jj0 == 0x4000 && ((i0 & 0x0000ffffffffffffLL) | i1))
	      return x*one;
	    /* return +-0 */
	    SET_LDOUBLE_WORDS64(x,i0&0x8000000000000000ULL,0);
	    return x;
	} else {			/* fraction part in low x */
	    i = -1ULL>>(jj0-48);
	    if((i1&i)==0) {		/* x is integral */
		*iptr = x;
		/* return +-0 */
		SET_LDOUBLE_WORDS64(x,i0&0x8000000000000000ULL,0);
		return x;
	    } else {
		SET_LDOUBLE_WORDS64(*iptr,i0,i1&(~i));
		return x - *iptr;
	    }
	}
}
