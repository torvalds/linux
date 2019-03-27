/* e_lgammaf_r.c -- float version of e_lgamma_r.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 * Conversion to float fixed By Steven G. Kargl.
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

static const volatile float vzero = 0;

static const float
zero=  0,
half=  0.5,
one =  1,
pi  =  3.1415927410e+00, /* 0x40490fdb */
/*
 * Domain y in [0x1p-27, 0.27], range ~[-3.4599e-10, 3.4590e-10]:
 * |(lgamma(2 - y) + 0.5 * y) / y - a(y)| < 2**-31.4
 */
a0  =  7.72156641e-02, /* 0x3d9e233f */
a1  =  3.22467119e-01, /* 0x3ea51a69 */
a2  =  6.73484802e-02, /* 0x3d89ee00 */
a3  =  2.06395667e-02, /* 0x3ca9144f */
a4  =  6.98275631e-03, /* 0x3be4cf9b */
a5  =  4.11768444e-03, /* 0x3b86eda4 */
/*
 * Domain x in [tc-0.24, tc+0.28], range ~[-5.6577e-10, 5.5677e-10]:
 * |(lgamma(x) - tf) - t(x - tc)| < 2**-30.8.
 */
tc  =  1.46163213e+00, /* 0x3fbb16c3 */
tf  = -1.21486291e-01, /* 0xbdf8cdce */
t0  = -2.94064460e-11, /* 0xae0154b7 */
t1  = -2.35939837e-08, /* 0xb2caabb8 */
t2  =  4.83836412e-01, /* 0x3ef7b968 */
t3  = -1.47586212e-01, /* 0xbe1720d7 */
t4  =  6.46013096e-02, /* 0x3d844db1 */
t5  = -3.28450352e-02, /* 0xbd068884 */
t6  =  1.86483748e-02, /* 0x3c98c47a */
t7  = -9.89206228e-03, /* 0xbc221251 */
/*
 * Domain y in [-0.1, 0.232], range ~[-8.4931e-10, 8.7794e-10]:
 * |(lgamma(1 + y) + 0.5 * y) / y - u(y) / v(y)| < 2**-31.2
 */
u0  = -7.72156641e-02, /* 0xbd9e233f */
u1  =  7.36789703e-01, /* 0x3f3c9e40 */
u2  =  4.95649040e-01, /* 0x3efdc5b6 */
v1  =  1.10958421e+00, /* 0x3f8e06db */
v2  =  2.10598111e-01, /* 0x3e57a708 */
v3  = -1.02995494e-02, /* 0xbc28bf71 */
/*
 * Domain x in (2, 3], range ~[-5.5189e-11, 5.2317e-11]:
 * |(lgamma(y+2) - 0.5 * y) / y - s(y)/r(y)| < 2**-35.0
 * with y = x - 2.
 */
s0 = -7.72156641e-02, /* 0xbd9e233f */
s1 =  2.69987404e-01, /* 0x3e8a3bca */
s2 =  1.42851010e-01, /* 0x3e124789 */
s3 =  1.19389519e-02, /* 0x3c439b98 */
r1 =  6.79650068e-01, /* 0x3f2dfd8c */
r2 =  1.16058730e-01, /* 0x3dedb033 */
r3 =  3.75673687e-03, /* 0x3b763396 */
/*
 * Domain z in [8, 0x1p24], range ~[-1.2640e-09, 1.2640e-09]:
 * |lgamma(x) - (x - 0.5) * (log(x) - 1) - w(1/x)| < 2**-29.6.
 */
w0 =  4.18938547e-01, /* 0x3ed67f1d */
w1 =  8.33332464e-02, /* 0x3daaaa9f */
w2 = -2.76129087e-03; /* 0xbb34f6c6 */

static float
sin_pif(float x)
{
	volatile float vz;
	float y,z;
	int n;

	y = -x;

	vz = y+0x1p23F;			/* depend on 0 <= y < 0x1p23 */
	z = vz-0x1p23F;			/* rintf(y) for the above range */
	if (z == y)
	    return zero;

	vz = y+0x1p21F;
	GET_FLOAT_WORD(n,vz);		/* bits for rounded y (units 0.25) */
	z = vz-0x1p21F;			/* y rounded to a multiple of 0.25 */
	if (z > y) {
	    z -= 0.25F;			/* adjust to round down */
	    n--;
	}
	n &= 7;				/* octant of y mod 2 */
	y = y - z + n * 0.25F;		/* y mod 2 */

	switch (n) {
	    case 0:   y =  __kernel_sindf(pi*y); break;
	    case 1:
	    case 2:   y =  __kernel_cosdf(pi*((float)0.5-y)); break;
	    case 3:
	    case 4:   y =  __kernel_sindf(pi*(one-y)); break;
	    case 5:
	    case 6:   y = -__kernel_cosdf(pi*(y-(float)1.5)); break;
	    default:  y =  __kernel_sindf(pi*(y-(float)2.0)); break;
	    }
	return -y;
}


