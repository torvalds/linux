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

/* double erf(double x)
 * double erfc(double x)
 *			     x
 *		      2      |\
 *     erf(x)  =  ---------  | exp(-t*t)dt
 *		   sqrt(pi) \|
 *			     0
 *
 *     erfc(x) =  1-erf(x)
 *  Note that
 *		erf(-x) = -erf(x)
 *		erfc(-x) = 2 - erfc(x)
 *
 * Method:
 *	1. For |x| in [0, 0.84375]
 *	    erf(x)  = x + x*R(x^2)
 *          erfc(x) = 1 - erf(x)           if x in [-.84375,0.25]
 *                  = 0.5 + ((0.5-x)-x*R)  if x in [0.25,0.84375]
 *	   Remark. The formula is derived by noting
 *          erf(x) = (2/sqrt(pi))*(x - x^3/3 + x^5/10 - x^7/42 + ....)
 *	   and that
 *          2/sqrt(pi) = 1.128379167095512573896158903121545171688
 *	   is close to one. The interval is chosen because the fix
 *	   point of erf(x) is near 0.6174 (i.e., erf(x)=x when x is
 *	   near 0.6174), and by some experiment, 0.84375 is chosen to
 *	   guarantee the error is less than one ulp for erf.
 *
 *      2. For |x| in [0.84375,1.25], let s = |x| - 1, and
 *         c = 0.84506291151 rounded to single (24 bits)
 *	erf(x)  = sign(x) * (c  + P1(s)/Q1(s))
 *	erfc(x) = (1-c)  - P1(s)/Q1(s) if x > 0
 *			  1+(c+P1(s)/Q1(s))    if x < 0
 *	   Remark: here we use the taylor series expansion at x=1.
 *		erf(1+s) = erf(1) + s*Poly(s)
 *			 = 0.845.. + P1(s)/Q1(s)
 *	   Note that |P1/Q1|< 0.078 for x in [0.84375,1.25]
 *
 *      3. For x in [1.25,1/0.35(~2.857143)],
 *	erfc(x) = (1/x)*exp(-x*x-0.5625+R1(z)/S1(z))
 *              z=1/x^2
 *	erf(x)  = 1 - erfc(x)
 *
 *      4. For x in [1/0.35,107]
 *	erfc(x) = (1/x)*exp(-x*x-0.5625+R2/S2) if x > 0
 *			= 2.0 - (1/x)*exp(-x*x-0.5625+R2(z)/S2(z))
 *                             if -6.666<x<0
 *			= 2.0 - tiny		(if x <= -6.666)
 *              z=1/x^2
 *	erf(x)  = sign(x)*(1.0 - erfc(x)) if x < 6.666, else
 *	erf(x)  = sign(x)*(1.0 - tiny)
 *      Note1:
 *	   To compute exp(-x*x-0.5625+R/S), let s be a single
 *	   precision number and s := x; then
 *		-x*x = -s*s + (s-x)*(s+x)
 *	        exp(-x*x-0.5626+R/S) =
 *			exp(-s*s-0.5625)*exp((s-x)*(s+x)+R/S);
 *      Note2:
 *	   Here 4 and 5 make use of the asymptotic series
 *			  exp(-x*x)
 *		erfc(x) ~ ---------- * ( 1 + Poly(1/x^2) )
 *			  x*sqrt(pi)
 *
 *      5. For inf > x >= 107
 *	erf(x)  = sign(x) *(1 - tiny)  (raise inexact)
 *	erfc(x) = tiny*tiny (raise underflow) if x > 0
 *			= 2 - tiny if x<0
 *
 *      7. Special case:
 *	erf(0)  = 0, erf(inf)  = 1, erf(-inf) = -1,
 *	erfc(0) = 1, erfc(inf) = 0, erfc(-inf) = 2,
 *		erfc/erf(NaN) is NaN
 */


#include <math.h>

#include "math_private.h"

static const long double
tiny = 1e-4931L,
  half = 0.5L,
  one = 1.0L,
  two = 2.0L,
	/* c = (float)0.84506291151 */
  erx = 0.845062911510467529296875L,
