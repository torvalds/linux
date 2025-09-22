/*	$OpenBSD: e_log2l.c,v 1.3 2017/01/21 08:29:13 krw Exp $	*/

/*
 * Copyright (c) 2008 Stephen L. Moshier <steve@moshier.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*							log2l.c
 *
 *	Base 2 logarithm, long double precision
 *
 *
 *
 * SYNOPSIS:
 *
 * long double x, y, log2l();
 *
 * y = log2l( x );
 *
 *
 *
 * DESCRIPTION:
 *
 * Returns the base 2 logarithm of x.
 *
 * The argument is separated into its exponent and fractional
 * parts.  If the exponent is between -1 and +1, the (natural)
 * logarithm of the fraction is approximated by
 *
 *     log(1+x) = x - 0.5 x**2 + x**3 P(x)/Q(x).
 *
 * Otherwise, setting  z = 2(x-1)/x+1),
 *
 *     log(x) = z + z**3 P(z)/Q(z).
 *
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      0.5, 2.0     30000      9.8e-20     2.7e-20
 *    IEEE     exp(+-10000)  70000      5.4e-20     2.3e-20
 *
 * In the tests over the interval exp(+-10000), the logarithms
 * of the random arguments were uniformly distributed over
 * [-10000, +10000].
 *
 * ERROR MESSAGES:
 *
 * log singularity:  x = 0; returns -INFINITY
 * log domain:       x < 0; returns NAN
 */

#include <math.h>

#include "math_private.h"

/* Coefficients for ln(1+x) = x - x**2/2 + x**3 P(x)/Q(x)
 * 1/sqrt(2) <= x < sqrt(2)
 * Theoretical peak relative error = 6.2e-22
 */
static long double P[] = {
 4.9962495940332550844739E-1L,
 1.0767376367209449010438E1L,
 7.7671073698359539859595E1L,
 2.5620629828144409632571E2L,
 4.2401812743503691187826E2L,
 3.4258224542413922935104E2L,
 1.0747524399916215149070E2L,
};
static long double Q[] = {
/* 1.0000000000000000000000E0,*/
 2.3479774160285863271658E1L,
 1.9444210022760132894510E2L,
 7.7952888181207260646090E2L,
 1.6911722418503949084863E3L,
 2.0307734695595183428202E3L,
 1.2695660352705325274404E3L,
 3.2242573199748645407652E2L,
};

/* Coefficients for log(x) = z + z^3 P(z^2)/Q(z^2),
 * where z = 2(x-1)/(x+1)
 * 1/sqrt(2) <= x < sqrt(2)
 * Theoretical peak relative error = 6.16e-22
 */
static long double R[4] = {
 1.9757429581415468984296E-3L,
-7.1990767473014147232598E-1L,
 1.0777257190312272158094E1L,
-3.5717684488096787370998E1L,
};
static long double S[4] = {
/* 1.00000000000000000000E0L,*/
-2.6201045551331104417768E1L,
 1.9361891836232102174846E2L,
-4.2861221385716144629696E2L,
};
/* log2(e) - 1 */
#define LOG2EA 4.4269504088896340735992e-1L

#define SQRTH 0.70710678118654752440L

long double
log2l(long double x)
{
volatile long double z;
long double y;
int e;

if( isnan(x) )
	return(x);
if( x == INFINITY )
	return(x);
/* Test for domain */
if( x <= 0.0L )
	{
	if( x == 0.0L )
		return( -INFINITY );
	else
		return( NAN );
	}

/* separate mantissa from exponent */

/* Note, frexp is used so that denormal numbers
 * will be handled properly.
 */
x = frexpl( x, &e );


/* logarithm using log(x) = z + z**3 P(z)/Q(z),
 * where z = 2(x-1)/x+1)
 */
if( (e > 2) || (e < -2) )
{
if( x < SQRTH )
	{ /* 2( 2x-1 )/( 2x+1 ) */
	e -= 1;
	z = x - 0.5L;
	y = 0.5L * z + 0.5L;
	}
else
	{ /*  2 (x-1)/(x+1)   */
	z = x - 0.5L;
	z -= 0.5L;
	y = 0.5L * x  + 0.5L;
	}
x = z / y;
z = x*x;
y = x * ( z * __polevll( z, R, 3 ) / __p1evll( z, S, 3 ) );
goto done;
}


/* logarithm using log(1+x) = x - .5x**2 + x**3 P(x)/Q(x) */

if( x < SQRTH )
	{
	e -= 1;
	x = ldexpl( x, 1 ) - 1.0L; /*  2x - 1  */
	}
else
	{
	x = x - 1.0L;
	}
z = x*x;
y = x * ( z * __polevll( x, P, 6 ) / __p1evll( x, Q, 7 ) );
y = y - ldexpl( z, -1 );   /* -0.5x^2 + ... */

done:

/* Multiply log of fraction by log2(e)
 * and base 2 exponent by 1
 *
 * ***CAUTION***
 *
 * This sequence of operations is critical and it may
 * be horribly defeated by some compiler optimizers.
 */
z = y * LOG2EA;
z += x * LOG2EA;
z += y;
z += x;
z += e;
return( z );
}
