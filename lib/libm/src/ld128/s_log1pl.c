/*	$OpenBSD: s_log1pl.c,v 1.2 2016/09/12 19:47:02 guenther Exp $	*/

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

/*							log1pl.c
 *
 *      Relative error logarithm
 *	Natural logarithm of 1+x, 128-bit long double precision
 *
 *
 *
 * SYNOPSIS:
 *
 * long double x, y, log1pl();
 *
 * y = log1pl( x );
 *
 *
 *
 * DESCRIPTION:
 *
 * Returns the base e (2.718...) logarithm of 1+x.
 *
 * The argument 1+x is separated into its exponent and fractional
 * parts.  If the exponent is between -1 and +1, the logarithm
 * of the fraction is approximated by
 *
 *     log(1+x) = x - 0.5 x^2 + x^3 P(x)/Q(x).
 *
 * Otherwise, setting  z = 2(w-1)/(w+1),
 *
 *     log(w) = z + z^3 P(z)/Q(z).
 *
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      -1, 8       100000      1.9e-34     4.3e-35
 */

#include <math.h>

#include "math_private.h"

/* Coefficients for log(1+x) = x - x^2 / 2 + x^3 P(x)/Q(x)
 * 1/sqrt(2) <= 1+x < sqrt(2)
 * Theoretical peak relative error = 5.3e-37,
 * relative peak error spread = 2.3e-14
 */
static const long double
  P12 = 1.538612243596254322971797716843006400388E-6L,
  P11 = 4.998469661968096229986658302195402690910E-1L,
  P10 = 2.321125933898420063925789532045674660756E1L,
  P9 = 4.114517881637811823002128927449878962058E2L,
  P8 = 3.824952356185897735160588078446136783779E3L,
  P7 = 2.128857716871515081352991964243375186031E4L,
  P6 = 7.594356839258970405033155585486712125861E4L,
  P5 = 1.797628303815655343403735250238293741397E5L,
  P4 = 2.854829159639697837788887080758954924001E5L,
  P3 = 3.007007295140399532324943111654767187848E5L,
  P2 = 2.014652742082537582487669938141683759923E5L,
  P1 = 7.771154681358524243729929227226708890930E4L,
  P0 = 1.313572404063446165910279910527789794488E4L,
  /* Q12 = 1.000000000000000000000000000000000000000E0L, */
  Q11 = 4.839208193348159620282142911143429644326E1L,
  Q10 = 9.104928120962988414618126155557301584078E2L,
  Q9 = 9.147150349299596453976674231612674085381E3L,
  Q8 = 5.605842085972455027590989944010492125825E4L,
  Q7 = 2.248234257620569139969141618556349415120E5L,
  Q6 = 6.132189329546557743179177159925690841200E5L,
  Q5 = 1.158019977462989115839826904108208787040E6L,
  Q4 = 1.514882452993549494932585972882995548426E6L,
  Q3 = 1.347518538384329112529391120390701166528E6L,
  Q2 = 7.777690340007566932935753241556479363645E5L,
  Q1 = 2.626900195321832660448791748036714883242E5L,
  Q0 = 3.940717212190338497730839731583397586124E4L;

/* Coefficients for log(x) = z + z^3 P(z^2)/Q(z^2),
 * where z = 2(x-1)/(x+1)
 * 1/sqrt(2) <= x < sqrt(2)
 * Theoretical peak relative error = 1.1e-35,
 * relative peak error spread 1.1e-9
 */
static const long double
  R5 = -8.828896441624934385266096344596648080902E-1L,
  R4 = 8.057002716646055371965756206836056074715E1L,
  R3 = -2.024301798136027039250415126250455056397E3L,
  R2 = 2.048819892795278657810231591630928516206E4L,
  R1 = -8.977257995689735303686582344659576526998E4L,
  R0 = 1.418134209872192732479751274970992665513E5L,
  /* S6 = 1.000000000000000000000000000000000000000E0L, */
  S5 = -1.186359407982897997337150403816839480438E2L,
  S4 = 3.998526750980007367835804959888064681098E3L,
  S3 = -5.748542087379434595104154610899551484314E4L,
  S2 = 4.001557694070773974936904547424676279307E5L,
  S1 = -1.332535117259762928288745111081235577029E6L,
  S0 = 1.701761051846631278975701529965589676574E6L;

