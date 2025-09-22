/* @(#)s_ceil.c 5.1 93/09/24 */
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
 * ceill(x)
 * Return x rounded toward -inf to integral value
 * Method:
 *	Bit twiddling.
 * Exception:
 *	Inexact flag raised if x not equal to ceil(x).
 */

#include <math.h>

#include "math_private.h"

static const long double huge = 1.0e4930L;

long double
ceill(long double x)
{
	int64_t i0,i1,jj0;
	u_int64_t i,j;
	GET_LDOUBLE_WORDS64(i0,i1,x);
	jj0 = ((i0>>48)&0x7fff)-0x3fff;
	if(jj0<48) {
	    if(jj0<0) {		/* raise inexact if x != 0 */
		if(huge+x>0.0) {/* return 0*sign(x) if |x|<1 */
		    if(i0<0) {i0=0x8000000000000000ULL;i1=0;}
		    else if((i0|i1)!=0) { i0=0x3fff000000000000ULL;i1=0;}
		}
	    } else {
		i = (0x0000ffffffffffffULL)>>jj0;
		if(((i0&i)|i1)==0) return x; /* x is integral */
		if(huge+x>0.0) {	/* raise inexact flag */
		    if(i0>0) i0 += (0x0001000000000000LL)>>jj0;
		    i0 &= (~i); i1=0;
		}
	    }
	} else if (jj0>111) {
	    if(jj0==0x4000) return x+x;	/* inf or NaN */
	    else return x;		/* x is integral */
	} else {
	    i = -1ULL>>(jj0-48);
	    if((i1&i)==0) return x;	/* x is integral */
	    if(huge+x>0.0) {		/* raise inexact flag */
		if(i0>0) {
		    if(jj0==48) i0+=1;
		    else {
			j = i1+(1LL<<(112-jj0));
			if(j<i1) i0 +=1 ;	/* got a carry */
			i1=j;
		    }
		}
		i1 &= (~i);
	    }
	}
	SET_LDOUBLE_WORDS64(x,i0,i1);
	return x;
}
