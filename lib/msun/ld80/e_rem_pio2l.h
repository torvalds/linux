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

/* ld80 version of __ieee754_rem_pio2l(x,y)
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
 * invpio2:  64 bits of 2/pi
 * pio2_1:   first  39 bits of pi/2
 * pio2_1t:  pi/2 - pio2_1
 * pio2_2:   second 39 bits of pi/2
 * pio2_2t:  pi/2 - (pio2_1+pio2_2)
 * pio2_3:   third  39 bits of pi/2
 * pio2_3t:  pi/2 - (pio2_1+pio2_2+pio2_3)
 */

static const double
zero =  0.00000000000000000000e+00, /* 0x00000000, 0x00000000 */
two24 =  1.67772160000000000000e+07, /* 0x41700000, 0x00000000 */
pio2_1  =  1.57079632679597125389e+00,	/* 0x3FF921FB, 0x54444000 */
pio2_2  = -1.07463465549783099519e-12,	/* -0x12e7b967674000.0p-92 */
pio2_3  =  6.36831716351370313614e-25;	/*  0x18a2e037074000.0p-133 */

#if defined(__amd64__) || defined(__i386__)
/* Long double constants are slow on these arches, and broken on i386. */
static const volatile double
invpio2hi =  6.3661977236758138e-01,	/*  0x145f306dc9c883.0p-53 */
invpio2lo = -3.9356538861223811e-17,	/* -0x16b00000000000.0p-107 */
pio2_1thi = -1.0746346554971943e-12,	/* -0x12e7b9676733af.0p-92 */
pio2_1tlo =  8.8451028997905949e-29,	/*  0x1c080000000000.0p-146 */
pio2_2thi =  6.3683171635109499e-25,	/*  0x18a2e03707344a.0p-133 */
pio2_2tlo =  2.3183081793789774e-41,	/*  0x10280000000000.0p-187 */
pio2_3thi = -2.7529965190440717e-37,	/* -0x176b7ed8fbbacc.0p-174 */
pio2_3tlo = -4.2006647512740502e-54;	/* -0x19c00000000000.0p-230 */
#define	invpio2	((long double)invpio2hi + invpio2lo)
#define	pio2_1t	((long double)pio2_1thi + pio2_1tlo)
#define	pio2_2t	((long double)pio2_2thi + pio2_2tlo)
#define	pio2_3t	((long double)pio2_3thi + pio2_3tlo)
#else
static const long double
invpio2 =  6.36619772367581343076e-01L,	/*  0xa2f9836e4e44152a.0p-64 */
pio2_1t = -1.07463465549719416346e-12L,	/* -0x973dcb3b399d747f.0p-103 */
pio2_2t =  6.36831716351095013979e-25L,	/*  0xc51701b839a25205.0p-144 */
pio2_3t = -2.75299651904407171810e-37L;	/* -0xbb5bf6c7ddd660ce.0p-185 */
#endif

static inline __always_inline int
__ieee754_rem_pio2l(long double x, long double *y)
{
	union IEEEl2bits u,u1;
	long double z,w,t,r,fn;
	double tx[3],ty[2];
	int e0,ex,i,j,nx,n;
	int16_t expsign;

	u.e = x;
	expsign = u.xbits.expsign;
	ex = expsign & 0x7fff;
	if (ex < BIAS + 25 || (ex == BIAS + 25 && u.bits.manh < 0xc90fdaa2)) {
	    /* |x| ~< 2^25*(pi/2), medium size */
	    fn = rnintl(x*invpio2);
	    n  = irint(fn);
	    r  = x-fn*pio2_1;
	    w  = fn*pio2_1t;	/* 1st round good to 102 bit */
	    {
		union IEEEl2bits u2;
	        int ex1;
	        j  = ex;
	        y[0] = r-w; 
		u2.e = y[0];
		ex1 = u2.xbits.expsign & 0x7fff;
	        i = j-ex1;
	        if(i>22) {  /* 2nd iteration needed, good to 141 */
		    t  = r;
		    w  = fn*pio2_2;	
		    r  = t-w;
		    w  = fn*pio2_2t-((t-r)-w);	
		    y[0] = r-w;
		    u2.e = y[0];
		    ex1 = u2.xbits.expsign & 0x7fff;
		    i = j-ex1;
		    if(i>61) {	/* 3rd iteration need, 180 bits acc */
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
	for(i=0;i<2;i++) {
		tx[i] = (double)((int32_t)(z));
		z     = (z-tx[i])*two24;
	}
	tx[2] = z;
	nx = 3;
	while(tx[nx-1]==zero) nx--;	/* skip zero term */
	n  =  __kernel_rem_pio2(tx,ty,e0,nx,2);
	r = (long double)ty[0] + ty[1];
	w = ty[1] - (r - ty[0]);
	if(expsign<0) {y[0] = -r; y[1] = -w; return -n;}
	y[0] = r; y[1] = w; return n;
}
