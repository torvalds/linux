/* @(#)e_sinh.c 5.1 93/09/24 */
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

/* sinhl(x)
 * Method :
 * mathematically sinh(x) if defined to be (exp(x)-exp(-x))/2
 *      1. Replace x by |x| (sinhl(-x) = -sinhl(x)).
 *      2.
 *                                                   E + E/(E+1)
 *          0        <= x <= 25     :  sinhl(x) := --------------, E=expm1l(x)
 *                                                       2
 *
 *          25       <= x <= lnovft :  sinhl(x) := expl(x)/2
 *          lnovft   <= x <= ln2ovft:  sinhl(x) := expl(x/2)/2 * expl(x/2)
 *          ln2ovft  <  x           :  sinhl(x) := x*shuge (overflow)
 *
 * Special cases:
 *      sinhl(x) is |x| if x is +INF, -INF, or NaN.
 *      only sinhl(0)=0 is exact for finite x.
 */

#include <math.h>

#include "math_private.h"

static const long double one = 1.0, shuge = 1.0e4931L,
ovf_thresh = 1.1357216553474703894801348310092223067821E4L;

long double
sinhl(long double x)
{
  long double t, w, h;
  u_int32_t jx, ix;
  ieee_quad_shape_type u;

  /* Words of |x|. */
  u.value = x;
  jx = u.parts32.mswhi;
  ix = jx & 0x7fffffff;

  /* x is INF or NaN */
  if (ix >= 0x7fff0000)
    return x + x;

  h = 0.5;
  if (jx & 0x80000000)
    h = -h;

  /* Absolute value of x.  */
  u.parts32.mswhi = ix;

  /* |x| in [0,40], return sign(x)*0.5*(E+E/(E+1))) */
  if (ix <= 0x40044000)
    {
      if (ix < 0x3fc60000) /* |x| < 2^-57 */
	if (shuge + x > one)
	  return x;		/* sinh(tiny) = tiny with inexact */
      t = expm1l (u.value);
      if (ix < 0x3fff0000)
	return h * (2.0 * t - t * t / (t + one));
      return h * (t + t / (t + one));
    }

  /* |x| in [40, log(maxdouble)] return 0.5*exp(|x|) */
  if (ix <= 0x400c62e3) /* 11356.375 */
    return h * expl (u.value);

  /* |x| in [log(maxdouble), overflowthreshold]
     Overflow threshold is log(2 * maxdouble).  */
  if (u.value <= ovf_thresh)
    {
      w = expl (0.5 * u.value);
      t = h * w;
      return t * w;
    }

  /* |x| > overflowthreshold, sinhl(x) overflow */
  return x * shuge;
}
DEF_STD(sinhl);
