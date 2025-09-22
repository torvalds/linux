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

/* coshl(x)
 * Method :
 * mathematically coshl(x) if defined to be (exp(x)+exp(-x))/2
 *      1. Replace x by |x| (coshl(x) = coshl(-x)).
 *      2.
 *                                                      [ exp(x) - 1 ]^2
 *          0        <= x <= ln2/2  :  coshl(x) := 1 + -------------------
 *                                                         2*exp(x)
 *
 *                                                 exp(x) +  1/exp(x)
 *          ln2/2    <= x <= 22     :  coshl(x) := -------------------
 *                                                         2
 *          22       <= x <= lnovft :  coshl(x) := expl(x)/2
 *          lnovft   <= x <= ln2ovft:  coshl(x) := expl(x/2)/2 * expl(x/2)
 *          ln2ovft  <  x           :  coshl(x) := huge*huge (overflow)
 *
 * Special cases:
 *      coshl(x) is |x| if x is +INF, -INF, or NaN.
 *      only coshl(0)=1 is exact for finite x.
 */

#include <math.h>

#include "math_private.h"

static const long double one = 1.0, half = 0.5, huge = 1.0e4900L,
ovf_thresh = 1.1357216553474703894801348310092223067821E4L;

long double
coshl(long double x)
{
  long double t, w;
  int32_t ex;
  ieee_quad_shape_type u;

  u.value = x;
  ex = u.parts32.mswhi & 0x7fffffff;

  /* Absolute value of x.  */
  u.parts32.mswhi = ex;

  /* x is INF or NaN */
  if (ex >= 0x7fff0000)
    return x * x;

  /* |x| in [0,0.5*ln2], return 1+expm1l(|x|)^2/(2*expl(|x|)) */
  if (ex < 0x3ffd62e4) /* 0.3465728759765625 */
    {
      t = expm1l (u.value);
      w = one + t;
      if (ex < 0x3fb80000) /* |x| < 2^-116 */
	return w;		/* cosh(tiny) = 1 */

      return one + (t * t) / (w + w);
    }

  /* |x| in [0.5*ln2,40], return (exp(|x|)+1/exp(|x|)/2; */
  if (ex < 0x40044000)
    {
      t = expl (u.value);
      return half * t + half / t;
    }

  /* |x| in [22, ln(maxdouble)] return half*exp(|x|) */
  if (ex <= 0x400c62e3) /* 11356.375 */
    return half * expl (u.value);

  /* |x| in [log(maxdouble), overflowthresold] */
  if (u.value <= ovf_thresh)
    {
      w = expl (half * u.value);
      t = half * w;
      return t * w;
    }

  /* |x| > overflowthresold, cosh(x) overflow */
  return huge * huge;
}
DEF_STD(coshl);
