/* @(#)e_sqrt.c 5.1 93/09/24 */
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

/* sqrt(x)
 * Return correctly rounded sqrt.
 *           ------------------------------------------
 *	     |  Use the hardware sqrt if you have one |
 *           ------------------------------------------
 * Method: 
 *   Bit by bit method using integer arithmetic. (Slow, but portable) 
 *   1. Normalization
 *	Scale x to y in [1,4) with even powers of 2: 
 *	find an integer k such that  1 <= (y=x*2^(2k)) < 4, then
 *		sqrt(x) = 2^k * sqrt(y)
 *   2. Bit by bit computation
 *	Let q  = sqrt(y) truncated to i bit after binary point (q = 1),
 *	     i							 0
 *                                     i+1         2
 *	    s  = 2*q , and	y  =  2   * ( y - q  ).		(1)
 *	     i      i            i                 i
 *                                                        
 *	To compute q    from q , one checks whether 
 *		    i+1       i                       
 *
 *			      -(i+1) 2
 *			(q + 2      ) <= y.			(2)
 *     			  i
 *							      -(i+1)
 *	If (2) is false, then q   = q ; otherwise q   = q  + 2      .
 *		 	       i+1   i             i+1   i
 *
 *	With some algebraic manipulation, it is not difficult to see
 *	that (2) is equivalent to 
 *                             -(i+1)
 *			s  +  2       <= y			(3)
 *			 i                i
 *
 *	The advantage of (3) is that s  and y  can be computed by 
 *				      i      i
 *	the following recurrence formula:
 *	    if (3) is false
 *
 *	    s     =  s  ,	y    = y   ;			(4)
 *	     i+1      i		 i+1    i
 *
 *	    otherwise,
 *                         -i                     -(i+1)
 *	    s	  =  s  + 2  ,  y    = y  -  s  - 2  		(5)
 *           i+1      i          i+1    i     i
 *				
 *	One may easily use induction to prove (4) and (5). 
 *	Note. Since the left hand side of (3) contain only i+2 bits,
 *	      it does not necessary to do a full (53-bit) comparison 
 *	      in (3).
 *   3. Final rounding
 *	After generating the 53 bits result, we compute one more bit.
 *	Together with the remainder, we can decide whether the
 *	result is exact, bigger than 1/2ulp, or less than 1/2ulp
 *	(it will never equal to 1/2ulp).
 *	The rounding mode can be detected by checking whether
 *	huge + tiny is equal to huge, and whether huge - tiny is
 *	equal to huge for some floating point number "huge" and "tiny".
 *		
 * Special cases:
 *	sqrt(+-0) = +-0 	... exact
 *	sqrt(inf) = inf
 *	sqrt(-ve) = NaN		... with invalid signal
 *	sqrt(NaN) = NaN		... with invalid signal for signaling NaN
 *
 * Other methods : see the appended file at the end of the program below.
 *---------------
 */

#include <float.h>
#include <math.h>

#include "math_private.h"

static	const double	one	= 1.0, tiny=1.0e-300;

