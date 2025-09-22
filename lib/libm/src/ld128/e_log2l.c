/*	$OpenBSD: e_log2l.c,v 1.1 2011/07/06 00:02:42 martynas Exp $	*/

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

/*                                                      log2l.c
 *      Base 2 logarithm, 128-bit long double precision
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
 *     log(1+x) = x - 0.5 x^2 + x^3 P(x)/Q(x).
 *
 * Otherwise, setting  z = 2(x-1)/x+1),
 *
 *     log(x) = z + z^3 P(z)/Q(z).
 *
 *
 *
 * ACCURACY:
 *
 *                      Relative error:
 * arithmetic   domain     # trials      peak         rms
 *    IEEE      0.5, 2.0     100,000    2.6e-34     4.9e-35
 *    IEEE     exp(+-10000)  100,000    9.6e-35     4.0e-35
 *
 * In the tests over the interval exp(+-10000), the logarithms
 * of the random arguments were uniformly distributed over
 * [-10000, +10000].
 *
 */

#include <math.h>

#include "math_private.h"

/* Coefficients for ln(1+x) = x - x**2/2 + x**3 P(x)/Q(x)
 * 1/sqrt(2) <= x < sqrt(2)
 * Theoretical peak relative error = 5.3e-37,
 * relative peak error spread = 2.3e-14
 */
static const long double P[13] =
{
  1.313572404063446165910279910527789794488E4L,
  7.771154681358524243729929227226708890930E4L,
  2.014652742082537582487669938141683759923E5L,
  3.007007295140399532324943111654767187848E5L,
  2.854829159639697837788887080758954924001E5L,
  1.797628303815655343403735250238293741397E5L,
  7.594356839258970405033155585486712125861E4L,
  2.128857716871515081352991964243375186031E4L,
  3.824952356185897735160588078446136783779E3L,
  4.114517881637811823002128927449878962058E2L,
  2.321125933898420063925789532045674660756E1L,
  4.998469661968096229986658302195402690910E-1L,
  1.538612243596254322971797716843006400388E-6L
};
static const long double Q[12] =
{
  3.940717212190338497730839731583397586124E4L,
  2.626900195321832660448791748036714883242E5L,
  7.777690340007566932935753241556479363645E5L,
  1.347518538384329112529391120390701166528E6L,
  1.514882452993549494932585972882995548426E6L,
  1.158019977462989115839826904108208787040E6L,
  6.132189329546557743179177159925690841200E5L,
  2.248234257620569139969141618556349415120E5L,
  5.605842085972455027590989944010492125825E4L,
  9.147150349299596453976674231612674085381E3L,
  9.104928120962988414618126155557301584078E2L,
  4.839208193348159620282142911143429644326E1L
/* 1.000000000000000000000000000000000000000E0L, */
};

/* Coefficients for log(x) = z + z^3 P(z^2)/Q(z^2),
 * where z = 2(x-1)/(x+1)
 * 1/sqrt(2) <= x < sqrt(2)
 * Theoretical peak relative error = 1.1e-35,
 * relative peak error spread 1.1e-9
 */
static const long double R[6] =
{
  1.418134209872192732479751274970992665513E5L,
 -8.977257995689735303686582344659576526998E4L,
  2.048819892795278657810231591630928516206E4L,
 -2.024301798136027039250415126250455056397E3L,
  8.057002716646055371965756206836056074715E1L,
 -8.828896441624934385266096344596648080902E-1L
};
static const long double S[6] =
{
  1.701761051846631278975701529965589676574E6L,
 -1.332535117259762928288745111081235577029E6L,
  4.001557694070773974936904547424676279307E5L,
 -5.748542087379434595104154610899551484314E4L,
  3.998526750980007367835804959888064681098E3L,
 -1.186359407982897997337150403816839480438E2L
/* 1.000000000000000000000000000000000000000E0L, */
};

static const long double
/* log2(e) - 1 */
LOG2EA = 4.4269504088896340735992468100189213742664595E-1L,
/* sqrt(2)/2 */
SQRTH = 7.071067811865475244008443621048490392848359E-1L;


/* Evaluate P[n] x^n  +  P[n-1] x^(n-1)  +  ...  +  P[0] */

static long double
neval (long double x, const long double *p, int n)
{
  long double y;

  p += n;
  y = *p--;
  do
    {
      y = y * x + *p--;
    }
  while (--n > 0);
  return y;
}


/* Evaluate x^n+1  +  P[n] x^(n)  +  P[n-1] x^(n-1)  +  ...  +  P[0] */

static long double
deval (long double x, const long double *p, int n)
{
  long double y;

  p += n;
  y = x + *p--;
  do
    {
      y = y * x + *p--;
    }
  while (--n > 0);
  return y;
}



long double
log2l(long double x)
{
  long double z;
  long double y;
  int e;
  int64_t hx, lx;

/* Test for domain */
  GET_LDOUBLE_WORDS64 (hx, lx, x);
  if (((hx & 0x7fffffffffffffffLL) | lx) == 0)
    return (-1.0L / (x - x));
  if (hx < 0)
    return (x - x) / (x - x);
  if (hx >= 0x7fff000000000000LL)
    return (x + x);

/* separate mantissa from exponent */

/* Note, frexp is used so that denormal numbers
 * will be handled properly.
 */
  x = frexpl (x, &e);


/* logarithm using log(x) = z + z**3 P(z)/Q(z),
 * where z = 2(x-1)/x+1)
 */
  if ((e > 2) || (e < -2))
    {
      if (x < SQRTH)
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
      y = x * (z * neval (z, R, 5) / deval (z, S, 5));
      goto done;
    }


/* logarithm using log(1+x) = x - .5x**2 + x**3 P(x)/Q(x) */

  if (x < SQRTH)
    {
      e -= 1;
      x = 2.0 * x - 1.0L;	/*  2x - 1  */
    }
  else
    {
      x = x - 1.0L;
    }
  z = x * x;
  y = x * (z * neval (x, P, 12) / deval (x, Q, 11));
  y = y - 0.5 * z;

done:

/* Multiply log of fraction by log2(e)
 * and base 2 exponent by 1
 */
  z = y * LOG2EA;
  z += x * LOG2EA;
  z += y;
  z += x;
  z += e;
  return (z);
}
