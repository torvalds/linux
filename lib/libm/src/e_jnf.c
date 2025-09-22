/* e_jnf.c -- float version of e_jn.c.
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
two   =  2.0000000000e+00, /* 0x40000000 */
one   =  1.0000000000e+00; /* 0x3F800000 */

static const float zero  =  0.0000000000e+00;

float
jnf(int n, float x)
{
	int32_t i,hx,ix, sgn;
	float a, b, temp, di;
	float z, w;

    /* J(-n,x) = (-1)^n * J(n, x), J(n, -x) = (-1)^n * J(n, x)
     * Thus, J(-n,x) = J(n,-x)
     */
	GET_FLOAT_WORD(hx,x);
	ix = 0x7fffffff&hx;
    /* if J(n,NaN) is NaN */
	if(ix>0x7f800000) return x+x;
	if(n<0){		
		n = -n;
		x = -x;
		hx ^= 0x80000000;
	}
	if(n==0) return(j0f(x));
	if(n==1) return(j1f(x));
	sgn = (n&1)&(hx>>31);	/* even n -- 0, odd n -- sign(x) */
	x = fabsf(x);
	if(ix==0||ix>=0x7f800000) 	/* if x is 0 or inf */
	    b = zero;
	else if((float)n<=x) {   
		/* Safe to use J(n+1,x)=2n/x *J(n,x)-J(n-1,x) */
	    a = j0f(x);
	    b = j1f(x);
	    for(i=1;i<n;i++){
		temp = b;
		b = b*((float)(i+i)/x) - a; /* avoid underflow */
		a = temp;
	    }
	} else {
	    if(ix<0x30800000) {	/* x < 2**-29 */
    /* x is tiny, return the first Taylor expansion of J(n,x) 
     * J(n,x) = 1/n!*(x/2)^n  - ...
     */
		if(n>33)	/* underflow */
		    b = zero;
		else {
		    temp = x*(float)0.5; b = temp;
		    for (a=one,i=2;i<=n;i++) {
			a *= (float)i;		/* a = n! */
			b *= temp;		/* b = (x/2)^n */
		    }
		    b = b/a;
		}
	    } else {
		/* use backward recurrence */
		/* 			x      x^2      x^2       
		 *  J(n,x)/J(n-1,x) =  ----   ------   ------   .....
		 *			2n  - 2(n+1) - 2(n+2)
		 *
		 * 			1      1        1       
		 *  (for large x)   =  ----  ------   ------   .....
		 *			2n   2(n+1)   2(n+2)
		 *			-- - ------ - ------ - 
		 *			 x     x         x
		 *
		 * Let w = 2n/x and h=2/x, then the above quotient
		 * is equal to the continued fraction:
		 *		    1
		 *	= -----------------------
		 *		       1
		 *	   w - -----------------
		 *			  1
		 * 	        w+h - ---------
		 *		       w+2h - ...
		 *
		 * To determine how many terms needed, let
		 * Q(0) = w, Q(1) = w(w+h) - 1,
		 * Q(k) = (w+k*h)*Q(k-1) - Q(k-2),
		 * When Q(k) > 1e4	good for single 
		 * When Q(k) > 1e9	good for double 
		 * When Q(k) > 1e17	good for quadruple 
		 */
	    /* determine k */
		float t,v;
		float q0,q1,h,tmp; int32_t k,m;
		w  = (n+n)/(float)x; h = (float)2.0/(float)x;
		q0 = w;  z = w+h; q1 = w*z - (float)1.0; k=1;
		while(q1<(float)1.0e9) {
			k += 1; z += h;
			tmp = z*q1 - q0;
			q0 = q1;
			q1 = tmp;
		}
		m = n+n;
		for(t=zero, i = 2*(n+k); i>=m; i -= 2) t = one/(i/x-t);
		a = t;
		b = one;
		/*  estimate log((2/x)^n*n!) = n*log(2/x)+n*ln(n)
		 *  Hence, if n*(log(2n/x)) > ...
		 *  single 8.8722839355e+01
		 *  double 7.09782712893383973096e+02
		 *  long double 1.1356523406294143949491931077970765006170e+04
		 *  then recurrent value may overflow and the result is 
		 *  likely underflow to zero
		 */
		tmp = n;
		v = two/x;
		tmp = tmp*logf(fabsf(v*tmp));
		if(tmp<(float)8.8721679688e+01) {
	    	    for(i=n-1,di=(float)(i+i);i>0;i--){
		        temp = b;
			b *= di;
			b  = b/x - a;
		        a = temp;
			di -= two;
	     	    }
		} else {
	    	    for(i=n-1,di=(float)(i+i);i>0;i--){
		        temp = b;
			b *= di;
			b  = b/x - a;
		        a = temp;
			di -= two;
		    /* scale b to avoid spurious overflow */
			if(b>(float)1e10) {
			    a /= b;
			    t /= b;
			    b  = one;
			}
	     	    }
		}
	    	b = (t*j0f(x)/b);
	    }
	}
	if(sgn==1) return -b; else return b;
}

float
ynf(int n, float x) 
{
	int32_t i,hx,ix,ib;
	int32_t sign;
	float a, b, temp;

	GET_FLOAT_WORD(hx,x);
	ix = 0x7fffffff&hx;
    /* if Y(n,NaN) is NaN */
	if(ix>0x7f800000) return x+x;
	if(ix==0) return -one/zero;
	if(hx<0) return zero/zero;
	sign = 1;
	if(n<0){
		n = -n;
		sign = 1 - ((n&1)<<1);
	}
	if(n==0) return(y0f(x));
	if(n==1) return(sign*y1f(x));
	if(ix==0x7f800000) return zero;

	a = y0f(x);
	b = y1f(x);
	/* quit if b is -inf */
	GET_FLOAT_WORD(ib,b);
	for(i=1;i<n&&ib!=0xff800000;i++){ 
	    temp = b;
	    b = ((float)(i+i)/x)*b - a;
	    GET_FLOAT_WORD(ib,b);
	    a = temp;
	}
	if(sign>0) return b; else return -b;
}
