/* e_lgammaf_r.c -- float version of e_lgamma_r.c.
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

static const float 
two23=  8.3886080000e+06, /* 0x4b000000 */
half=  5.0000000000e-01, /* 0x3f000000 */
one =  1.0000000000e+00, /* 0x3f800000 */
pi  =  3.1415927410e+00, /* 0x40490fdb */
a0  =  7.7215664089e-02, /* 0x3d9e233f */
a1  =  3.2246702909e-01, /* 0x3ea51a66 */
a2  =  6.7352302372e-02, /* 0x3d89f001 */
a3  =  2.0580807701e-02, /* 0x3ca89915 */
a4  =  7.3855509982e-03, /* 0x3bf2027e */
a5  =  2.8905137442e-03, /* 0x3b3d6ec6 */
a6  =  1.1927076848e-03, /* 0x3a9c54a1 */
a7  =  5.1006977446e-04, /* 0x3a05b634 */
a8  =  2.2086278477e-04, /* 0x39679767 */
a9  =  1.0801156895e-04, /* 0x38e28445 */
a10 =  2.5214456400e-05, /* 0x37d383a2 */
a11 =  4.4864096708e-05, /* 0x383c2c75 */
tc  =  1.4616321325e+00, /* 0x3fbb16c3 */
tf  = -1.2148628384e-01, /* 0xbdf8cdcd */
/* tt = -(tail of tf) */
tt  =  6.6971006518e-09, /* 0x31e61c52 */
t0  =  4.8383611441e-01, /* 0x3ef7b95e */
t1  = -1.4758771658e-01, /* 0xbe17213c */
t2  =  6.4624942839e-02, /* 0x3d845a15 */
t3  = -3.2788541168e-02, /* 0xbd064d47 */
t4  =  1.7970675603e-02, /* 0x3c93373d */
t5  = -1.0314224288e-02, /* 0xbc28fcfe */
t6  =  6.1005386524e-03, /* 0x3bc7e707 */
t7  = -3.6845202558e-03, /* 0xbb7177fe */
t8  =  2.2596477065e-03, /* 0x3b141699 */
t9  = -1.4034647029e-03, /* 0xbab7f476 */
t10 =  8.8108185446e-04, /* 0x3a66f867 */
t11 = -5.3859531181e-04, /* 0xba0d3085 */
t12 =  3.1563205994e-04, /* 0x39a57b6b */
t13 = -3.1275415677e-04, /* 0xb9a3f927 */
t14 =  3.3552918467e-04, /* 0x39afe9f7 */
u0  = -7.7215664089e-02, /* 0xbd9e233f */
u1  =  6.3282704353e-01, /* 0x3f2200f4 */
u2  =  1.4549225569e+00, /* 0x3fba3ae7 */
u3  =  9.7771751881e-01, /* 0x3f7a4bb2 */
u4  =  2.2896373272e-01, /* 0x3e6a7578 */
u5  =  1.3381091878e-02, /* 0x3c5b3c5e */
v1  =  2.4559779167e+00, /* 0x401d2ebe */
v2  =  2.1284897327e+00, /* 0x4008392d */
v3  =  7.6928514242e-01, /* 0x3f44efdf */
v4  =  1.0422264785e-01, /* 0x3dd572af */
v5  =  3.2170924824e-03, /* 0x3b52d5db */
s0  = -7.7215664089e-02, /* 0xbd9e233f */
s1  =  2.1498242021e-01, /* 0x3e5c245a */
s2  =  3.2577878237e-01, /* 0x3ea6cc7a */
s3  =  1.4635047317e-01, /* 0x3e15dce6 */
s4  =  2.6642270386e-02, /* 0x3cda40e4 */
s5  =  1.8402845599e-03, /* 0x3af135b4 */
s6  =  3.1947532989e-05, /* 0x3805ff67 */
r1  =  1.3920053244e+00, /* 0x3fb22d3b */
r2  =  7.2193557024e-01, /* 0x3f38d0c5 */
r3  =  1.7193385959e-01, /* 0x3e300f6e */
r4  =  1.8645919859e-02, /* 0x3c98bf54 */
r5  =  7.7794247773e-04, /* 0x3a4beed6 */
r6  =  7.3266842264e-06, /* 0x36f5d7bd */
w0  =  4.1893854737e-01, /* 0x3ed67f1d */
w1  =  8.3333335817e-02, /* 0x3daaaaab */
w2  = -2.7777778450e-03, /* 0xbb360b61 */
w3  =  7.9365057172e-04, /* 0x3a500cfd */
w4  = -5.9518753551e-04, /* 0xba1c065c */
w5  =  8.3633989561e-04, /* 0x3a5b3dd2 */
w6  = -1.6309292987e-03; /* 0xbad5c4e8 */

static const float zero=  0.0000000000e+00;

