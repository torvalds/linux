/* s_erff.c -- float version of s_erf.c.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "math.h"
#include "math_private.h"

/* XXX Prevent compilers from erroneously constant folding: */
static const volatile float tiny = 1e-30;

static const float
half= 0.5,
one = 1,
two = 2,
erx = 8.42697144e-01,			/* 0x3f57bb00 */
/*
 * In the domain [0, 2**-14], only the first term in the power series
 * expansion of erf(x) is used.  The magnitude of the first neglected
 * terms is less than 2**-42.
 */
efx = 1.28379166e-01, /* 0x3e0375d4 */
efx8= 1.02703333e+00, /* 0x3f8375d4 */
/*
 * Domain [0, 0.84375], range ~[-5.4419e-10, 5.5179e-10]:
 * |(erf(x) - x)/x - pp(x)/qq(x)| < 2**-31
 */
pp0  =  1.28379166e-01, /* 0x3e0375d4 */
pp1  = -3.36030394e-01, /* 0xbeac0c2d */
pp2  = -1.86261395e-03, /* 0xbaf422f4 */
qq1  =  3.12324315e-01, /* 0x3e9fe8f9 */
qq2  =  2.16070414e-02, /* 0x3cb10140 */
qq3  = -1.98859372e-03, /* 0xbb025311 */
/*
 * Domain [0.84375, 1.25], range ~[-1.023e-9, 1.023e-9]:
 * |(erf(x) - erx) - pa(x)/qa(x)| < 2**-31
 */
pa0  =  3.65041046e-06, /* 0x3674f993 */
pa1  =  4.15109307e-01, /* 0x3ed48935 */
pa2  = -2.09395722e-01, /* 0xbe566bd5 */
pa3  =  8.67677554e-02, /* 0x3db1b34b */
qa1  =  4.95560974e-01, /* 0x3efdba2b */
qa2  =  3.71248513e-01, /* 0x3ebe1449 */
qa3  =  3.92478965e-02, /* 0x3d20c267 */
/*
 * Domain [1.25,1/0.35], range ~[-4.821e-9, 4.927e-9]:
 * |log(x*erfc(x)) + x**2 + 0.5625 - ra(x)/sa(x)| < 2**-28
 */
ra0  = -9.88156721e-03, /* 0xbc21e64c */
ra1  = -5.43658376e-01, /* 0xbf0b2d32 */
ra2  = -1.66828310e+00, /* 0xbfd58a4d */
ra3  = -6.91554189e-01, /* 0xbf3109b2 */
sa1  =  4.48581553e+00, /* 0x408f8bcd */
sa2  =  4.10799170e+00, /* 0x408374ab */
sa3  =  5.53855181e-01, /* 0x3f0dc974 */
/*
 * Domain [2.85715, 11], range ~[-1.484e-9, 1.505e-9]:
 * |log(x*erfc(x)) + x**2 + 0.5625 - rb(x)/sb(x)| < 2**-30
 */
rb0  = -9.86496918e-03, /* 0xbc21a0ae */
rb1  = -5.48049808e-01, /* 0xbf0c4cfe */
rb2  = -1.84115684e+00, /* 0xbfebab07 */
sb1  =  4.87132740e+00, /* 0x409be1ea */
sb2  =  3.04982710e+00, /* 0x4043305e */
sb3  = -7.61900663e-01; /* 0xbf430bec */

