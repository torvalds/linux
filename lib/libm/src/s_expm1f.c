/* s_expm1f.c -- float version of s_expm1.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */

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

#include "math.h"
#include "math_private.h"

static const volatile float huge = 1.0e+30, tiny = 1.0e-30;

static const float
one		= 1.0,
o_threshold	= 8.8721679688e+01,/* 0x42b17180 */
ln2_hi		= 6.9313812256e-01,/* 0x3f317180 */
ln2_lo		= 9.0580006145e-06,/* 0x3717f7d1 */
invln2		= 1.4426950216e+00,/* 0x3fb8aa3b */
	/* scaled coefficients related to expm1 */
Q1  =  -3.3333335072e-02, /* 0xbd088889 */
Q2  =   1.5873016091e-03, /* 0x3ad00d01 */
Q3  =  -7.9365076090e-05, /* 0xb8a670cd */
Q4  =   4.0082177293e-06, /* 0x36867e54 */
Q5  =  -2.0109921195e-07; /* 0xb457edbb */

float
expm1f(float x)
{
	float y,hi,lo,c,t,e,hxs,hfx,r1;
	int32_t k,xsb;
	u_int32_t hx;

	GET_FLOAT_WORD(hx,x);
	xsb = hx&0x80000000;		/* sign bit of x */
	if(xsb==0) y=x; else y= -x;	/* y = |x| */
	hx &= 0x7fffffff;		/* high word of |x| */

    /* filter out huge and non-finite argument */
	if(hx >= 0x4195b844) {			/* if |x|>=27*ln2 */
	    if(hx >= 0x42b17218) {		/* if |x|>=88.721... */
                if(hx>0x7f800000)
		    return x+x; 	 /* NaN */
		if(hx==0x7f800000)
		    return (xsb==0)? x:-1.0;/* exp(+-inf)={inf,-1} */
	        if(x > o_threshold) return huge*huge; /* overflow */
	    }
	    if(xsb!=0) { /* x < -27*ln2, return -1.0 with inexact */
		if(x+tiny<(float)0.0)	/* raise inexact */
		return tiny-one;	/* return -1 */
	    }
	}

    /* argument reduction */
	if(hx > 0x3eb17218) {		/* if  |x| > 0.5 ln2 */ 
	    if(hx < 0x3F851592) {	/* and |x| < 1.5 ln2 */
		if(xsb==0)
		    {hi = x - ln2_hi; lo =  ln2_lo;  k =  1;}
		else
		    {hi = x + ln2_hi; lo = -ln2_lo;  k = -1;}
	    } else {
		k  = invln2*x+((xsb==0)?(float)0.5:(float)-0.5);
		t  = k;
		hi = x - t*ln2_hi;	/* t*ln2_hi is exact here */
		lo = t*ln2_lo;
	    }
	    x  = hi - lo;
	    c  = (hi-x)-lo;
	} 
	else if(hx < 0x33000000) {  	/* when |x|<2**-25, return x */
	    t = huge+x;	/* return x with inexact flags when x!=0 */
	    return x - (t-(huge+x));	
	}
	else k = 0;

    /* x is now in primary range */
	hfx = (float)0.5*x;
	hxs = x*hfx;
	r1 = one+hxs*(Q1+hxs*(Q2+hxs*(Q3+hxs*(Q4+hxs*Q5))));
	t  = (float)3.0-r1*hfx;
	e  = hxs*((r1-t)/((float)6.0 - x*t));
	if(k==0) return x - (x*e-hxs);		/* c is 0 */
	else {
	    e  = (x*(e-c)-c);
	    e -= hxs;
	    if(k== -1) return (float)0.5*(x-e)-(float)0.5;
	    if(k==1) {
	       	if(x < (float)-0.25) return -(float)2.0*(e-(x+(float)0.5));
	       	else 	      return  one+(float)2.0*(x-e);
	    }
	    if (k <= -2 || k>56) {   /* suffice to return exp(x)-1 */
	        int32_t i;
	        y = one-(e-x);
		GET_FLOAT_WORD(i,y);
		SET_FLOAT_WORD(y,i+(k<<23));	/* add k to y's exponent */
	        return y-one;
	    }
	    t = one;
	    if(k<23) {
	        int32_t i;
	        SET_FLOAT_WORD(t,0x3f800000 - (0x1000000>>k)); /* t=1-2^-k */
	       	y = t-(e-x);
		GET_FLOAT_WORD(i,y);
		SET_FLOAT_WORD(y,i+(k<<23));	/* add k to y's exponent */
	   } else {
	        int32_t i;
		SET_FLOAT_WORD(t,((0x7f-k)<<23));	/* 2^-k */
	       	y = x-(e+t);
	       	y += one;
		GET_FLOAT_WORD(i,y);
		SET_FLOAT_WORD(y,i+(k<<23));	/* add k to y's exponent */
	    }
	}
	return y;
}
DEF_STD(expm1f);
