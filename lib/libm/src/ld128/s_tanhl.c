/* @(#)s_tanh.c 5.1 93/09/24 */
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

/* tanhl(x)
 * Return the Hyperbolic Tangent of x
 *
 * Method :
 *                                      x    -x
 *                                     e  - e
 *      0. tanhl(x) is defined to be -----------
 *                                      x    -x
 *                                     e  + e
 *      1. reduce x to non-negative by tanhl(-x) = -tanhl(x).
 *      2.  0      <= x <= 2**-57 : tanhl(x) := x*(one+x)
 *                                               -t
 *          2**-57 <  x <=  1     : tanhl(x) := -----; t = expm1l(-2x)
 *                                              t + 2
 *                                                    2
 *          1      <= x <=  40.0  : tanhl(x) := 1-  ----- ; t=expm1l(2x)
 *                                                  t + 2
 *          40.0   <  x <= INF    : tanhl(x) := 1.
 *
 * Special cases:
 *      tanhl(NaN) is NaN;
 *      only tanhl(0)=0 is exact for finite argument.
 */

#include "math.h"
#include "math_private.h"

static const long double one = 1.0, two = 2.0, tiny = 1.0e-4900L;

long double
tanhl(long double x)
{
  long double t, z;
  u_int32_t jx, ix;
  ieee_quad_shape_type u;

  /* Words of |x|. */
  u.value = x;
  jx = u.parts32.mswhi;
  ix = jx & 0x7fffffff;
  /* x is INF or NaN */
  if (ix >= 0x7fff0000)
    {
      /* for NaN it's not important which branch: tanhl(NaN) = NaN */
      if (jx & 0x80000000)
	return one / x - one;	/* tanhl(-inf)= -1; */
      else
	return one / x + one;	/* tanhl(+inf)=+1 */
    }

  /* |x| < 40 */
  if (ix < 0x40044000)
    {
      if (u.value == 0)
	return x;		/* x == +- 0 */
      if (ix < 0x3fc60000)	/* |x| < 2^-57 */
	return x * (one + tiny); /* tanh(small) = small */
      u.parts32.mswhi = ix;	/* Absolute value of x.  */
      if (ix >= 0x3fff0000)
	{			/* |x| >= 1  */
	  t = expm1l (two * u.value);
	  z = one - two / (t + two);
	}
      else
	{
	  t = expm1l (-two * u.value);
	  z = -t / (t + two);
	}
      /* |x| > 40, return +-1 */
    }
  else
    {
      z = one - tiny;		/* raised inexact flag */
    }
  return (jx & 0x80000000) ? -z : z;
}
