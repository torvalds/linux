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

/* powl(x,y) return x**y
 *
 *		      n
 * Method:  Let x =  2   * (1+f)
 *	1. Compute and return log2(x) in two pieces:
 *		log2(x) = w1 + w2,
 *	   where w1 has 113-53 = 60 bit trailing zeros.
 *	2. Perform y*log2(x) = n+y' by simulating muti-precision
 *	   arithmetic, where |y'|<=0.5.
 *	3. Return x**y = 2**n*exp(y'*log2)
 *
 * Special cases:
 *	1.  (anything) ** 0  is 1
 *	2.  (anything) ** 1  is itself
 *	3.  (anything) ** NAN is NAN
 *	4.  NAN ** (anything except 0) is NAN
 *	5.  +-(|x| > 1) **  +INF is +INF
 *	6.  +-(|x| > 1) **  -INF is +0
 *	7.  +-(|x| < 1) **  +INF is +0
 *	8.  +-(|x| < 1) **  -INF is +INF
 *	9.  +-1         ** +-INF is NAN
 *	10. +0 ** (+anything except 0, NAN)               is +0
 *	11. -0 ** (+anything except 0, NAN, odd integer)  is +0
 *	12. +0 ** (-anything except 0, NAN)               is +INF
 *	13. -0 ** (-anything except 0, NAN, odd integer)  is +INF
 *	14. -0 ** (odd integer) = -( +0 ** (odd integer) )
 *	15. +INF ** (+anything except 0,NAN) is +INF
 *	16. +INF ** (-anything except 0,NAN) is +0
 *	17. -INF ** (anything)  = -0 ** (-anything)
 *	18. (-anything) ** (integer) is (-1)**(integer)*(+anything**integer)
 *	19. (-anything except 0 and inf) ** (non-integer) is NAN
 *
 */

#include <math.h>

#include "math_private.h"

static const long double bp[] = {
  1.0L,
  1.5L,
};

/* log_2(1.5) */
static const long double dp_h[] = {
  0.0,
  5.8496250072115607565592654282227158546448E-1L
};

/* Low part of log_2(1.5) */
static const long double dp_l[] = {
  0.0,
  1.0579781240112554492329533686862998106046E-16L
};

static const long double zero = 0.0L,
  one = 1.0L,
  two = 2.0L,
  two113 = 1.0384593717069655257060992658440192E34L,
  huge = 1.0e3000L,
  tiny = 1.0e-3000L;

/* 3/2 log x = 3 z + z^3 + z^3 (z^2 R(z^2))
   z = (x-1)/(x+1)
   1 <= x <= 1.25
   Peak relative error 2.3e-37 */
static const long double LN[] =
{
 -3.0779177200290054398792536829702930623200E1L,
  6.5135778082209159921251824580292116201640E1L,
 -4.6312921812152436921591152809994014413540E1L,
  1.2510208195629420304615674658258363295208E1L,
 -9.9266909031921425609179910128531667336670E-1L
};
static const long double LD[] =
{
 -5.129862866715009066465422805058933131960E1L,
  1.452015077564081884387441590064272782044E2L,
 -1.524043275549860505277434040464085593165E2L,
  7.236063513651544224319663428634139768808E1L,
 -1.494198912340228235853027849917095580053E1L
  /* 1.0E0 */
};

/* exp(x) = 1 + x - x / (1 - 2 / (x - x^2 R(x^2)))
   0 <= x <= 0.5
   Peak relative error 5.7e-38  */
static const long double PN[] =
{
  5.081801691915377692446852383385968225675E8L,
  9.360895299872484512023336636427675327355E6L,
  4.213701282274196030811629773097579432957E4L,
  5.201006511142748908655720086041570288182E1L,
  9.088368420359444263703202925095675982530E-3L,
};
static const long double PD[] =
{
  3.049081015149226615468111430031590411682E9L,
  1.069833887183886839966085436512368982758E8L,
  8.259257717868875207333991924545445705394E5L,
  1.872583833284143212651746812884298360922E3L,
  /* 1.0E0 */
};