/*
 * Coefficients for approximation to  erf on [0,0.84375]
 */
  /* 2/sqrt(pi) - 1 */
  efx = 1.2837916709551257389615890312154517168810E-1L,
  /* 8 * (2/sqrt(pi) - 1) */
  efx8 = 1.0270333367641005911692712249723613735048E0L,

  pp[6] = {
    1.122751350964552113068262337278335028553E6L,
    -2.808533301997696164408397079650699163276E6L,
    -3.314325479115357458197119660818768924100E5L,
    -6.848684465326256109712135497895525446398E4L,
    -2.657817695110739185591505062971929859314E3L,
    -1.655310302737837556654146291646499062882E2L,
  },

  qq[6] = {
    8.745588372054466262548908189000448124232E6L,
    3.746038264792471129367533128637019611485E6L,
    7.066358783162407559861156173539693900031E5L,
    7.448928604824620999413120955705448117056E4L,
    4.511583986730994111992253980546131408924E3L,
    1.368902937933296323345610240009071254014E2L,
    /* 1.000000000000000000000000000000000000000E0 */
  },

/*
 * Coefficients for approximation to  erf  in [0.84375,1.25]
 */
/* erf(x+1) = 0.845062911510467529296875 + pa(x)/qa(x)
   -0.15625 <= x <= +.25
   Peak relative error 8.5e-22  */

  pa[8] = {
    -1.076952146179812072156734957705102256059E0L,
     1.884814957770385593365179835059971587220E2L,
    -5.339153975012804282890066622962070115606E1L,
     4.435910679869176625928504532109635632618E1L,
     1.683219516032328828278557309642929135179E1L,
    -2.360236618396952560064259585299045804293E0L,
     1.852230047861891953244413872297940938041E0L,
     9.394994446747752308256773044667843200719E-2L,
  },

  qa[7] =  {
    4.559263722294508998149925774781887811255E2L,
    3.289248982200800575749795055149780689738E2L,
    2.846070965875643009598627918383314457912E2L,
    1.398715859064535039433275722017479994465E2L,
    6.060190733759793706299079050985358190726E1L,
    2.078695677795422351040502569964299664233E1L,
    4.641271134150895940966798357442234498546E0L,
    /* 1.000000000000000000000000000000000000000E0 */
  },

/*
 * Coefficients for approximation to  erfc in [1.25,1/0.35]
 */
/* erfc(1/x) = x exp (-1/x^2 - 0.5625 + ra(x^2)/sa(x^2))
   1/2.85711669921875 < 1/x < 1/1.25
   Peak relative error 3.1e-21  */

    ra[] = {
      1.363566591833846324191000679620738857234E-1L,
      1.018203167219873573808450274314658434507E1L,
      1.862359362334248675526472871224778045594E2L,
      1.411622588180721285284945138667933330348E3L,
      5.088538459741511988784440103218342840478E3L,
      8.928251553922176506858267311750789273656E3L,
      7.264436000148052545243018622742770549982E3L,
      2.387492459664548651671894725748959751119E3L,
      2.220916652813908085449221282808458466556E2L,
    },

    sa[] = {
      -1.382234625202480685182526402169222331847E1L,
      -3.315638835627950255832519203687435946482E2L,
      -2.949124863912936259747237164260785326692E3L,
      -1.246622099070875940506391433635999693661E4L,
      -2.673079795851665428695842853070996219632E4L,
      -2.880269786660559337358397106518918220991E4L,
      -1.450600228493968044773354186390390823713E4L,
      -2.874539731125893533960680525192064277816E3L,
      -1.402241261419067750237395034116942296027E2L,
      /* 1.000000000000000000000000000000000000000E0 */
    },
/*
 * Coefficients for approximation to  erfc in [1/.35,107]
 */
/* erfc(1/x) = x exp (-1/x^2 - 0.5625 + rb(x^2)/sb(x^2))
   1/6.6666259765625 < 1/x < 1/2.85711669921875
   Peak relative error 4.2e-22  */
    rb[] = {
      -4.869587348270494309550558460786501252369E-5L,
      -4.030199390527997378549161722412466959403E-3L,
      -9.434425866377037610206443566288917589122E-2L,
      -9.319032754357658601200655161585539404155E-1L,
      -4.273788174307459947350256581445442062291E0L,
      -8.842289940696150508373541814064198259278E0L,
      -7.069215249419887403187988144752613025255E0L,
      -1.401228723639514787920274427443330704764E0L,
    },

    sb[] = {
      4.936254964107175160157544545879293019085E-3L,
      1.583457624037795744377163924895349412015E-1L,
      1.850647991850328356622940552450636420484E0L,
      9.927611557279019463768050710008450625415E0L,
      2.531667257649436709617165336779212114570E1L,
      2.869752886406743386458304052862814690045E1L,
      1.182059497870819562441683560749192539345E1L,
      /* 1.000000000000000000000000000000000000000E0 */
    },