static float
sin_pif(float x)
{
	float y,z;
	int n,ix;

	GET_FLOAT_WORD(ix,x);
	ix &= 0x7fffffff;

	if(ix<0x3e800000) return __kernel_sinf(pi*x,zero,0);
	y = -x;		/* x is assume negative */

    /*
     * argument reduction, make sure inexact flag not raised if input
     * is an integer
     */
	z = floorf(y);
	if(z!=y) {				/* inexact anyway */
	    y  *= (float)0.5;
	    y   = (float)2.0*(y - floorf(y));	/* y = |x| mod 2.0 */
	    n   = (int) (y*(float)4.0);
	} else {
            if(ix>=0x4b800000) {
                y = zero; n = 0;                 /* y must be even */
            } else {
                if(ix<0x4b000000) z = y+two23;	/* exact */
		GET_FLOAT_WORD(n,z);
		n &= 1;
                y  = n;
                n<<= 2;
            }
        }
	switch (n) {
	    case 0:   y =  __kernel_sinf(pi*y,zero,0); break;
	    case 1:   
	    case 2:   y =  __kernel_cosf(pi*((float)0.5-y),zero); break;
	    case 3:  
	    case 4:   y =  __kernel_sinf(pi*(one-y),zero,0); break;
	    case 5:
	    case 6:   y = -__kernel_cosf(pi*(y-(float)1.5),zero); break;
	    default:  y =  __kernel_sinf(pi*(y-(float)2.0),zero,0); break;
	    }
	return -y;
}


float
lgammaf_r(float x, int *signgamp)
{
	float t,y,z,nadj,p,p1,p2,p3,q,r,w;
	int i,hx,ix;

	GET_FLOAT_WORD(hx,x);

    /* purge off +-inf, NaN, +-0, and negative arguments */
	*signgamp = 1;
	ix = hx&0x7fffffff;
	if(ix>=0x7f800000) return x*x;
	if(ix==0) {
	    if(hx<0)
		*signgamp = -1;
	    return one/zero;
	}
	if(ix<0x1c800000) {	/* |x|<2**-70, return -log(|x|) */
	    if(hx<0) {
	        *signgamp = -1;
	        return - logf(-x);
	    } else return - logf(x);
	}
	if(hx<0) {
	    if(ix>=0x4b000000) 	/* |x|>=2**23, must be -integer */
		return one/zero;
	    t = sin_pif(x);
	    if(t==zero) return one/zero; /* -integer */
	    nadj = logf(pi/fabsf(t*x));
	    if(t<zero) *signgamp = -1;
	    x = -x;
	}

    /* purge off 1 and 2 */
	if (ix==0x3f800000||ix==0x40000000) r = 0;
    /* for x < 2.0 */
	else if(ix<0x40000000) {
	    if(ix<=0x3f666666) { 	/* lgamma(x) = lgamma(x+1)-log(x) */
		r = - logf(x);
		if(ix>=0x3f3b4a20) {y = one-x; i= 0;}
		else if(ix>=0x3e6d3308) {y= x-(tc-one); i=1;}
	  	else {y = x; i=2;}
	    } else {
	  	r = zero;
	        if(ix>=0x3fdda618) {y=(float)2.0-x;i=0;} /* [1.7316,2] */
	        else if(ix>=0x3F9da620) {y=x-tc;i=1;} /* [1.23,1.73] */
		else {y=x-one;i=2;}
	    }
	    switch(i) {
	      case 0:
		z = y*y;
		p1 = a0+z*(a2+z*(a4+z*(a6+z*(a8+z*a10))));
		p2 = z*(a1+z*(a3+z*(a5+z*(a7+z*(a9+z*a11)))));
		p  = y*p1+p2;
		r  += (p-(float)0.5*y); break;
	      case 1:
		z = y*y;
		w = z*y;
		p1 = t0+w*(t3+w*(t6+w*(t9 +w*t12)));	/* parallel comp */
		p2 = t1+w*(t4+w*(t7+w*(t10+w*t13)));
		p3 = t2+w*(t5+w*(t8+w*(t11+w*t14)));
		p  = z*p1-(tt-w*(p2+y*p3));
		r += (tf + p); break;
	      case 2:	
		p1 = y*(u0+y*(u1+y*(u2+y*(u3+y*(u4+y*u5)))));
		p2 = one+y*(v1+y*(v2+y*(v3+y*(v4+y*v5))));
		r += (-(float)0.5*y + p1/p2);
	    }
	}
	else if(ix<0x41000000) { 			/* x < 8.0 */
	    i = (int)x;
	    t = zero;
	    y = x-(float)i;
	    p = y*(s0+y*(s1+y*(s2+y*(s3+y*(s4+y*(s5+y*s6))))));
	    q = one+y*(r1+y*(r2+y*(r3+y*(r4+y*(r5+y*r6)))));
	    r = half*y+p/q;
	    z = one;	/* lgamma(1+s) = log(s) + lgamma(s) */
	    switch(i) {
	    case 7: z *= (y+(float)6.0);	/* FALLTHRU */
	    case 6: z *= (y+(float)5.0);	/* FALLTHRU */
	    case 5: z *= (y+(float)4.0);	/* FALLTHRU */
	    case 4: z *= (y+(float)3.0);	/* FALLTHRU */
	    case 3: z *= (y+(float)2.0);	/* FALLTHRU */
		    r += logf(z); break;
	    }
    /* 8.0 <= x < 2**58 */
	} else if (ix < 0x5c800000) {
	    t = logf(x);
	    z = one/x;
	    y = z*z;
	    w = w0+z*(w1+y*(w2+y*(w3+y*(w4+y*(w5+y*w6)))));
	    r = (x-half)*(t-one)+w;
	} else 
    /* 2**58 <= x <= inf */
	    r =  x*(logf(x)-one);
	if(hx<0) r = nadj - r;
	return r;
}
DEF_NONSTD(lgammaf_r);