static const long double
  /* ln 2 */
  lg2 = 6.9314718055994530941723212145817656807550E-1L,
  lg2_h = 6.9314718055994528622676398299518041312695E-1L,
  lg2_l = 2.3190468138462996154948554638754786504121E-17L,
  ovt = 8.0085662595372944372e-0017L,
  /* 2/(3*log(2)) */
  cp = 9.6179669392597560490661645400126142495110E-1L,
  cp_h = 9.6179669392597555432899980587535537779331E-1L,
  cp_l = 5.0577616648125906047157785230014751039424E-17L;

long double
powl(long double x, long double y)
{
  long double z, ax, z_h, z_l, p_h, p_l;
  long double yy1, t1, t2, r, s, t, u, v, w;
  long double s2, s_h, s_l, t_h, t_l;
  int32_t i, j, k, yisint, n;
  u_int32_t ix, iy;
  int32_t hx, hy;
  ieee_quad_shape_type o, p, q;

  p.value = x;
  hx = p.parts32.mswhi;
  ix = hx & 0x7fffffff;

  q.value = y;
  hy = q.parts32.mswhi;
  iy = hy & 0x7fffffff;


  /* y==zero: x**0 = 1 */
  if ((iy | q.parts32.mswlo | q.parts32.lswhi | q.parts32.lswlo) == 0)
    return one;

  /* 1.0**y = 1; -1.0**+-Inf = 1 */
  if (x == one)
    return one;
  if (x == -1.0L && iy == 0x7fff0000
      && (q.parts32.mswlo | q.parts32.lswhi | q.parts32.lswlo) == 0)
    return one;

  /* +-NaN return x+y */
  if ((ix > 0x7fff0000)
      || ((ix == 0x7fff0000)
	  && ((p.parts32.mswlo | p.parts32.lswhi | p.parts32.lswlo) != 0))
      || (iy > 0x7fff0000)
      || ((iy == 0x7fff0000)
	  && ((q.parts32.mswlo | q.parts32.lswhi | q.parts32.lswlo) != 0)))
    return x + y;

  /* determine if y is an odd int when x < 0
   * yisint = 0       ... y is not an integer
   * yisint = 1       ... y is an odd int
   * yisint = 2       ... y is an even int
   */
  yisint = 0;
  if (hx < 0)
    {
      if (iy >= 0x40700000)	/* 2^113 */
	yisint = 2;		/* even integer y */
      else if (iy >= 0x3fff0000)	/* 1.0 */
	{
	  if (floorl (y) == y)
	    {
	      z = 0.5 * y;
	      if (floorl (z) == z)
		yisint = 2;
	      else
		yisint = 1;
	    }
	}
    }

  /* special value of y */
  if ((q.parts32.mswlo | q.parts32.lswhi | q.parts32.lswlo) == 0)
    {
      if (iy == 0x7fff0000)	/* y is +-inf */
	{
	  if (((ix - 0x3fff0000) | p.parts32.mswlo | p.parts32.lswhi |
	    p.parts32.lswlo) == 0)
	    return y - y;	/* +-1**inf is NaN */
	  else if (ix >= 0x3fff0000)	/* (|x|>1)**+-inf = inf,0 */
	    return (hy >= 0) ? y : zero;
	  else			/* (|x|<1)**-,+inf = inf,0 */
	    return (hy < 0) ? -y : zero;
	}
      if (iy == 0x3fff0000)
	{			/* y is  +-1 */
	  if (hy < 0)
	    return one / x;
	  else
	    return x;
	}
      if (hy == 0x40000000)
	return x * x;		/* y is  2 */
      if (hy == 0x3ffe0000)
	{			/* y is  0.5 */
	  if (hx >= 0)		/* x >= +0 */
	    return sqrtl (x);
	}
    }

  ax = fabsl (x);
  /* special value of x */
  if ((p.parts32.mswlo | p.parts32.lswhi | p.parts32.lswlo) == 0)
    {
      if (ix == 0x7fff0000 || ix == 0 || ix == 0x3fff0000)
	{
	  z = ax;		/*x is +-0,+-inf,+-1 */
	  if (hy < 0)
	    z = one / z;	/* z = (1/|x|) */
	  if (hx < 0)
	    {
	      if (((ix - 0x3fff0000) | yisint) == 0)
		{
		  z = (z - z) / (z - z);	/* (-1)**non-int is NaN */
		}
	      else if (yisint == 1)
		z = -z;		/* (x<0)**odd = -(|x|**odd) */
	    }
	  return z;
	}
    }

  /* (x<0)**(non-int) is NaN */
  if (((((u_int32_t) hx >> 31) - 1) | yisint) == 0)
    return (x - x) / (x - x);

  /* |y| is huge.
     2^-16495 = 1/2 of smallest representable value.
     If (1 - 1/131072)^y underflows, y > 1.4986e9 */
  if (iy > 0x401d654b)
    {
      /* if (1 - 2^-113)^y underflows, y > 1.1873e38 */
      if (iy > 0x407d654b)
	{
	  if (ix <= 0x3ffeffff)
	    return (hy < 0) ? huge * huge : tiny * tiny;
	  if (ix >= 0x3fff0000)
	    return (hy > 0) ? huge * huge : tiny * tiny;
	}
      /* over/underflow if x is not close to one */
      if (ix < 0x3ffeffff)
	return (hy < 0) ? huge * huge : tiny * tiny;
      if (ix > 0x3fff0000)
	return (hy > 0) ? huge * huge : tiny * tiny;
    }

  n = 0;
  /* take care subnormal number */
  if (ix < 0x00010000)
    {
      ax *= two113;
      n -= 113;
      o.value = ax;
      ix = o.parts32.mswhi;
    }
  n += ((ix) >> 16) - 0x3fff;
  j = ix & 0x0000ffff;
  /* determine interval */
  ix = j | 0x3fff0000;		/* normalize ix */
  if (j <= 0x3988)
    k = 0;			/* |x|<sqrt(3/2) */
  else if (j < 0xbb67)
    k = 1;			/* |x|<sqrt(3)   */
  else
    {
      k = 0;
      n += 1;
      ix -= 0x00010000;
    }

  o.value = ax;
  o.parts32.mswhi = ix;
  ax = o.value;

  /* compute s = s_h+s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5) */
  u = ax - bp[k];		/* bp[0]=1.0, bp[1]=1.5 */
  v = one / (ax + bp[k]);
  s = u * v;
  s_h = s;

  o.value = s_h;
  o.parts32.lswlo = 0;
  o.parts32.lswhi &= 0xf8000000;
  s_h = o.value;
  /* t_h=ax+bp[k] High */
  t_h = ax + bp[k];
  o.value = t_h;
  o.parts32.lswlo = 0;
  o.parts32.lswhi &= 0xf8000000;
  t_h = o.value;
  t_l = ax - (t_h - bp[k]);
  s_l = v * ((u - s_h * t_h) - s_h * t_l);
  /* compute log(ax) */
  s2 = s * s;
  u = LN[0] + s2 * (LN[1] + s2 * (LN[2] + s2 * (LN[3] + s2 * LN[4])));
  v = LD[0] + s2 * (LD[1] + s2 * (LD[2] + s2 * (LD[3] + s2 * (LD[4] + s2))));
  r = s2 * s2 * u / v;
  r += s_l * (s_h + s);
  s2 = s_h * s_h;
  t_h = 3.0 + s2 + r;
  o.value = t_h;
  o.parts32.lswlo = 0;
  o.parts32.lswhi &= 0xf8000000;
  t_h = o.value;
  t_l = r - ((t_h - 3.0) - s2);
  /* u+v = s*(1+...) */
  u = s_h * t_h;
  v = s_l * t_h + t_l * s;
  /* 2/(3log2)*(s+...) */
  p_h = u + v;
  o.value = p_h;
  o.parts32.lswlo = 0;
  o.parts32.lswhi &= 0xf8000000;
  p_h = o.value;
  p_l = v - (p_h - u);
  z_h = cp_h * p_h;		/* cp_h+cp_l = 2/(3*log2) */
  z_l = cp_l * p_h + p_l * cp + dp_l[k];
  /* log2(ax) = (s+..)*2/(3*log2) = n + dp_h + z_h + z_l */
  t = (long double) n;
  t1 = (((z_h + z_l) + dp_h[k]) + t);
  o.value = t1;
  o.parts32.lswlo = 0;
  o.parts32.lswhi &= 0xf8000000;
  t1 = o.value;
  t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);

  /* s (sign of result -ve**odd) = -1 else = 1 */
  s = one;
  if (((((u_int32_t) hx >> 31) - 1) | (yisint - 1)) == 0)
    s = -one;			/* (-ve)**(odd int) */

  /* split up y into yy1+y2 and compute (yy1+y2)*(t1+t2) */
  yy1 = y;
  o.value = yy1;
  o.parts32.lswlo = 0;
  o.parts32.lswhi &= 0xf8000000;
  yy1 = o.value;
  p_l = (y - yy1) * t1 + y * t2;
  p_h = yy1 * t1;
  z = p_l + p_h;
  o.value = z;
  j = o.parts32.mswhi;
  if (j >= 0x400d0000) /* z >= 16384 */
    {
      /* if z > 16384 */
      if (((j - 0x400d0000) | o.parts32.mswlo | o.parts32.lswhi |
	o.parts32.lswlo) != 0)
	return s * huge * huge;	/* overflow */
      else
	{
	  if (p_l + ovt > z - p_h)
	    return s * huge * huge;	/* overflow */
	}
    }
  else if ((j & 0x7fffffff) >= 0x400d01b9)	/* z <= -16495 */
    {
      /* z < -16495 */
      if (((j - 0xc00d01bc) | o.parts32.mswlo | o.parts32.lswhi |
	o.parts32.lswlo)
	  != 0)
	return s * tiny * tiny;	/* underflow */
      else
	{
	  if (p_l <= z - p_h)
	    return s * tiny * tiny;	/* underflow */
	}
    }
  /* compute 2**(p_h+p_l) */
  i = j & 0x7fffffff;
  k = (i >> 16) - 0x3fff;
  n = 0;
  if (i > 0x3ffe0000)
    {				/* if |z| > 0.5, set n = [z+0.5] */
      n = floorl (z + 0.5L);
      t = n;
      p_h -= t;
    }
  t = p_l + p_h;
  o.value = t;
  o.parts32.lswlo = 0;
  o.parts32.lswhi &= 0xf8000000;
  t = o.value;
  u = t * lg2_h;
  v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
  z = u + v;
  w = v - (z - u);
  /*  exp(z) */
  t = z * z;
  u = PN[0] + t * (PN[1] + t * (PN[2] + t * (PN[3] + t * PN[4])));
  v = PD[0] + t * (PD[1] + t * (PD[2] + t * (PD[3] + t)));
  t1 = z - t * u / v;
  r = (z * t1) / (t1 - two) - (w + z * w);
  z = one - (r - z);
  o.value = z;
  j = o.parts32.mswhi;
  j += (n << 16);
  if ((j >> 16) <= 0)
    z = scalbnl (z, n);	/* subnormal output */
  else
    {
      o.parts32.mswhi = j;
      z = o.value;
    }
  return s * z;
}
DEF_STD(powl);