float
erff(float x)
{
	int32_t hx,ix,i;
	float R,S,P,Q,s,y,z,r;
	GET_FLOAT_WORD(hx,x);
	ix = hx&0x7fffffff;
	if(ix>=0x7f800000) {		/* erff(nan)=nan */
	    i = ((u_int32_t)hx>>31)<<1;
	    return (float)(1-i)+one/x;	/* erff(+-inf)=+-1 */
	}

	if(ix < 0x3f580000) {		/* |x|<0.84375 */
	    if(ix < 0x38800000) { 	/* |x|<2**-14 */
	        if (ix < 0x04000000)	/* |x|<0x1p-119 */
		    return (8*x+efx8*x)/8;	/* avoid spurious underflow */
		return x + efx*x;
	    }
	    z = x*x;
	    r = pp0+z*(pp1+z*pp2);
	    s = one+z*(qq1+z*(qq2+z*qq3));
	    y = r/s;
	    return x + x*y;
	}
	if(ix < 0x3fa00000) {		/* 0.84375 <= |x| < 1.25 */
	    s = fabsf(x)-one;
	    P = pa0+s*(pa1+s*(pa2+s*pa3));
	    Q = one+s*(qa1+s*(qa2+s*qa3));
	    if(hx>=0) return erx + P/Q; else return -erx - P/Q;
	}
	if (ix >= 0x40800000) {		/* inf>|x|>=4 */
	    if(hx>=0) return one-tiny; else return tiny-one;
	}
	x = fabsf(x);
 	s = one/(x*x);
	if(ix< 0x4036db8c) {	/* |x| < 2.85715 ~ 1/0.35 */
	    R=ra0+s*(ra1+s*(ra2+s*ra3));
	    S=one+s*(sa1+s*(sa2+s*sa3));
	} else {	/* |x| >= 2.85715 ~ 1/0.35 */
	    R=rb0+s*(rb1+s*rb2);
	    S=one+s*(sb1+s*(sb2+s*sb3));
	}
	SET_FLOAT_WORD(z,hx&0xffffe000);
	r  = expf(-z*z-0.5625F)*expf((z-x)*(z+x)+R/S);
	if(hx>=0) return one-r/x; else return  r/x-one;
}

float
erfcf(float x)
{
	int32_t hx,ix;
	float R,S,P,Q,s,y,z,r;
	GET_FLOAT_WORD(hx,x);
	ix = hx&0x7fffffff;
	if(ix>=0x7f800000) {			/* erfcf(nan)=nan */
						/* erfcf(+-inf)=0,2 */
	    return (float)(((u_int32_t)hx>>31)<<1)+one/x;
	}

	if(ix < 0x3f580000) {		/* |x|<0.84375 */
	    if(ix < 0x33800000)  	/* |x|<2**-24 */
		return one-x;
	    z = x*x;
	    r = pp0+z*(pp1+z*pp2);
	    s = one+z*(qq1+z*(qq2+z*qq3));
	    y = r/s;
	    if(hx < 0x3e800000) {  	/* x<1/4 */
		return one-(x+x*y);
	    } else {
		r = x*y;
		r += (x-half);
	        return half - r ;
	    }
	}
	if(ix < 0x3fa00000) {		/* 0.84375 <= |x| < 1.25 */
	    s = fabsf(x)-one;
	    P = pa0+s*(pa1+s*(pa2+s*pa3));
	    Q = one+s*(qa1+s*(qa2+s*qa3));
	    if(hx>=0) {
	        z  = one-erx; return z - P/Q;
	    } else {
		z = erx+P/Q; return one+z;
	    }
	}
	if (ix < 0x41300000) {		/* |x|<11 */
	    x = fabsf(x);
 	    s = one/(x*x);
	    if(ix< 0x4036db8c) {	/* |x| < 2.85715 ~ 1/.35 */
		R=ra0+s*(ra1+s*(ra2+s*ra3));
		S=one+s*(sa1+s*(sa2+s*sa3));
	    } else {			/* |x| >= 2.85715 ~ 1/.35 */
		if(hx<0&&ix>=0x40a00000) return two-tiny;/* x < -5 */
		R=rb0+s*(rb1+s*rb2);
		S=one+s*(sb1+s*(sb2+s*sb3));
	    }
	    SET_FLOAT_WORD(z,hx&0xffffe000);
	    r  = expf(-z*z-0.5625F)*expf((z-x)*(z+x)+R/S);
	    if(hx>0) return r/x; else return two-r/x;
	} else {
	    if(hx>0) return tiny*tiny; else return two-tiny;
	}
}