double
sqrt(double x)
{
	double z;
	int32_t sign = (int)0x80000000; 
	int32_t ix0,s0,q,m,t,i;
	u_int32_t r,t1,s1,ix1,q1;

	EXTRACT_WORDS(ix0,ix1,x);

    /* take care of Inf and NaN */
	if((ix0&0x7ff00000)==0x7ff00000) {			
	    return x*x+x;		/* sqrt(NaN)=NaN, sqrt(+inf)=+inf
					   sqrt(-inf)=sNaN */
	} 
    /* take care of zero */
	if(ix0<=0) {
	    if(((ix0&(~sign))|ix1)==0) return x;/* sqrt(+-0) = +-0 */
	    else if(ix0<0)
		return (x-x)/(x-x);		/* sqrt(-ve) = sNaN */
	}
    /* normalize x */
	m = (ix0>>20);
	if(m==0) {				/* subnormal x */
	    while(ix0==0) {
		m -= 21;
		ix0 |= (ix1>>11); ix1 <<= 21;
	    }
	    for(i=0;(ix0&0x00100000)==0;i++) ix0<<=1;
	    m -= i-1;
	    ix0 |= (ix1>>(32-i));
	    ix1 <<= i;
	}
	m -= 1023;	/* unbias exponent */
	ix0 = (ix0&0x000fffff)|0x00100000;
	if(m&1){	/* odd m, double x to make it even */
	    ix0 += ix0 + ((ix1&sign)>>31);
	    ix1 += ix1;
	}
	m >>= 1;	/* m = [m/2] */

    /* generate sqrt(x) bit by bit */
	ix0 += ix0 + ((ix1&sign)>>31);
	ix1 += ix1;
	q = q1 = s0 = s1 = 0;	/* [q,q1] = sqrt(x) */
	r = 0x00200000;		/* r = moving bit from right to left */

	while(r!=0) {
	    t = s0+r; 
	    if(t<=ix0) { 
		s0   = t+r; 
		ix0 -= t; 
		q   += r; 
	    } 
	    ix0 += ix0 + ((ix1&sign)>>31);
	    ix1 += ix1;
	    r>>=1;
	}

	r = sign;
	while(r!=0) {
	    t1 = s1+r; 
	    t  = s0;
	    if((t<ix0)||((t==ix0)&&(t1<=ix1))) { 
		s1  = t1+r;
		if(((t1&sign)==sign)&&(s1&sign)==0) s0 += 1;
		ix0 -= t;
		if (ix1 < t1) ix0 -= 1;
		ix1 -= t1;
		q1  += r;
	    }
	    ix0 += ix0 + ((ix1&sign)>>31);
	    ix1 += ix1;
	    r>>=1;
	}

    /* use floating add to find out rounding direction */
	if((ix0|ix1)!=0) {
	    z = one-tiny; /* trigger inexact flag */
	    if (z>=one) {
	        z = one+tiny;
	        if (q1==(u_int32_t)0xffffffff) { q1=0; q += 1;}
		else if (z>one) {
		    if (q1==(u_int32_t)0xfffffffe) q+=1;
		    q1+=2; 
		} else
	            q1 += (q1&1);
	    }
	}
	ix0 = (q>>1)+0x3fe00000;
	ix1 =  q1>>1;
	if ((q&1)==1) ix1 |= sign;
	ix0 += (m <<20);
	INSERT_WORDS(z,ix0,ix1);
	return z;
}
DEF_STD(sqrt);
LDBL_MAYBE_CLONE(sqrt);

