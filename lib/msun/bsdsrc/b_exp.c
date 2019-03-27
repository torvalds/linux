/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1985, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* @(#)exp.c	8.1 (Berkeley) 6/4/93 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");


/* EXP(X)
 * RETURN THE EXPONENTIAL OF X
 * DOUBLE PRECISION (IEEE 53 bits, VAX D FORMAT 56 BITS)
 * CODED IN C BY K.C. NG, 1/19/85;
 * REVISED BY K.C. NG on 2/6/85, 2/15/85, 3/7/85, 3/24/85, 4/16/85, 6/14/86.
 *
 * Required system supported functions:
 *	scalb(x,n)
 *	copysign(x,y)
 *	finite(x)
 *
 * Method:
 *	1. Argument Reduction: given the input x, find r and integer k such
 *	   that
 *	                   x = k*ln2 + r,  |r| <= 0.5*ln2 .
 *	   r will be represented as r := z+c for better accuracy.
 *
 *	2. Compute exp(r) by
 *
 *		exp(r) = 1 + r + r*R1/(2-R1),
 *	   where
 *		R1 = x - x^2*(p1+x^2*(p2+x^2*(p3+x^2*(p4+p5*x^2)))).
 *
 *	3. exp(x) = 2^k * exp(r) .
 *
 * Special cases:
 *	exp(INF) is INF, exp(NaN) is NaN;
 *	exp(-INF)=  0;
 *	for finite argument, only exp(0)=1 is exact.
 *
 * Accuracy:
 *	exp(x) returns the exponential of x nearly rounded. In a test run
 *	with 1,156,000 random arguments on a VAX, the maximum observed
 *	error was 0.869 ulps (units in the last place).
 */

#include "mathimpl.h"

static const double p1 = 0x1.555555555553ep-3;
static const double p2 = -0x1.6c16c16bebd93p-9;
static const double p3 = 0x1.1566aaf25de2cp-14;
static const double p4 = -0x1.bbd41c5d26bf1p-20;
static const double p5 = 0x1.6376972bea4d0p-25;
static const double ln2hi = 0x1.62e42fee00000p-1;
static const double ln2lo = 0x1.a39ef35793c76p-33;
static const double lnhuge = 0x1.6602b15b7ecf2p9;
static const double lntiny = -0x1.77af8ebeae354p9;
static const double invln2 = 0x1.71547652b82fep0;

#if 0
double exp(x)
double x;
{
	double  z,hi,lo,c;
	int k;

#if !defined(vax)&&!defined(tahoe)
	if(x!=x) return(x);	/* x is NaN */
#endif	/* !defined(vax)&&!defined(tahoe) */
	if( x <= lnhuge ) {
		if( x >= lntiny ) {

		    /* argument reduction : x --> x - k*ln2 */

			k=invln2*x+copysign(0.5,x);	/* k=NINT(x/ln2) */

		    /* express x-k*ln2 as hi-lo and let x=hi-lo rounded */

			hi=x-k*ln2hi;
			x=hi-(lo=k*ln2lo);

		    /* return 2^k*[1+x+x*c/(2+c)]  */
			z=x*x;
			c= x - z*(p1+z*(p2+z*(p3+z*(p4+z*p5))));
			return  scalb(1.0+(hi-(lo-(x*c)/(2.0-c))),k);

		}
		/* end of x > lntiny */

		else
		     /* exp(-big#) underflows to zero */
		     if(finite(x))  return(scalb(1.0,-5000));

		     /* exp(-INF) is zero */
		     else return(0.0);
	}
	/* end of x < lnhuge */

	else
	/* exp(INF) is INF, exp(+big#) overflows to INF */
	    return( finite(x) ?  scalb(1.0,5000)  : x);
}
#endif

/* returns exp(r = x + c) for |c| < |x| with no overlap.  */

double __exp__D(x, c)
double x, c;
{
	double  z,hi,lo;
	int k;

	if (x != x)	/* x is NaN */
		return(x);
	if ( x <= lnhuge ) {
		if ( x >= lntiny ) {

		    /* argument reduction : x --> x - k*ln2 */
			z = invln2*x;
			k = z + copysign(.5, x);

		    /* express (x+c)-k*ln2 as hi-lo and let x=hi-lo rounded */

			hi=(x-k*ln2hi);			/* Exact. */
			x= hi - (lo = k*ln2lo-c);
		    /* return 2^k*[1+x+x*c/(2+c)]  */
			z=x*x;
			c= x - z*(p1+z*(p2+z*(p3+z*(p4+z*p5))));
			c = (x*c)/(2.0-c);

			return  scalb(1.+(hi-(lo - c)), k);
		}
		/* end of x > lntiny */

		else
		     /* exp(-big#) underflows to zero */
		     if(finite(x))  return(scalb(1.0,-5000));

		     /* exp(-INF) is zero */
		     else return(0.0);
	}
	/* end of x < lnhuge */

	else
	/* exp(INF) is INF, exp(+big#) overflows to INF */
	    return( finite(x) ?  scalb(1.0,5000)  : x);
}
