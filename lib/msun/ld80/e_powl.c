/*-
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <math.h>

#include "math_private.h"

/*
 * Polynomial evaluator:
 *  P[0] x^n  +  P[1] x^(n-1)  +  ...  +  P[n]
 */
static inline long double
__polevll(long double x, long double *PP, int n)
{
	long double y;
	long double *P;

	P = PP;
	y = *P++;
	do {
		y = y * x + *P++;
	} while (--n);

	return (y);
}

/*
 * Polynomial evaluator:
 *  x^n  +  P[0] x^(n-1)  +  P[1] x^(n-2)  +  ...  +  P[n]
 */
static inline long double
__p1evll(long double x, long double *PP, int n)
{
	long double y;
	long double *P;

	P = PP;
	n -= 1;
	y = x + *P++;
	do {
		y = y * x + *P++;
	} while (--n);

	return (y);
}

/*							powl.c
 *
 *	Power function, long double precision
 *
 *
 *
 * SYNOPSIS:
 *
 * long double x, y, z, powl();
 *
 * z = powl( x, y );
 *
 *
 *
 * DESCRIPTION:
 *
 * Computes x raised to the yth power.  Analytically,
 *
 *      x**y  =  exp( y log(x) ).
 *
 * Following Cody and Waite, this program uses a lookup table
 * of 2**-i/32 and pseudo extended precision arithmetic to
 * obtain several extra bits of accuracy in both the logarithm
 * and the exponential.
 *
 *
 *
 * ACCURACY:
 *
 * The relative error of pow(x,y) can be estimated
 * by   y dl ln(2),   where dl is the absolute error of
 * the internally computed base 2 logarithm.  At the ends
 * of the approximation interval the logarithm equal 1/32
 * and its relative error is about 1 lsb = 1.1e-19.  Hence
 * the predicted relative error in the result is 2.3e-21 y .
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *
 *    IEEE     +-1000       40000      2.8e-18      3.7e-19
 * .001 < x < 1000, with log(x) uniformly distributed.
 * -1000 < y < 1000, y uniformly distributed.
 *
 *    IEEE     0,8700       60000      6.5e-18      1.0e-18
 * 0.99 < x < 1.01, 0 < y < 8700, uniformly distributed.
 *
 *
 * ERROR MESSAGES:
 *
 *   message         condition      value returned
 * pow overflow     x**y > MAXNUM      INFINITY
 * pow underflow   x**y < 1/MAXNUM       0.0
 * pow domain      x<0 and y noninteger  0.0
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <float.h>
#include <math.h>

#include "math_private.h"

/* Table size */
#define NXT 32
/* log2(Table size) */
#define LNXT 5

/* log(1+x) =  x - .5x^2 + x^3 *  P(z)/Q(z)
 * on the domain  2^(-1/32) - 1  <=  x  <=  2^(1/32) - 1
 */
static long double P[] = {
 8.3319510773868690346226E-4L,
 4.9000050881978028599627E-1L,
 1.7500123722550302671919E0L,
 1.4000100839971580279335E0L,
};
static long double Q[] = {
/* 1.0000000000000000000000E0L,*/
 5.2500282295834889175431E0L,
 8.4000598057587009834666E0L,
 4.2000302519914740834728E0L,
};
/* A[i] = 2^(-i/32), rounded to IEEE long double precision.
 * If i is even, A[i] + B[i/2] gives additional accuracy.
 */