/* C1 + C2 = ln 2 */
static const long double C1 = 6.93145751953125E-1L;
static const long double C2 = 1.428606820309417232121458176568075500134E-6L;

static const long double sqrth = 0.7071067811865475244008443621048490392848L;
/* ln (2^16384 * (1 - 2^-113)) */
static const long double zero = 0.0L;

long double
log1pl(long double xm1)
{
  long double x, y, z, r, s;
  ieee_quad_shape_type u;
  int32_t hx;
  int e;

  /* Test for NaN or infinity input. */
  u.value = xm1;
  hx = u.parts32.mswhi;
  if (hx >= 0x7fff0000)
    return xm1;

  /* log1p(+- 0) = +- 0.  */
  if (((hx & 0x7fffffff) == 0)
      && (u.parts32.mswlo | u.parts32.lswhi | u.parts32.lswlo) == 0)
    return xm1;

  x = xm1 + 1.0L;

  /* log1p(-1) = -inf */
  if (x <= 0.0L)
    {
      if (x == 0.0L)
	return (-1.0L / (x - x));
      else
	return (zero / (x - x));
    }

  /* Separate mantissa from exponent.  */

  /* Use frexp used so that denormal numbers will be handled properly.  */
  x = frexpl (x, &e);

  /* Logarithm using log(x) = z + z^3 P(z^2)/Q(z^2),
     where z = 2(x-1)/x+1).  */
  if ((e > 2) || (e < -2))
    {
      if (x < sqrth)
	{			/* 2( 2x-1 )/( 2x+1 ) */
	  e -= 1;
	  z = x - 0.5L;
	  y = 0.5L * z + 0.5L;
	}
      else
	{			/*  2 (x-1)/(x+1)   */
	  z = x - 0.5L;
	  z -= 0.5L;
	  y = 0.5L * x + 0.5L;
	}
      x = z / y;
      z = x * x;
      r = ((((R5 * z
	      + R4) * z
	     + R3) * z
	    + R2) * z
	   + R1) * z
	+ R0;
      s = (((((z
	       + S5) * z
	      + S4) * z
	     + S3) * z
	    + S2) * z
	   + S1) * z
	+ S0;
      z = x * (z * r / s);
      z = z + e * C2;
      z = z + x;
      z = z + e * C1;
      return (z);
    }


  /* Logarithm using log(1+x) = x - .5x^2 + x^3 P(x)/Q(x). */

  if (x < sqrth)
    {
      e -= 1;
      if (e != 0)
	x = 2.0L * x - 1.0L;	/*  2x - 1  */
      else
	x = xm1;
    }
  else
    {
      if (e != 0)
	x = x - 1.0L;
      else
	x = xm1;
    }
  z = x * x;
  r = (((((((((((P12 * x
		 + P11) * x
		+ P10) * x
	       + P9) * x
	      + P8) * x
	     + P7) * x
	    + P6) * x
	   + P5) * x
	  + P4) * x
	 + P3) * x
	+ P2) * x
       + P1) * x
    + P0;
  s = (((((((((((x
		 + Q11) * x
		+ Q10) * x
	       + Q9) * x
	      + Q8) * x
	     + Q7) * x
	    + Q6) * x
	   + Q5) * x
	  + Q4) * x
	 + Q3) * x
	+ Q2) * x
       + Q1) * x
    + Q0;
  y = x * (z * r / s);
  y = y + e * C2;
  z = y - 0.5L * z;
  z = z + x;
  z = z + e * C1;
  return (z);
}
DEF_STD(log1pl);
