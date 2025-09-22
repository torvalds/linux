/*	$OpenBSD: s_expm1l.c,v 1.3 2016/09/12 19:47:03 guenther Exp $	*/

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

/*							expm1l.c
 *
 *	Exponential function, minus 1
 *      Long double precision
 *
 *
 * SYNOPSIS:
 *
 * long double x, y, expm1l();
 *
 * y = expm1l( x );
 *
 *
 *
 * DESCRIPTION:
 *
 * Returns e (2.71828...) raised to the x power, minus 1.
 *
 * Range reduction is accomplished by separating the argument
 * into an integer k and fraction f such that
 *
 *     x    k  f
 *    e  = 2  e.
 *
 * An expansion x + .5 x^2 + x^3 R(x) approximates exp(f) - 1
 * in the basic range [-0.5 ln 2, 0.5 ln 2].
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE    -45,+MAXLOG   200,000     1.2e-19     2.5e-20
 *
 * ERROR MESSAGES:
 *
 *   message         condition      value returned
 * expm1l overflow   x > MAXLOG         MAXNUM
 *
 */

#include <math.h>

static const long double MAXLOGL = 1.1356523406294143949492E4L;

/* exp(x) - 1 = x + 0.5 x^2 + x^3 P(x)/Q(x)
   -.5 ln 2  <  x  <  .5 ln 2
   Theoretical peak relative error = 3.4e-22  */

static const long double
  P0 = -1.586135578666346600772998894928250240826E4L,
  P1 =  2.642771505685952966904660652518429479531E3L,
  P2 = -3.423199068835684263987132888286791620673E2L,
  P3 =  1.800826371455042224581246202420972737840E1L,
  P4 = -5.238523121205561042771939008061958820811E-1L,

  Q0 = -9.516813471998079611319047060563358064497E4L,
  Q1 =  3.964866271411091674556850458227710004570E4L,
  Q2 = -7.207678383830091850230366618190187434796E3L,
  Q3 =  7.206038318724600171970199625081491823079E2L,
  Q4 = -4.002027679107076077238836622982900945173E1L,
  /* Q5 = 1.000000000000000000000000000000000000000E0 */

/* C1 + C2 = ln 2 */
C1 = 6.93145751953125E-1L,
C2 = 1.428606820309417232121458176568075500134E-6L,
/* ln 2^-65 */
minarg = -4.5054566736396445112120088E1L;
static const long double huge = 0x1p10000L;

long double
expm1l(long double x)
{
long double px, qx, xx;
int k;

/* Overflow.  */
if (x > MAXLOGL)
  return (huge*huge);	/* overflow */

if (x == 0.0)
  return x;

/* Minimum value.  */
if (x < minarg)
  return -1.0L;

xx = C1 + C2;

/* Express x = ln 2 (k + remainder), remainder not exceeding 1/2. */
px = floorl (0.5 + x / xx);
k = px;
/* remainder times ln 2 */
x -= px * C1;
x -= px * C2;

/* Approximate exp(remainder ln 2).  */
px = (((( P4 * x
	 + P3) * x
	+ P2) * x
       + P1) * x
      + P0) * x;

qx = (((( x
	 + Q4) * x
	+ Q3) * x
       + Q2) * x
      + Q1) * x
     + Q0;

xx = x * x;
qx = x + (0.5 * xx + xx * px / qx);

/* exp(x) = exp(k ln 2) exp(remainder ln 2) = 2^k exp(remainder ln 2).
   We have qx = exp(remainder ln 2) - 1, so
   exp(x) - 1  =  2^k (qx + 1) - 1  =  2^k qx + 2^k - 1.  */
px = ldexpl(1.0L, k);
x = px * qx + (px - 1.0);
return x;
}
DEF_STD(expm1l);