static long double A[33] = {
 1.0000000000000000000000E0L,
 9.7857206208770013448287E-1L,
 9.5760328069857364691013E-1L,
 9.3708381705514995065011E-1L,
 9.1700404320467123175367E-1L,
 8.9735453750155359320742E-1L,
 8.7812608018664974155474E-1L,
 8.5930964906123895780165E-1L,
 8.4089641525371454301892E-1L,
 8.2287773907698242225554E-1L,
 8.0524516597462715409607E-1L,
 7.8799042255394324325455E-1L,
 7.7110541270397041179298E-1L,
 7.5458221379671136985669E-1L,
 7.3841307296974965571198E-1L,
 7.2259040348852331001267E-1L,
 7.0710678118654752438189E-1L,
 6.9195494098191597746178E-1L,
 6.7712777346844636413344E-1L,
 6.6261832157987064729696E-1L,
 6.4841977732550483296079E-1L,
 6.3452547859586661129850E-1L,
 6.2092890603674202431705E-1L,
 6.0762367999023443907803E-1L,
 5.9460355750136053334378E-1L,
 5.8186242938878875689693E-1L,
 5.6939431737834582684856E-1L,
 5.5719337129794626814472E-1L,
 5.4525386633262882960438E-1L,
 5.3357020033841180906486E-1L,
 5.2213689121370692017331E-1L,
 5.1094857432705833910408E-1L,
 5.0000000000000000000000E-1L,
};
static long double B[17] = {
 0.0000000000000000000000E0L,
 2.6176170809902549338711E-20L,
-1.0126791927256478897086E-20L,
 1.3438228172316276937655E-21L,
 1.2207982955417546912101E-20L,
-6.3084814358060867200133E-21L,
 1.3164426894366316434230E-20L,
-1.8527916071632873716786E-20L,
 1.8950325588932570796551E-20L,
 1.5564775779538780478155E-20L,
 6.0859793637556860974380E-21L,
-2.0208749253662532228949E-20L,
 1.4966292219224761844552E-20L,
 3.3540909728056476875639E-21L,
-8.6987564101742849540743E-22L,
-1.2327176863327626135542E-20L,
 0.0000000000000000000000E0L,
};

/* 2^x = 1 + x P(x),
 * on the interval -1/32 <= x <= 0
 */
static long double R[] = {
 1.5089970579127659901157E-5L,
 1.5402715328927013076125E-4L,
 1.3333556028915671091390E-3L,
 9.6181291046036762031786E-3L,
 5.5504108664798463044015E-2L,
 2.4022650695910062854352E-1L,
 6.9314718055994530931447E-1L,
};

#define douba(k) A[k]
#define doubb(k) B[k]
#define MEXP (NXT*16384.0L)
/* The following if denormal numbers are supported, else -MEXP: */
#define MNEXP (-NXT*(16384.0L+64.0L))
/* log2(e) - 1 */
#define LOG2EA 0.44269504088896340735992L

#define F W
#define Fa Wa
#define Fb Wb
#define G W
#define Ga Wa
#define Gb u
#define H W
#define Ha Wb
#define Hb Wb

static const long double MAXLOGL = 1.1356523406294143949492E4L;
static const long double MINLOGL = -1.13994985314888605586758E4L;
static const long double LOGE2L = 6.9314718055994530941723E-1L;
static volatile long double z;
static long double w, W, Wa, Wb, ya, yb, u;
static const long double huge = 0x1p10000L;
#if 0 /* XXX Prevent gcc from erroneously constant folding this. */
static const long double twom10000 = 0x1p-10000L;
#else
static volatile long double twom10000 = 0x1p-10000L;
#endif

static long double reducl( long double );
static long double powil ( long double, int );

