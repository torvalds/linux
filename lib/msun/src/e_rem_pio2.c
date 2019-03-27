
/* @(#)e_rem_pio2.c 1.4 95/01/18 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
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

/* __ieee754_rem_pio2(x,y)
 * 
 * return the remainder of x rem pi/2 in y[0]+y[1] 
 * use __kernel_rem_pio2()
 */

#include <float.h>

#include "math.h"
#include "math_private.h"

/*
 * invpio2:  53 bits of 2/pi
 * pio2_1:   first  33 bit of pi/2
 * pio2_1t:  pi/2 - pio2_1
 * pio2_2:   second 33 bit of pi/2
 * pio2_2t:  pi/2 - (pio2_1+pio2_2)
 * pio2_3:   third  33 bit of pi/2
 * pio2_3t:  pi/2 - (pio2_1+pio2_2+pio2_3)
 */

static const double
zero =  0.00000000000000000000e+00, /* 0x00000000, 0x00000000 */
two24 =  1.67772160000000000000e+07, /* 0x41700000, 0x00000000 */
invpio2 =  6.36619772367581382433e-01, /* 0x3FE45F30, 0x6DC9C883 */
pio2_1  =  1.57079632673412561417e+00, /* 0x3FF921FB, 0x54400000 */
pio2_1t =  6.07710050650619224932e-11, /* 0x3DD0B461, 0x1A626331 */
pio2_2  =  6.07710050630396597660e-11, /* 0x3DD0B461, 0x1A600000 */
pio2_2t =  2.02226624879595063154e-21, /* 0x3BA3198A, 0x2E037073 */
pio2_3  =  2.02226624871116645580e-21, /* 0x3BA3198A, 0x2E000000 */
pio2_3t =  8.47842766036889956997e-32; /* 0x397B839A, 0x252049C1 */

#ifdef INLINE_REM_PIO2
static __inline __always_inline
#endif
int
__ieee754_rem_pio2(double x, double *y)
{
	double z,w,t,r,fn;
	double tx[3],ty[2];
	int32_t e0,i,j,nx,n,ix,hx;
	u_int32_t low;

	GET_HIGH_WORD(hx,x);		/* high word of x */
	ix = hx&0x7fffffff;
#if 0 /* Must be handled in caller. */
	if(ix<=0x3fe921fb)   /* |x| ~<= pi/4 , no need for reduction */
	    {y[0] = x; y[1] = 0; return 0;}
#endif
	if (ix <= 0x400f6a7a) {		/* |x| ~<= 5pi/4 */
	    if ((ix & 0xfffff) == 0x921fb)  /* |x| ~= pi/2 or 2pi/2 */
		goto medium;		/* cancellation -- use medium case */
	    if (ix <= 0x4002d97c) {	/* |x| ~<= 3pi/4 */
		if (hx > 0) {
		    z = x - pio2_1;	/* one round good to 85 bits */
		    y[0] = z - pio2_1t;
		    y[1] = (z-y[0])-pio2_1t;
		    return 1;
		} else {
		    z = x + pio2_1;
		    y[0] = z + pio2_1t;
		    y[1] = (z-y[0])+pio2_1t;
		    return -1;
		}
	    } else {
		if (hx > 0) {
		    z = x - 2*pio2_1;
		    y[0] = z - 2*pio2_1t;
		    y[1] = (z-y[0])-2*pio2_1t;
		    return 2;
		} else {
		    z = x + 2*pio2_1;
		    y[0] = z + 2*pio2_1t;
		    y[1] = (z-y[0])+2*pio2_1t;
		    return -2;
		}
	    }
	}
	if (ix <= 0x401c463b) {		/* |x| ~<= 9pi/4 */
	    if (ix <= 0x4015fdbc) {	/* |x| ~<= 7pi/4 */
		if (ix == 0x4012d97c)	/* |x| ~= 3pi/2 */
		    goto medium;
		if (hx > 0) {
		    z = x - 3*pio2_1;
		    y[0] = z - 3*pio2_1t;
		    y[1] = (z-y[0])-3*pio2_1t;
		    return 3;
		} else {
		    z = x + 3*pio2_1;
		    y[0] = z + 3*pio2_1t;
		    y[1] = (z-y[0])+3*pio2_1t;
		    return -3;
		}
	    } else {
		if (ix == 0x401921fb)	/* |x| ~= 4pi/2 */
		    goto medium;
		if (hx > 0) {
		    z = x - 4*pio2_1;
		    y[0] = z - 4*pio2_1t;
		    y[1] = (z-y[0])-4*pio2_1t;
		    return 4;
		} else {
		    z = x + 4*pio2_1;
		    y[0] = z + 4*pio2_1t;
		    y[1] = (z-y[0])+4*pio2_1t;
		    return -4;
		}
	    }
	}
	if(ix<0x413921fb) {	/* |x| ~< 2^20*(pi/2), medium size */
medium:
	    fn = rnint((double_t)x*invpio2);
	    n  = irint(fn);
	    r  = x-fn*pio2_1;
	    w  = fn*pio2_1t;	/* 1st round good to 85 bit */
	    {
	        u_int32_t high;
	        j  = ix>>20;
	        y[0] = r-w; 
		GET_HIGH_WORD(high,y[0]);
	        i = j-((high>>20)&0x7ff);
	        if(i>16) {  /* 2nd iteration needed, good to 118 */
		    t  = r;
		    w  = fn*pio2_2;	
		    r  = t-w;
		    w  = fn*pio2_2t-((t-r)-w);	
		    y[0] = r-w;
		    GET_HIGH_WORD(high,y[0]);
		    i = j-((high>>20)&0x7ff);
		    if(i>49)  {	/* 3rd iteration need, 151 bits acc */
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
	if(ix>=0x7ff00000) {		/* x is inf or NaN */
	    y[0]=y[1]=x-x; return 0;
	}
    /* set z = scalbn(|x|,ilogb(x)-23) */
	GET_LOW_WORD(low,x);
	e0 	= (ix>>20)-1046;	/* e0 = ilogb(z)-23; */
	INSERT_WORDS(z, ix - ((int32_t)(e0<<20)), low);
	for(i=0;i<2;i++) {
		tx[i] = (double)((int32_t)(z));
		z     = (z-tx[i])*two24;
	}
	tx[2] = z;
	nx = 3;
	while(tx[nx-1]==zero) nx--;	/* skip zero term */
	n  =  __kernel_rem_pio2(tx,ty,e0,nx,1);
	if(hx<0) {y[0] = -ty[0]; y[1] = -ty[1]; return -n;}
	y[0] = ty[0]; y[1] = ty[1]; return n;
}
