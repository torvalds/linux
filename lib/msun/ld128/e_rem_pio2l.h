/* From: @(#)e_rem_pio2.c 1.4 95/01/18 */
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
 *
 * Optimized by Bruce D. Evans.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* ld128 version of __ieee754_rem_pio2l(x,y)
 * 
 * return the remainder of x rem pi/2 in y[0]+y[1] 
 * use __kernel_rem_pio2()
 */

#include <float.h>

#include "math.h"
#include "math_private.h"
#include "fpmath.h"

#define	BIAS	(LDBL_MAX_EXP - 1)

/*
 * XXX need to verify that nonzero integer multiples of pi/2 within the
 * range get no closer to a long double than 2**-140, or that
 * ilogb(x) + ilogb(min_delta) < 45 - -140.
 */
/*
 * invpio2:  113 bits of 2/pi
 * pio2_1:   first  68 bits of pi/2
 * pio2_1t:  pi/2 - pio2_1
 * pio2_2:   second 68 bits of pi/2
 * pio2_2t:  pi/2 - (pio2_1+pio2_2)
 * pio2_3:   third  68 bits of pi/2
 * pio2_3t:  pi/2 - (pio2_1+pio2_2+pio2_3)
 */

static const double
zero =  0.00000000000000000000e+00, /* 0x00000000, 0x00000000 */
two24 =  1.67772160000000000000e+07; /* 0x41700000, 0x00000000 */

static const long double
invpio2 =  6.3661977236758134307553505349005747e-01L,	/*  0x145f306dc9c882a53f84eafa3ea6a.0p-113 */
pio2_1  =  1.5707963267948966192292994253909555e+00L,	/*  0x1921fb54442d18469800000000000.0p-112 */
pio2_1t =  2.0222662487959507323996846200947577e-21L,	/*  0x13198a2e03707344a4093822299f3.0p-181 */
pio2_2  =  2.0222662487959507323994779168837751e-21L,	/*  0x13198a2e03707344a400000000000.0p-181 */
pio2_2t =  2.0670321098263988236496903051604844e-43L,	/*  0x127044533e63a0105df531d89cd91.0p-254 */
pio2_3  =  2.0670321098263988236499468110329591e-43L,	/*  0x127044533e63a0105e00000000000.0p-254 */
pio2_3t = -2.5650587247459238361625433492959285e-65L;	/* -0x159c4ec64ddaeb5f78671cbfb2210.0p-327 */

static inline __always_inline int
__ieee754_rem_pio2l(long double x, long double *y)
{
	union IEEEl2bits u,u1;
	long double z,w,t,r,fn;
	double tx[5],ty[3];
	int64_t n;
	int e0,ex,i,j,nx;
	int16_t expsign;

	u.e = x;
	expsign = u.xbits.expsign;
	ex = expsign & 0x7fff;
	if (ex < BIAS + 45 || ex == BIAS + 45 &&
	    u.bits.manh < 0x921fb54442d1LL) {
	    /* |x| ~< 2^45*(pi/2), medium size */
	    /* TODO: use only double precision for fn, as in expl(). */
	    fn = rnintl(x * invpio2);
	    n  = i64rint(fn);
	    r  = x-fn*pio2_1;
	    w  = fn*pio2_1t;	/* 1st round good to 180 bit */
	    {
		union IEEEl2bits u2;
	        int ex1;
	        j  = ex;
	        y[0] = r-w; 
		u2.e = y[0];
		ex1 = u2.xbits.expsign & 0x7fff;
	        i = j-ex1;
	        if(i>51) {  /* 2nd iteration needed, good to 248 */
		    t  = r;
		    w  = fn*pio2_2;	
		    r  = t-w;
		    w  = fn*pio2_2t-((t-r)-w);	
		    y[0] = r-w;
		    u2.e = y[0];
		    ex1 = u2.xbits.expsign & 0x7fff;
		    i = j-ex1;
		    if(i>119) {	/* 3rd iteration need, 316 bits acc */
		    	t  = r;	/* will cover all possible cases */
		    	w  = fn*pio2_3;	
		    	r  = t-w;
		    	w  = fn*pio2_3t-((t-r)-w);	
		    	y[0] = r-w;
		    }
		}
	    }
	    y[1] = (r-y[0])-w;
	    return n;
	}
    /* 
     * all other (large) arguments
     */
	if(ex==0x7fff) {		/* x is inf or NaN */
	    y[0]=y[1]=x-x; return 0;
	}
    /* set z = scalbn(|x|,ilogb(x)-23) */
	u1.e = x;
	e0 = ex - BIAS - 23;		/* e0 = ilogb(|x|)-23; */
	u1.xbits.expsign = ex - e0;
	z = u1.e;
	for(i=0;i<4;i++) {
		tx[i] = (double)((int32_t)(z));
		z     = (z-tx[i])*two24;
	}
	tx[4] = z;
	nx = 5;
	while(tx[nx-1]==zero) nx--;	/* skip zero term */
	n  =  __kernel_rem_pio2(tx,ty,e0,nx,3);
	t = (long double)ty[2] + ty[1];
	r = t + ty[0];
	w = ty[0] - (r - t);
	if(expsign<0) {y[0] = -r; y[1] = -w; return -n;}
	y[0] = r; y[1] = w; return n;
}