long double
powl(long double x, long double y)
{
/* double F, Fa, Fb, G, Ga, Gb, H, Ha, Hb */
int i, nflg, iyflg, yoddint;
long e;

if( y == 0.0L )
	return( 1.0L );

if( x == 1.0L )
	return( 1.0L );

if( isnan(x) )
	return ( nan_mix(x, y) );
if( isnan(y) )
	return ( nan_mix(x, y) );

if( y == 1.0L )
	return( x );

if( !isfinite(y) && x == -1.0L )
	return( 1.0L );

if( y >= LDBL_MAX )
	{
	if( x > 1.0L )
		return( INFINITY );
	if( x > 0.0L && x < 1.0L )
		return( 0.0L );
	if( x < -1.0L )
		return( INFINITY );
	if( x > -1.0L && x < 0.0L )
		return( 0.0L );
	}
if( y <= -LDBL_MAX )
	{
	if( x > 1.0L )
		return( 0.0L );
	if( x > 0.0L && x < 1.0L )
		return( INFINITY );
	if( x < -1.0L )
		return( 0.0L );
	if( x > -1.0L && x < 0.0L )
		return( INFINITY );
	}
if( x >= LDBL_MAX )
	{
	if( y > 0.0L )
		return( INFINITY );
	return( 0.0L );
	}

w = floorl(y);
/* Set iyflg to 1 if y is an integer.  */
iyflg = 0;
if( w == y )
	iyflg = 1;

/* Test for odd integer y.  */
yoddint = 0;
if( iyflg )
	{
	ya = fabsl(y);
	ya = floorl(0.5L * ya);
	yb = 0.5L * fabsl(w);
	if( ya != yb )
		yoddint = 1;
	}

if( x <= -LDBL_MAX )
	{
	if( y > 0.0L )
		{
		if( yoddint )
			return( -INFINITY );
		return( INFINITY );
		}
	if( y < 0.0L )
		{
		if( yoddint )
			return( -0.0L );
		return( 0.0 );
		}
	}


nflg = 0;	/* flag = 1 if x<0 raised to integer power */
if( x <= 0.0L )
	{
	if( x == 0.0L )
		{
		if( y < 0.0 )
			{
			if( signbit(x) && yoddint )
				return( -INFINITY );
			return( INFINITY );
			}
		if( y > 0.0 )
			{
			if( signbit(x) && yoddint )
				return( -0.0L );
			return( 0.0 );
			}
		if( y == 0.0L )
			return( 1.0L );  /*   0**0   */
		else
			return( 0.0L );  /*   0**y   */
		}
	else
		{
		if( iyflg == 0 )
			return (x - x) / (x - x); /* (x<0)**(non-int) is NaN */
		nflg = 1;
		}
	}

/* Integer power of an integer.  */

if( iyflg )
	{
	i = w;
	w = floorl(x);
	if( (w == x) && (fabsl(y) < 32768.0) )
		{
		w = powil( x, (int) y );
		return( w );
		}
	}


if( nflg )
	x = fabsl(x);

/* separate significand from exponent */
x = frexpl( x, &i );
e = i;

/* find significand in antilog table A[] */
i = 1;
if( x <= douba(17) )
	i = 17;
if( x <= douba(i+8) )
	i += 8;
if( x <= douba(i+4) )
	i += 4;
if( x <= douba(i+2) )
	i += 2;
if( x >= douba(1) )
	i = -1;
i += 1;


/* Find (x - A[i])/A[i]
 * in order to compute log(x/A[i]):
 *
 * log(x) = log( a x/a ) = log(a) + log(x/a)
 *
 * log(x/a) = log(1+v),  v = x/a - 1 = (x-a)/a
 */
x -= douba(i);
x -= doubb(i/2);
x /= douba(i);


/* rational approximation for log(1+v):
 *
 * log(1+v)  =  v  -  v**2/2  +  v**3 P(v) / Q(v)
 */
z = x*x;
w = x * ( z * __polevll( x, P, 3 ) / __p1evll( x, Q, 3 ) );
w = w - ldexpl( z, -1 );   /*  w - 0.5 * z  */

/* Convert to base 2 logarithm:
 * multiply by log2(e) = 1 + LOG2EA
 */
z = LOG2EA * w;
z += w;
z += LOG2EA * x;
z += x;

/* Compute exponent term of the base 2 logarithm. */
w = -i;
w = ldexpl( w, -LNXT );	/* divide by NXT */
w += e;
/* Now base 2 log of x is w + z. */

/* Multiply base 2 log by y, in extended precision. */

/* separate y into large part ya
 * and small part yb less than 1/NXT
 */
ya = reducl(y);
yb = y - ya;

/* (w+z)(ya+yb)
 * = w*ya + w*yb + z*y
 */
F = z * y  +  w * yb;
Fa = reducl(F);
Fb = F - Fa;

G = Fa + w * ya;
Ga = reducl(G);
Gb = G - Ga;

H = Fb + Gb;
Ha = reducl(H);
w = ldexpl( Ga+Ha, LNXT );

/* Test the power of 2 for overflow */
if( w > MEXP )
	return (huge * huge);		/* overflow */

if( w < MNEXP )
	return (twom10000 * twom10000);	/* underflow */

e = w;
Hb = H - Ha;

if( Hb > 0.0L )
	{
	e += 1;
	Hb -= (1.0L/NXT);  /*0.0625L;*/
	}

/* Now the product y * log2(x)  =  Hb + e/NXT.
 *
 * Compute base 2 exponential of Hb,
 * where -0.0625 <= Hb <= 0.
 */
z = Hb * __polevll( Hb, R, 6 );  /*    z  =  2**Hb - 1    */

/* Express e/NXT as an integer plus a negative number of (1/NXT)ths.
 * Find lookup table entry for the fractional power of 2.
 */
if( e < 0 )
	i = 0;
else
	i = 1;
i = e/NXT + i;
e = NXT*i - e;
w = douba( e );
z = w * z;      /*    2**-e * ( 1 + (2**Hb-1) )    */
z = z + w;
z = ldexpl( z, i );  /* multiply by integer power of 2 */

if( nflg )
	{
/* For negative x,
 * find out if the integer exponent
 * is odd or even.
 */
	w = ldexpl( y, -1 );
	w = floorl(w);
	w = ldexpl( w, 1 );
	if( w != y )
		z = -z; /* odd exponent */
	}

return( z );
}