/* erfc(1/x) = x exp (-1/x^2 - 0.5625 + rc(x^2)/sc(x^2))
   1/107 <= 1/x <= 1/6.6666259765625
   Peak relative error 1.1e-21  */
    rc[] = {
      -8.299617545269701963973537248996670806850E-5L,
      -6.243845685115818513578933902532056244108E-3L,
      -1.141667210620380223113693474478394397230E-1L,
      -7.521343797212024245375240432734425789409E-1L,
      -1.765321928311155824664963633786967602934E0L,
      -1.029403473103215800456761180695263439188E0L,
    },

    sc[] = {
      8.413244363014929493035952542677768808601E-3L,
      2.065114333816877479753334599639158060979E-1L,
      1.639064941530797583766364412782135680148E0L,
      4.936788463787115555582319302981666347450E0L,
      5.005177727208955487404729933261347679090E0L,
      /* 1.000000000000000000000000000000000000000E0 */
    };

long double
erfl(long double x)
{
  long double R, S, P, Q, s, y, z, r;
  int32_t ix, i;
  u_int32_t se, i0, i1;

  GET_LDOUBLE_WORDS (se, i0, i1, x);
  ix = se & 0x7fff;

  if (ix >= 0x7fff)
    {				/* erf(nan)=nan */
      i = ((se & 0xffff) >> 15) << 1;
      return (long double) (1 - i) + one / x;	/* erf(+-inf)=+-1 */
    }

  ix = (ix << 16) | (i0 >> 16);
  if (ix < 0x3ffed800) /* |x|<0.84375 */
    {
      if (ix < 0x3fde8000) /* |x|<2**-33 */
	{
	  if (ix < 0x00080000)
	    return 0.125 * (8.0 * x + efx8 * x);	/*avoid underflow */
	  return x + efx * x;
	}
      z = x * x;
      r = pp[0] + z * (pp[1]
	+ z * (pp[2] + z * (pp[3] + z * (pp[4] + z * pp[5]))));
      s = qq[0] + z * (qq[1]
	+ z * (qq[2] + z * (qq[3] + z * (qq[4] + z * (qq[5] + z)))));
      y = r / s;
      return x + x * y;
    }
  if (ix < 0x3fffa000) /* 1.25 */
    {				/* 0.84375 <= |x| < 1.25 */
      s = fabsl (x) - one;
      P = pa[0] + s * (pa[1] + s * (pa[2]
	+ s * (pa[3] + s * (pa[4] + s * (pa[5] + s * (pa[6] + s * pa[7]))))));
      Q = qa[0] + s * (qa[1] + s * (qa[2]
	+ s * (qa[3] + s * (qa[4] + s * (qa[5] + s * (qa[6] + s))))));
      if ((se & 0x8000) == 0)
	return erx + P / Q;
      else
	return -erx - P / Q;
    }
  if (ix >= 0x4001d555) /* 6.6666259765625 */
    {				/* inf>|x|>=6.666 */
      if ((se & 0x8000) == 0)
	return one - tiny;
      else
	return tiny - one;
    }
  x = fabsl (x);
  s = one / (x * x);
  if (ix < 0x4000b6db) /* 2.85711669921875 */
    {
      R = ra[0] + s * (ra[1] + s * (ra[2] + s * (ra[3] + s * (ra[4] +
	s * (ra[5] + s * (ra[6] + s * (ra[7] + s * ra[8])))))));
      S = sa[0] + s * (sa[1] + s * (sa[2] + s * (sa[3] + s * (sa[4] +
	s * (sa[5] + s * (sa[6] + s * (sa[7] + s * (sa[8] + s))))))));
    }
  else
    {				/* |x| >= 1/0.35 */
      R = rb[0] + s * (rb[1] + s * (rb[2] + s * (rb[3] + s * (rb[4] +
	s * (rb[5] + s * (rb[6] + s * rb[7]))))));
      S = sb[0] + s * (sb[1] + s * (sb[2] + s * (sb[3] + s * (sb[4] +
	s * (sb[5] + s * (sb[6] + s))))));
    }
  z = x;
  GET_LDOUBLE_WORDS (i, i0, i1, z);
  i1 = 0;
  SET_LDOUBLE_WORDS (z, i, i0, i1);
  r =
    expl (-z * z - 0.5625) * expl ((z - x) * (z + x) + R / S);
  if ((se & 0x8000) == 0)
    return one - r / x;
  else
    return r / x - one;
}
DEF_STD(erfl);

