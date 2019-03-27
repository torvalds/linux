/* @(#)s_nextafter.c 5.1 93/09/24 */
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

/*
 * We assume that a long double has a 15-bit exponent.  On systems
 * where long double is the same as double, nexttoward() is an alias
 * for nextafter(), so we don't use this routine.
 */

#include <float.h>

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

#if LDBL_MAX_EXP != 0x4000
#error "Unsupported long double format"
#endif

double
nexttoward(double x, long double y)
{
	union IEEEl2bits uy;
	volatile double t;
	int32_t hx,ix;
	u_int32_t lx;

	EXTRACT_WORDS(hx,lx,x);
	ix = hx&0x7fffffff;		/* |x| */
	uy.e = y;

	if(((ix>=0x7ff00000)&&((ix-0x7ff00000)|lx)!=0) ||
	    (uy.bits.exp == 0x7fff &&
	     ((uy.bits.manh&~LDBL_NBIT)|uy.bits.manl) != 0))
	   return x+y;	/* x or y is nan */
	if(x==y) return (double)y;		/* x=y, return y */
	if(x==0.0) {
	    INSERT_WORDS(x,uy.bits.sign<<31,1);	/* return +-minsubnormal */
	    t = x*x;
	    if(t==x) return t; else return x;	/* raise underflow flag */
	}
	if(hx>0.0 ^ x < y) {			/* x -= ulp */
	    if(lx==0) hx -= 1;
	    lx -= 1;
	} else {				/* x += ulp */
	    lx += 1;
	    if(lx==0) hx += 1;
	}
	ix = hx&0x7ff00000;
	if(ix>=0x7ff00000) return x+x;	/* overflow  */
	if(ix<0x00100000) {		/* underflow */
	    t = x*x;
	    if(t!=x) {		/* raise underflow flag */
	        INSERT_WORDS(x,hx,lx);
		return x;
	    }
	}
	INSERT_WORDS(x,hx,lx);
	return x;
}