/* Find a multiple of 1/NXT that is within 1/NXT of x. */
static inline long double
reducl(long double x)
{
long double t;

t = ldexpl( x, LNXT );
t = floorl( t );
t = ldexpl( t, -LNXT );
return(t);
}

/*							powil.c
 *
 *	Real raised to integer power, long double precision
 *
 *
 *
 * SYNOPSIS:
 *
 * long double x, y, powil();
 * int n;
 *
 * y = powil( x, n );
 *
 *
 *
 * DESCRIPTION:
 *
 * Returns argument x raised to the nth power.
 * The routine efficiently decomposes n as a sum of powers of
 * two. The desired power is a product of two-to-the-kth
 * powers of x.  Thus to compute the 32767 power of x requires
 * 28 multiplications instead of 32767 multiplications.
 *
 *
 *
 * ACCURACY:
 *
 *
 *                      Relative error:
 * arithmetic   x domain   n domain  # trials      peak         rms
 *    IEEE     .001,1000  -1022,1023  50000       4.3e-17     7.8e-18
 *    IEEE        1,2     -1022,1023  20000       3.9e-17     7.6e-18
 *    IEEE     .99,1.01     0,8700    10000       3.6e-16     7.2e-17
 *
 * Returns MAXNUM on overflow, zero on underflow.
 *
 */

static long double
powil(long double x, int nn)
{
long double ww, y;
long double s;
int n, e, sign, asign, lx;

if( x == 0.0L )
	{
	if( nn == 0 )
		return( 1.0L );
	else if( nn < 0 )
		return( LDBL_MAX );
	else
		return( 0.0L );
	}

if( nn == 0 )
	return( 1.0L );


if( x < 0.0L )
	{
	asign = -1;
	x = -x;
	}
else
	asign = 0;


if( nn < 0 )
	{
	sign = -1;
	n = -nn;
	}
else
	{
	sign = 1;
	n = nn;
	}

/* Overflow detection */

/* Calculate approximate logarithm of answer */
s = x;
s = frexpl( s, &lx );
e = (lx - 1)*n;
if( (e == 0) || (e > 64) || (e < -64) )
	{
	s = (s - 7.0710678118654752e-1L) / (s +  7.0710678118654752e-1L);
	s = (2.9142135623730950L * s - 0.5L + lx) * nn * LOGE2L;
	}
else
	{
	s = LOGE2L * e;
	}

if( s > MAXLOGL )
	return (huge * huge);		/* overflow */

if( s < MINLOGL )
	return (twom10000 * twom10000);	/* underflow */
/* Handle tiny denormal answer, but with less accuracy
 * since roundoff error in 1.0/x will be amplified.
 * The precise demarcation should be the gradual underflow threshold.
 */
if( s < (-MAXLOGL+2.0L) )
	{
	x = 1.0L/x;
	sign = -sign;
	}

/* First bit of the power */
if( n & 1 )
	y = x;

else
	{
	y = 1.0L;
	asign = 0;
	}

ww = x;
n >>= 1;
while( n )
	{
	ww = ww * ww;	/* arg to the 2-to-the-kth power */
	if( n & 1 )	/* if that bit is set, then include in product */
		y *= ww;
	n >>= 1;
	}

if( asign )
	y = -y; /* odd power of negative number */
if( sign < 0 )
	y = 1.0L/y;
return(y);
}