float
__ieee754_lgammaf_r(float x, int *signgamp)
{
	float nadj,p,p1,p2,q,r,t,w,y,z;
	int32_t hx;
	int i,ix;

	GET_FLOAT_WORD(hx,x);

    /* purge +-Inf and NaNs */
	*signgamp = 1;
	ix = hx&0x7fffffff;
	if(ix>=0x7f800000) return x*x;

    /* purge +-0 and tiny arguments */
	*signgamp = 1-2*((uint32_t)hx>>31);
	if(ix<0x32000000) {		/* |x|<2**-27, return -log(|x|) */
	    if(ix==0)
	        return one/vzero;
	    return -__ieee754_logf(fabsf(x));
	}

    /* purge negative integers and start evaluation for other x < 0 */
	if(hx<0) {
	    *signgamp = 1;
	    if(ix>=0x4b000000) 		/* |x|>=2**23, must be -integer */
		return one/vzero;
	    t = sin_pif(x);
	    if(t==zero) return one/vzero; /* -integer */
	    nadj = __ieee754_logf(pi/fabsf(t*x));
	    if(t<zero) *signgamp = -1;
	    x = -x;
	}

    /* purge 1 and 2 */
	if (ix==0x3f800000||ix==0x40000000) r = 0;
    /* for x < 2.0 */
	else if(ix<0x40000000) {
	    if(ix<=0x3f666666) { 	/* lgamma(x) = lgamma(x+1)-log(x) */
		r = -__ieee754_logf(x);
		if(ix>=0x3f3b4a20) {y = one-x; i= 0;}
		else if(ix>=0x3e6d3308) {y= x-(tc-one); i=1;}
	  	else {y = x; i=2;}
	    } else {
	  	r = zero;
	        if(ix>=0x3fdda618) {y=2-x;i=0;} /* [1.7316,2] */
	        else if(ix>=0x3F9da620) {y=x-tc;i=1;} /* [1.23,1.73] */
		else {y=x-one;i=2;}
	    }
	    switch(i) {
	      case 0:
		z = y*y;
		p1 = a0+z*(a2+z*a4);
		p2 = z*(a1+z*(a3+z*a5));
		p  = y*p1+p2;
		r  += p-y/2; break;
	      case 1:
		p = t0+y*t1+y*y*(t2+y*(t3+y*(t4+y*(t5+y*(t6+y*t7)))));
		r += tf + p; break;
	      case 2:
		p1 = y*(u0+y*(u1+y*u2));
		p2 = one+y*(v1+y*(v2+y*v3));
		r += p1/p2-y/2;
	    }
	}
    /* x < 8.0 */
	else if(ix<0x41000000) {
	    i = x;
	    y = x-i;
	    p = y*(s0+y*(s1+y*(s2+y*s3)));
	    q = one+y*(r1+y*(r2+y*r3));
	    r = y/2+p/q;
	    z = one;	/* lgamma(1+s) = log(s) + lgamma(s) */
	    switch(i) {
	    case 7: z *= (y+6);		/* FALLTHRU */
	    case 6: z *= (y+5);		/* FALLTHRU */
	    case 5: z *= (y+4);		/* FALLTHRU */
	    case 4: z *= (y+3);		/* FALLTHRU */
	    case 3: z *= (y+2);		/* FALLTHRU */
		    r += __ieee754_logf(z); break;
	    }
    /* 8.0 <= x < 2**27 */
	} else if (ix < 0x4d000000) {
	    t = __ieee754_logf(x);
	    z = one/x;
	    y = z*z;
	    w = w0+z*(w1+y*w2);
	    r = (x-half)*(t-one)+w;
	} else
    /* 2**27 <= x <= inf */
	    r =  x*(__ieee754_logf(x)-one);
	if(hx<0) r = nadj - r;
	return r;
}