long double
erfcl(long double x)
{
  int32_t hx, ix;
  long double R, S, P, Q, s, y, z, r;
  u_int32_t se, i0, i1;

  GET_LDOUBLE_WORDS (se, i0, i1, x);
  ix = se & 0x7fff;
  if (ix >= 0x7fff)
    {				/* erfc(nan)=nan */
      /* erfc(+-inf)=0,2 */
      return (long double) (((se & 0xffff) >> 15) << 1) + one / x;
    }

  ix = (ix << 16) | (i0 >> 16);
  if (ix < 0x3ffed800) /* |x|<0.84375 */
    {
      if (ix < 0x3fbe0000) /* |x|<2**-65 */
	return one - x;
      z = x * x;
      r = pp[0] + z * (pp[1]
	+ z * (pp[2] + z * (pp[3] + z * (pp[4] + z * pp[5]))));
      s = qq[0] + z * (qq[1]
	+ z * (qq[2] + z * (qq[3] + z * (qq[4] + z * (qq[5] + z)))));
      y = r / s;
      if (ix < 0x3ffd8000) /* x<1/4 */
	{
	  return one - (x + x * y);
	}
      else
	{
	  r = x * y;
	  r += (x - half);
	  return half - r;
	}
    }
  if (ix < 0x3fffa000) /* 1.25 */
    {				/* 0.84375 <= |x| < 1.25 */
      s = fabsl (x) - one;
      P = pa[0] + s * (pa[1] + s * (pa[2]
	+ s * (pa[3] + s * (pa[4] + s * (pa[5] + s * (pa[6] + s * pa[7]))))));
      Q = qa[0] + s * (qa[1] + s * (qa[2]
	+ s * (qa[3] + s * (qa[4] + s * (qa[5] + s * (qa[6] + s))))));
      if ((se & 0x8000) == 0)
	{
	  z = one - erx;
	  return z - P / Q;
	}
      else
	{
	  z = erx + P / Q;
	  return one + z;
	}
    }
  if (ix < 0x4005d600) /* 107 */
    {				/* |x|<107 */
      x = fabsl (x);
      s = one / (x * x);
      if (ix < 0x4000b6db) /* 2.85711669921875 */
	{			/* |x| < 1/.35 ~ 2.857143 */
	  R = ra[0] + s * (ra[1] + s * (ra[2] + s * (ra[3] + s * (ra[4] +
	    s * (ra[5] + s * (ra[6] + s * (ra[7] + s * ra[8])))))));
	  S = sa[0] + s * (sa[1] + s * (sa[2] + s * (sa[3] + s * (sa[4] +
	    s * (sa[5] + s * (sa[6] + s * (sa[7] + s * (sa[8] + s))))))));
	}
      else if (ix < 0x4001d555) /* 6.6666259765625 */
	{			/* 6.666 > |x| >= 1/.35 ~ 2.857143 */
	  R = rb[0] + s * (rb[1] + s * (rb[2] + s * (rb[3] + s * (rb[4] +
	    s * (rb[5] + s * (rb[6] + s * rb[7]))))));
	  S = sb[0] + s * (sb[1] + s * (sb[2] + s * (sb[3] + s * (sb[4] +
	    s * (sb[5] + s * (sb[6] + s))))));
	}
      else
	{			/* |x| >= 6.666 */
	  if (se & 0x8000)
	    return two - tiny;	/* x < -6.666 */

	  R = rc[0] + s * (rc[1] + s * (rc[2] + s * (rc[3] +
						    s * (rc[4] + s * rc[5]))));
	  S = sc[0] + s * (sc[1] + s * (sc[2] + s * (sc[3] +
						    s * (sc[4] + s))));
	}
      z = x;
      GET_LDOUBLE_WORDS (hx, i0, i1, z);
      i1 = 0;
      i0 &= 0xffffff00;
      SET_LDOUBLE_WORDS (z, hx, i0, i1);
      r = expl (-z * z - 0.5625) *
	expl ((z - x) * (z + x) + R / S);
      if ((se & 0x8000) == 0)
	return r / x;
      else
	return two - r / x;
    }
  else
    {
      if ((se & 0x8000) == 0)
	return tiny * tiny;
      else
	return two - tiny;
    }
}
DEF_STD(erfcl);