/*
Other methods  (use floating-point arithmetic)
-------------
(This is a copy of a drafted paper by Prof W. Kahan 
and K.C. Ng, written in May, 1986)

	Two algorithms are given here to implement sqrt(x) 
	(IEEE double precision arithmetic) in software.
	Both supply sqrt(x) correctly rounded. The first algorithm (in
	Section A) uses newton iterations and involves four divisions.
	The second one uses reciproot iterations to avoid division, but
	requires more multiplications. Both algorithms need the ability
	to chop results of arithmetic operations instead of round them, 
	and the INEXACT flag to indicate when an arithmetic operation
	is executed exactly with no roundoff error, all part of the 
	standard (IEEE 754-1985). The ability to perform shift, add,
	subtract and logical AND operations upon 32-bit words is needed
	too, though not part of the standard.

A.  sqrt(x) by Newton Iteration

   (1)	Initial approximation

	Let x0 and x1 be the leading and the trailing 32-bit words of
	a floating point number x (in IEEE double format) respectively 

	    1    11		     52				  ...widths
	   ------------------------------------------------------
	x: |s|	  e     |	      f				|
	   ------------------------------------------------------
	      msb    lsb  msb				      lsb ...order

 
	     ------------------------  	     ------------------------
	x0:  |s|   e    |    f1     |	 x1: |          f2           |
	     ------------------------  	     ------------------------

	By performing shifts and subtracts on x0 and x1 (both regarded
	as integers), we obtain an 8-bit approximation of sqrt(x) as
	follows.

		k  := (x0>>1) + 0x1ff80000;
		y0 := k - T1[31&(k>>15)].	... y ~ sqrt(x) to 8 bits
	Here k is a 32-bit integer and T1[] is an integer array containing
	correction terms. Now magically the floating value of y (y's
	leading 32-bit word is y0, the value of its trailing word is 0)
	approximates sqrt(x) to almost 8-bit.

	Value of T1:
	static int T1[32]= {
	0,	1024,	3062,	5746,	9193,	13348,	18162,	23592,
	29598,	36145,	43202,	50740,	58733,	67158,	75992,	85215,
	83599,	71378,	60428,	50647,	41945,	34246,	27478,	21581,
	16499,	12183,	8588,	5674,	3403,	1742,	661,	130,};

    (2)	Iterative refinement

	Apply Heron's rule three times to y, we have y approximates 
	sqrt(x) to within 1 ulp (Unit in the Last Place):

		y := (y+x/y)/2		... almost 17 sig. bits
		y := (y+x/y)/2		... almost 35 sig. bits
		y := y-(y-x/y)/2	... within 1 ulp


	Remark 1.
	    Another way to improve y to within 1 ulp is:

		y := (y+x/y)		... almost 17 sig. bits to 2*sqrt(x)
		y := y - 0x00100006	... almost 18 sig. bits to sqrt(x)

				2
			    (x-y )*y
		y := y + 2* ----------	...within 1 ulp
			       2
			     3y  + x


	This formula has one division fewer than the one above; however,
	it requires more multiplications and additions. Also x must be
	scaled in advance to avoid spurious overflow in evaluating the
	expression 3y*y+x. Hence it is not recommended unless division
	is slow. If division is very slow, then one should use the 
	reciproot algorithm given in section B.

    (3) Final adjustment

	By twiddling y's last bit it is possible to force y to be 
	correctly rounded according to the prevailing rounding mode
	as follows. Let r and i be copies of the rounding mode and
	inexact flag before entering the square root program. Also we
	use the expression y+-ulp for the next representable floating
	numbers (up and down) of y. Note that y+-ulp = either fixed
	point y+-1, or multiply y by nextafter(1,+-inf) in chopped
	mode.

		I := FALSE;	... reset INEXACT flag I
		R := RZ;	... set rounding mode to round-toward-zero
		z := x/y;	... chopped quotient, possibly inexact
		If(not I) then {	... if the quotient is exact
		    if(z=y) {
		        I := i;	 ... restore inexact flag
		        R := r;  ... restore rounded mode
		        return sqrt(x):=y.
		    } else {
			z := z - ulp;	... special rounding
		    }
		}
		i := TRUE;		... sqrt(x) is inexact
		If (r=RN) then z=z+ulp	... rounded-to-nearest
		If (r=RP) then {	... round-toward-+inf
		    y = y+ulp; z=z+ulp;
		}
		y := y+z;		... chopped sum
		y0:=y0-0x00100000;	... y := y/2 is correctly rounded.
	        I := i;	 		... restore inexact flag
	        R := r;  		... restore rounded mode
	        return sqrt(x):=y.
		    
    (4)	Special cases

	Square root of +inf, +-0, or NaN is itself;
	Square root of a negative number is NaN with invalid signal.


B.  sqrt(x) by Reciproot Iteration

   (1)	Initial approximation

	Let x0 and x1 be the leading and the trailing 32-bit words of
	a floating point number x (in IEEE double format) respectively
	(see section A). By performing shifs and subtracts on x0 and y0,
	we obtain a 7.8-bit approximation of 1/sqrt(x) as follows.

	    k := 0x5fe80000 - (x0>>1);
	    y0:= k - T2[63&(k>>14)].	... y ~ 1/sqrt(x) to 7.8 bits

	Here k is a 32-bit integer and T2[] is an integer array 
	containing correction terms. Now magically the floating
	value of y (y's leading 32-bit word is y0, the value of
	its trailing word y1 is set to zero) approximates 1/sqrt(x)
	to almost 7.8-bit.

	Value of T2:
	static int T2[64]= {
	0x1500,	0x2ef8,	0x4d67,	0x6b02,	0x87be,	0xa395,	0xbe7a,	0xd866,
	0xf14a,	0x1091b,0x11fcd,0x13552,0x14999,0x15c98,0x16e34,0x17e5f,
	0x18d03,0x19a01,0x1a545,0x1ae8a,0x1b5c4,0x1bb01,0x1bfde,0x1c28d,
	0x1c2de,0x1c0db,0x1ba73,0x1b11c,0x1a4b5,0x1953d,0x18266,0x16be0,
	0x1683e,0x179d8,0x18a4d,0x19992,0x1a789,0x1b445,0x1bf61,0x1c989,
	0x1d16d,0x1d77b,0x1dddf,0x1e2ad,0x1e5bf,0x1e6e8,0x1e654,0x1e3cd,
	0x1df2a,0x1d635,0x1cb16,0x1be2c,0x1ae4e,0x19bde,0x1868e,0x16e2e,
	0x1527f,0x1334a,0x11051,0xe951,	0xbe01,	0x8e0d,	0x5924,	0x1edd,};

    (2)	Iterative refinement

	Apply Reciproot iteration three times to y and multiply the
	result by x to get an approximation z that matches sqrt(x)
	to about 1 ulp. To be exact, we will have 
		-1ulp < sqrt(x)-z<1.0625ulp.
	
	... set rounding mode to Round-to-nearest
	   y := y*(1.5-0.5*x*y*y)	... almost 15 sig. bits to 1/sqrt(x)
	   y := y*((1.5-2^-30)+0.5*x*y*y)... about 29 sig. bits to 1/sqrt(x)
	... special arrangement for better accuracy
	   z := x*y			... 29 bits to sqrt(x), with z*y<1
	   z := z + 0.5*z*(1-z*y)	... about 1 ulp to sqrt(x)

	Remark 2. The constant 1.5-2^-30 is chosen to bias the error so that
	(a) the term z*y in the final iteration is always less than 1; 
	(b) the error in the final result is biased upward so that
		-1 ulp < sqrt(x) - z < 1.0625 ulp
	    instead of |sqrt(x)-z|<1.03125ulp.

    (3)	Final adjustment

	By twiddling y's last bit it is possible to force y to be 
	correctly rounded according to the prevailing rounding mode
	as follows. Let r and i be copies of the rounding mode and
	inexact flag before entering the square root program. Also we
	use the expression y+-ulp for the next representable floating
	numbers (up and down) of y. Note that y+-ulp = either fixed
	point y+-1, or multiply y by nextafter(1,+-inf) in chopped
	mode.

	R := RZ;		... set rounding mode to round-toward-zero
	switch(r) {
	    case RN:		... round-to-nearest
	       if(x<= z*(z-ulp)...chopped) z = z - ulp; else
	       if(x<= z*(z+ulp)...chopped) z = z; else z = z+ulp;
	       break;
	    case RZ:case RM:	... round-to-zero or round-to--inf
	       R:=RP;		... reset rounding mod to round-to-+inf
	       if(x<z*z ... rounded up) z = z - ulp; else
	       if(x>=(z+ulp)*(z+ulp) ...rounded up) z = z+ulp;
	       break;
	    case RP:		... round-to-+inf
	       if(x>(z+ulp)*(z+ulp)...chopped) z = z+2*ulp; else
	       if(x>z*z ...chopped) z = z+ulp;
	       break;
	}

	Remark 3. The above comparisons can be done in fixed point. For
	example, to compare x and w=z*z chopped, it suffices to compare
	x1 and w1 (the trailing parts of x and w), regarding them as
	two's complement integers.

	...Is z an exact square root?
	To determine whether z is an exact square root of x, let z1 be the
	trailing part of z, and also let x0 and x1 be the leading and
	trailing parts of x.

	If ((z1&0x03ffffff)!=0)	... not exact if trailing 26 bits of z!=0
	    I := 1;		... Raise Inexact flag: z is not exact
	else {
	    j := 1 - [(x0>>20)&1]	... j = logb(x) mod 2
	    k := z1 >> 26;		... get z's 25-th and 26-th 
					    fraction bits
	    I := i or (k&j) or ((k&(j+j+1))!=(x1&3));
	}
	R:= r		... restore rounded mode
	return sqrt(x):=z.

	If multiplication is cheaper then the foregoing red tape, the 
	Inexact flag can be evaluated by

	    I := i;
	    I := (z*z!=x) or I.

	Note that z*z can overwrite I; this value must be sensed if it is 
	True.

	Remark 4. If z*z = x exactly, then bit 25 to bit 0 of z1 must be
	zero.

		    --------------------
		z1: |        f2        | 
		    --------------------
		bit 31		   bit 0

	Further more, bit 27 and 26 of z1, bit 0 and 1 of x1, and the odd
	or even of logb(x) have the following relations:

	-------------------------------------------------
	bit 27,26 of z1		bit 1,0 of x1	logb(x)
	-------------------------------------------------
	00			00		odd and even
	01			01		even
	10			10		odd
	10			00		even
	11			01		even
	-------------------------------------------------

    (4)	Special cases (see (4) of Section A).	
 
 */
