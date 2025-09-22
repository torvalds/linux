/*	$OpenBSD: testvectll.c,v 1.5 2022/04/23 16:04:05 mbuhl Exp $	*/

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

/* Test vectors for math functions.
   See C9X section F.9.

   On some systems it may be necessary to modify the default exception
   settings of the floating point arithmetic unit.  */

#include <float.h>

#if	LDBL_MANT_DIG == 113
#include <stdio.h>
#include <string.h>
int __isfinitel (long double);

/* Some compilers will not accept these expressions.  */

#define ZINF 1
#define ZMINF 2
#define ZNANL 3
#define ZPIL 4
#define ZPIO2L 4

/*
extern long double INFINITYL, NANL, NEGZEROL;
*/
extern long double INFINITYL;
long double NANL, NEGZEROL;
long double MINFL;
extern long double PIL, PIO2L, PIO4L, MACHEPL;
long double MPIL;
long double MPIO2L;
long double MPIO4L;
long double THPIO4L = 2.35619449019234492884698L;
long double MTHPIO4L = -2.35619449019234492884698L;
long double SQRT2L = 1.414213562373095048802E0L;
long double SQRTHL = 7.071067811865475244008E-1L;
long double ZEROL = 0.0L;
long double HALFL = 0.5L;
long double MHALFL = -0.5L;
long double ONEL = 1.0L;
long double MONEL = -1.0L;
long double TWOL = 2.0L;
long double MTWOL = -2.0L;
long double THREEL = 3.0L;
long double MTHREEL = -3.0L;

/* Functions of one variable.  */
long double logl (long double);
long double expl (long double);
long double atanl (long double);
long double sinl (long double);
long double cosl (long double);
long double tanl (long double);
long double acosl (long double);
long double asinl (long double);
long double acoshl (long double);
long double asinhl (long double);
long double atanhl (long double);
long double sinhl (long double);
long double coshl (long double);
long double tanhl (long double);
long double exp2l (long double);
long double expm1l (long double);
long double log10l (long double);
long double log1pl (long double);
long double log2l (long double);
long double fabsl (long double);
long double erfl (long double);
long double erfcl (long double);
long double tgammal (long double);
long double floorl (long double);
long double ceill (long double);
long double cbrtl (long double);
long double lgammal (long double);

struct oneargument
  {
    char *name;			/* Name of the function. */
    long double (*func) (long double);
    long double *arg1;
    long double *answer;
    int thresh;			/* Error report threshold. */
  };

#if 0
  {"sinl", sinl, 32767.L, 1.8750655394138942394239E-1L, 0},
  {"cosl", cosl, 32767.L, 9.8226335176928229845654E-1L, 0},
  {"tanl", tanl, 32767.L, 1.9089234430221485740826E-1L, 0},
  {"sinl", sinl, 8388607.L, 9.9234509376961249835628E-1L, 0},
  {"cosl", cosl, 8388607.L, -1.2349580912475928183718E-1L, 0},
  {"tanl", tanl, 8388607.L, -8.0354556223613614748329E0L, 0},
  {"sinl", sinl, 2147483647.L, -7.2491655514455639054829E-1L, 0},
  {"cosl", cosl, 2147483647.L, -6.8883669187794383467976E-1L, 0},
  {"tanl", tanl, 2147483647.L, 1.0523779637351339136698E0L, 0},
  {"sinl", sinl, PIO4L, 7.0710678118654752440084E-1L, 0},
  {"cosl", cosl, PIO2L, -2.50827880633416613471e-20L, 0},
#endif

static struct oneargument test1[] =
{
  {"atanl", atanl, &ONEL, &PIO4L, 0},
  {"sinl", sinl, &PIO2L, &ONEL, 0},
  {"cosl", cosl, &PIO4L, &SQRTHL, 0},
  {"acosl", acosl, &NANL, &NANL, 0},
  {"acosl", acosl, &ONEL, &ZEROL, 0},
  {"acosl", acosl, &TWOL, &NANL, 0},
  {"acosl", acosl, &MTWOL, &NANL, 0},
  {"asinl", asinl, &NANL, &NANL, 0},
  {"asinl", asinl, &ZEROL, &ZEROL, 0},
  {"asinl", asinl, &NEGZEROL, &NEGZEROL, 0},
  {"asinl", asinl, &TWOL, &NANL, 0},
  {"asinl", asinl, &MTWOL, &NANL, 0},
  {"atanl", atanl, &NANL, &NANL, 0},
  {"atanl", atanl, &ZEROL, &ZEROL, 0},
  {"atanl", atanl, &NEGZEROL, &NEGZEROL, 0},
  {"atanl", atanl, &INFINITYL, &PIO2L, 0},
  {"atanl", atanl, &MINFL, &MPIO2L, 0},
  {"cosl", cosl, &NANL, &NANL, 0},
  {"cosl", cosl, &ZEROL, &ONEL, 0},
  {"cosl", cosl, &NEGZEROL, &ONEL, 0},
  {"cosl", cosl, &INFINITYL, &NANL, 0},
  {"cosl", cosl, &MINFL, &NANL, 0},
  {"sinl", sinl, &NANL, &NANL, 0},
  {"sinl", sinl, &NEGZEROL, &NEGZEROL, 0},
  {"sinl", sinl, &ZEROL, &ZEROL, 0},
  {"sinl", sinl, &INFINITYL, &NANL, 0},
  {"sinl", sinl, &MINFL, &NANL, 0},
  {"tanl", tanl, &NANL, &NANL, 0},
  {"tanl", tanl, &ZEROL, &ZEROL, 0},
  {"tanl", tanl, &NEGZEROL, &NEGZEROL, 0},
  {"tanl", tanl, &INFINITYL, &NANL, 0},
  {"tanl", tanl, &MINFL, &NANL, 0},
  {"acoshl", acoshl, &NANL, &NANL, 0},
  {"acoshl", acoshl, &ONEL, &ZEROL, 0},
  {"acoshl", acoshl, &INFINITYL, &INFINITYL, 0},
  {"acoshl", acoshl, &HALFL, &NANL, 0},
  {"acoshl", acoshl, &MONEL, &NANL, 0},
  {"asinhl", asinhl, &NANL, &NANL, 0},
  {"asinhl", asinhl, &ZEROL, &ZEROL, 0},
  {"asinhl", asinhl, &NEGZEROL, &NEGZEROL, 0},
  {"asinhl", asinhl, &INFINITYL, &INFINITYL, 0},
  {"asinhl", asinhl, &MINFL, &MINFL, 0},
  {"atanhl", atanhl, &NANL, &NANL, 0},
  {"atanhl", atanhl, &ZEROL, &ZEROL, 0},
  {"atanhl", atanhl, &NEGZEROL, &NEGZEROL, 0},
  {"atanhl", atanhl, &ONEL, &INFINITYL, 0},
  {"atanhl", atanhl, &MONEL, &MINFL, 0},
  {"atanhl", atanhl, &TWOL, &NANL, 0},
  {"atanhl", atanhl, &MTWOL, &NANL, 0},
  {"coshl", coshl, &NANL, &NANL, 0},
  {"coshl", coshl, &ZEROL, &ONEL, 0},
  {"coshl", coshl, &NEGZEROL, &ONEL, 0},
  {"coshl", coshl, &INFINITYL, &INFINITYL, 0},
  {"coshl", coshl, &MINFL, &INFINITYL, 0},
  {"sinhl", sinhl, &NANL, &NANL, 0},
  {"sinhl", sinhl, &ZEROL, &ZEROL, 0},
  {"sinhl", sinhl, &NEGZEROL, &NEGZEROL, 0},
  {"sinhl", sinhl, &INFINITYL, &INFINITYL, 0},
  {"sinhl", sinhl, &MINFL, &MINFL, 0},
  {"tanhl", tanhl, &NANL, &NANL, 0},
  {"tanhl", tanhl, &ZEROL, &ZEROL, 0},
  {"tanhl", tanhl, &NEGZEROL, &NEGZEROL, 0},
  {"tanhl", tanhl, &INFINITYL, &ONEL, 0},
  {"tanhl", tanhl, &MINFL, &MONEL, 0},
  {"expl", expl, &NANL, &NANL, 0},
  {"expl", expl, &ZEROL, &ONEL, 0},
  {"expl", expl, &NEGZEROL, &ONEL, 0},
  {"expl", expl, &INFINITYL, &INFINITYL, 0},
  {"expl", expl, &MINFL, &ZEROL, 0},
  {"exp2l", exp2l, &NANL, &NANL, 0},
  {"exp2l", exp2l, &ZEROL, &ONEL, 0},
  {"exp2l", exp2l, &NEGZEROL, &ONEL, 0},
  {"exp2l", exp2l, &INFINITYL, &INFINITYL, 0},
  {"exp2l", exp2l, &MINFL, &ZEROL, 0},
  {"expm1l", expm1l, &NANL, &NANL, 0},
  {"expm1l", expm1l, &ZEROL, &ZEROL, 0},
  {"expm1l", expm1l, &NEGZEROL, &NEGZEROL, 0},
  {"expm1l", expm1l, &INFINITYL, &INFINITYL, 0},
  {"expm1l", expm1l, &MINFL, &MONEL, 0},
  {"logl", logl, &NANL, &NANL, 0},
  {"logl", logl, &ZEROL, &MINFL, 0},
  {"logl", logl, &NEGZEROL, &MINFL, 0},
  {"logl", logl, &ONEL, &ZEROL, 0},
  {"logl", logl, &MONEL, &NANL, 0},
  {"logl", logl, &INFINITYL, &INFINITYL, 0},
  {"log10l", log10l, &NANL, &NANL, 0},
  {"log10l", log10l, &ZEROL, &MINFL, 0},
  {"log10l", log10l, &NEGZEROL, &MINFL, 0},
  {"log10l", log10l, &ONEL, &ZEROL, 0},
  {"log10l", log10l, &MONEL, &NANL, 0},
  {"log10l", log10l, &INFINITYL, &INFINITYL, 0},
  {"log1pl", log1pl, &NANL, &NANL, 0},
  {"log1pl", log1pl, &ZEROL, &ZEROL, 0},
  {"log1pl", log1pl, &NEGZEROL, &NEGZEROL, 0},
  {"log1pl", log1pl, &MONEL, &MINFL, 0},
  {"log1pl", log1pl, &MTWOL, &NANL, 0},
  {"log1pl", log1pl, &INFINITYL, &INFINITYL, 0},
  {"log2l", log2l, &NANL, &NANL, 0},
  {"log2l", log2l, &ZEROL, &MINFL, 0},
  {"log2l", log2l, &NEGZEROL, &MINFL, 0},
  {"log2l", log2l, &MONEL, &NANL, 0},
  {"log2l", log2l, &INFINITYL, &INFINITYL, 0},
  /*  {"fabsl", fabsl, &NANL, &NANL, 0}, */
  {"fabsl", fabsl, &ONEL, &ONEL, 0},
  {"fabsl", fabsl, &MONEL, &ONEL, 0},
  {"fabsl", fabsl, &ZEROL, &ZEROL, 0},
  {"fabsl", fabsl, &NEGZEROL, &ZEROL, 0},
  {"fabsl", fabsl, &INFINITYL, &INFINITYL, 0},
  {"fabsl", fabsl, &MINFL, &INFINITYL, 0},
  {"cbrtl", cbrtl, &NANL, &NANL, 0},
  {"cbrtl", cbrtl, &ZEROL, &ZEROL, 0},
  {"cbrtl", cbrtl, &NEGZEROL, &NEGZEROL, 0},
  {"cbrtl", cbrtl, &INFINITYL, &INFINITYL, 0},
  {"cbrtl", cbrtl, &MINFL, &MINFL, 0},
  {"erfl", erfl, &NANL, &NANL, 0},
  {"erfl", erfl, &ZEROL, &ZEROL, 0},
  {"erfl", erfl, &NEGZEROL, &NEGZEROL, 0},
  {"erfl", erfl, &INFINITYL, &ONEL, 0},
  {"erfl", erfl, &MINFL, &MONEL, 0},
  {"erfcl", erfcl, &NANL, &NANL, 0},
  {"erfcl", erfcl, &INFINITYL, &ZEROL, 0},
  {"erfcl", erfcl, &MINFL, &TWOL, 0},
  {"tgammal", tgammal, &NANL, &NANL, 0},
  {"tgammal", tgammal, &INFINITYL, &INFINITYL, 0},
  {"tgammal", tgammal, &MONEL, &NANL, 0},
  {"tgammal", tgammal, &ZEROL, &INFINITYL, 0},
  {"tgammal", tgammal, &MINFL, &NANL, 0},
  {"lgammal", lgammal, &NANL, &NANL, 0},
  {"lgammal", lgammal, &INFINITYL, &INFINITYL, 0},
  {"lgammal", lgammal, &MONEL, &INFINITYL, 0},
  {"lgammal", lgammal, &ZEROL, &INFINITYL, 0},
  {"lgammal", lgammal, &MINFL, &INFINITYL, 0},
  {"ceill", ceill, &NANL, &NANL, 0},
  {"ceill", ceill, &ZEROL, &ZEROL, 0},
  {"ceill", ceill, &NEGZEROL, &NEGZEROL, 0},
  {"ceill", ceill, &INFINITYL, &INFINITYL, 0},
  {"ceill", ceill, &MINFL, &MINFL, 0},
  {"floorl", floorl, &NANL, &NANL, 0},
  {"floorl", floorl, &ZEROL, &ZEROL, 0},
  {"floorl", floorl, &NEGZEROL, &NEGZEROL, 0},
  {"floorl", floorl, &INFINITYL, &INFINITYL, 0},
  {"floorl", floorl, &MINFL, &MINFL, 0},
  {"null", NULL, &ZEROL, &ZEROL, 0},
};

/* Functions of two variables.  */
long double atan2l (long double, long double);
long double powl (long double, long double);

struct twoarguments
  {
    char *name;			/* Name of the function. */
    long double (*func) (long double, long double);
    long double *arg1;
    long double *arg2;
    long double *answer;
    int thresh;
  };

static struct twoarguments test2[] =
{
  {"atan2l", atan2l, &ZEROL, &ONEL, &ZEROL, 0},
  {"atan2l", atan2l, &NEGZEROL, &ONEL,&NEGZEROL, 0},
  {"atan2l", atan2l, &ZEROL, &ZEROL, &ZEROL, 0},
  {"atan2l", atan2l, &NEGZEROL, &ZEROL, &NEGZEROL, 0},
  {"atan2l", atan2l, &ZEROL, &MONEL, &PIL, 0},
  {"atan2l", atan2l, &NEGZEROL, &MONEL, &MPIL, 0},
  {"atan2l", atan2l, &ZEROL, &NEGZEROL, &PIL, 0},
  {"atan2l", atan2l, &NEGZEROL, &NEGZEROL, &MPIL, 0},
  {"atan2l", atan2l, &ONEL, &ZEROL, &PIO2L, 0},
  {"atan2l", atan2l, &ONEL, &NEGZEROL, &PIO2L, 0},
  {"atan2l", atan2l, &MONEL, &ZEROL, &MPIO2L, 0},
  {"atan2l", atan2l, &MONEL, &NEGZEROL, &MPIO2L, 0},
  {"atan2l", atan2l, &ONEL, &INFINITYL, &ZEROL, 0},
  {"atan2l", atan2l, &MONEL, &INFINITYL, &NEGZEROL, 0},
  {"atan2l", atan2l, &INFINITYL, &ONEL, &PIO2L, 0},
  {"atan2l", atan2l, &INFINITYL, &MONEL, &PIO2L, 0},
  {"atan2l", atan2l, &MINFL, &ONEL, &MPIO2L, 0},
  {"atan2l", atan2l, &MINFL, &MONEL, &MPIO2L, 0},
  {"atan2l", atan2l, &ONEL, &MINFL, &PIL, 0},
  {"atan2l", atan2l, &MONEL, &MINFL, &MPIL, 0},
  {"atan2l", atan2l, &INFINITYL, &INFINITYL, &PIO4L, 0},
  {"atan2l", atan2l, &MINFL, &INFINITYL, &MPIO4L, 0},
  {"atan2l", atan2l, &INFINITYL, &MINFL, &THPIO4L, 0},
  {"atan2l", atan2l, &MINFL, &MINFL, &MTHPIO4L, 0},
  {"atan2l", atan2l, &ONEL, &ONEL, &PIO4L, 0},
  {"atan2l", atan2l, &NANL, &ONEL, &NANL, 0},
  {"atan2l", atan2l, &ONEL, &NANL, &NANL, 0},
  {"atan2l", atan2l, &NANL, &NANL, &NANL, 0},
  {"powl", powl, &ONEL, &ZEROL, &ONEL, 0},
  {"powl", powl, &ONEL, &NEGZEROL, &ONEL, 0},
  {"powl", powl, &MONEL, &ZEROL, &ONEL, 0},
  {"powl", powl, &MONEL, &NEGZEROL, &ONEL, 0},
  {"powl", powl, &INFINITYL, &ZEROL, &ONEL, 0},
  {"powl", powl, &INFINITYL, &NEGZEROL, &ONEL, 0},
  {"powl", powl, &NANL, &ZEROL, &ONEL, 0},
  {"powl", powl, &NANL, &NEGZEROL, &ONEL, 0},
  {"powl", powl, &TWOL, &INFINITYL, &INFINITYL, 0},
  {"powl", powl, &MTWOL, &INFINITYL, &INFINITYL, 0},
  {"powl", powl, &HALFL, &INFINITYL, &ZEROL, 0},
  {"powl", powl, &MHALFL, &INFINITYL, &ZEROL, 0},
  {"powl", powl, &TWOL, &MINFL, &ZEROL, 0},
  {"powl", powl, &MTWOL, &MINFL, &ZEROL, 0},
  {"powl", powl, &HALFL, &MINFL, &INFINITYL, 0},
  {"powl", powl, &MHALFL, &MINFL, &INFINITYL, 0},
  {"powl", powl, &INFINITYL, &HALFL, &INFINITYL, 0},
  {"powl", powl, &INFINITYL, &TWOL, &INFINITYL, 0},
  {"powl", powl, &INFINITYL, &MHALFL, &ZEROL, 0},
  {"powl", powl, &INFINITYL, &MTWOL, &ZEROL, 0},
  {"powl", powl, &MINFL, &THREEL, &MINFL, 0},
  {"powl", powl, &MINFL, &TWOL, &INFINITYL, 0},
  {"powl", powl, &MINFL, &MTHREEL, &NEGZEROL, 0},
  {"powl", powl, &MINFL, &MTWOL, &ZEROL, 0},
  {"powl", powl, &NANL, &ONEL, &NANL, 0},
  {"powl", powl, &ONEL, &NANL, &ONEL, 0},
  {"powl", powl, &NANL, &NANL, &NANL, 0},
  {"powl", powl, &ONEL, &INFINITYL, &ONEL, 0},
  {"powl", powl, &MONEL, &INFINITYL, &ONEL, 0},
  {"powl", powl, &ONEL, &MINFL, &ONEL, 0},
  {"powl", powl, &MONEL, &MINFL, &ONEL, 0},
  {"powl", powl, &MTWOL, &HALFL, &NANL, 0},
  {"powl", powl, &ZEROL, &MTHREEL, &INFINITYL, 0},
  {"powl", powl, &NEGZEROL, &MTHREEL, &MINFL, 0},
  {"powl", powl, &ZEROL, &MHALFL, &INFINITYL, 0},
  {"powl", powl, &NEGZEROL, &MHALFL, &INFINITYL, 0},
  {"powl", powl, &ZEROL, &THREEL, &ZEROL, 0},
  {"powl", powl, &NEGZEROL, &THREEL, &NEGZEROL, 0},
  {"powl", powl, &ZEROL, &HALFL, &ZEROL, 0},
  {"powl", powl, &NEGZEROL, &HALFL, &ZEROL, 0},
  {"null", NULL, &ZEROL, &ZEROL, &ZEROL, 0},
};

/* Integer functions of one variable.  */

int __isnanl (long double);
int __signbitl (long double);

struct intans
  {
    char *name;			/* Name of the function. */
    int (*func) (long double);
    long double *arg1;
    int ianswer;
  };

static struct intans test3[] =
{
  {"isfinitel", __isfinitel, &ZEROL, 1},
  {"isfinitel", __isfinitel, &INFINITYL, 0},
  {"isfinitel", __isfinitel, &MINFL, 0},
  {"isnanl", __isnanl, &NANL, 1},
  {"isnanl", __isnanl, &INFINITYL, 0},
  {"isnanl", __isnanl, &ZEROL, 0},
  {"isnanl", __isnanl, &NEGZEROL, 0},
  {"signbitl", __signbitl, &NEGZEROL, 1},
  {"signbitl", __signbitl, &MONEL, 1},
  {"signbitl", __signbitl, &ZEROL, 0},
  {"signbitl", __signbitl, &ONEL, 0},
  {"signbitl", __signbitl, &MINFL, 1},
  {"signbitl", __signbitl, &INFINITYL, 0},
  {"null", NULL, &ZEROL, 0},
};

static volatile long double x1;
static volatile long double x2;
static volatile long double y;
static volatile long double answer;

/* Print bits of a long double.  */
static void pvec (char *str, long double x)
{
  union
  {
    long double d;
    unsigned int i[4];
  } u;
  int i;

  u.d = x;
  printf ("%s ", str);
  for (i = 0; i < 4; i++)
    printf ("%08x ", u.i[i]);
  printf ("\n");
}

int
testvectll ()
{
  int i, nerrors, k, ianswer, ntests;
  long double (*fun1) (long double);
  long double (*fun2) (long double, long double);
  int (*fun3) (long double);
  long double e;
  union
    {
      long double d;
      char c[16];
    } u, v;

  INFINITYL = ONEL/ZEROL;
  pvec ("Infinity = ", INFINITYL);
  NANL = INFINITYL - INFINITYL;
  pvec ("Nan = ", NANL);
  NEGZEROL = -ZEROL;
  pvec ("-0.0 = ", NEGZEROL);
    /* This masks off fpu exceptions on i386.  */
    /* setfpu(0x137f); */
  nerrors = 0;
  ntests = 0;
  MINFL = -INFINITYL;
  MPIL = -PIL;
  MPIO2L = -PIO2L;
  MPIO4L = -PIO4L;
  i = 0;
  for (;;)
    {
      fun1 = test1[i].func;
      if (fun1 == NULL)
	break;
      x1 = *(test1[i].arg1);
      y = (*(fun1)) (x1);
      answer = *(test1[i].answer);
      if (test1[i].thresh == 0)
	{
	  v.d = answer;
	  u.d = y;
	  if (memcmp(u.c, v.c, 16) != 0)
	    {
	      /* O.K. if both are NaNs of some sort.  */
	      if (__isnanl(v.d) && __isnanl(u.d))
		goto nxttest1;
	      goto wrongone;
	    }
	  else
	    goto nxttest1;
	}
      if (y != answer)
	{
	  e = y - answer;
	  if (answer != 0.0L)
	    e = e / answer;
	  if (e < 0)
	    e = -e;
	  if (e > test1[i].thresh * MACHEPL)
	    {
wrongone:
	      printf ("%s (%.20Le) = %.20Le\n    should be %.20Le\n",
		      test1[i].name, x1, y, answer);
	      nerrors += 1;
	    }
	}
nxttest1:
      ntests += 1;
      i += 1;
    }

  i = 0;
  for (;;)
    {
      fun2 = test2[i].func;
      if (fun2 == NULL)
	break;
      x1 = *(test2[i].arg1);
      x2 = *(test2[i].arg2);
      y = (*(fun2)) (x1, x2);
      answer = *(test2[i].answer);
      if (test2[i].thresh == 0)
	{
	  v.d = answer;
	  u.d = y;
	  if (memcmp(u.c, v.c, 16) != 0)
	    {
	      /* O.K. if both are NaNs of some sort.  */
	      if (__isnanl(v.d) && __isnanl(u.d))
		goto nxttest2;
	      goto wrongtwo;
	    }
	  else
	    goto nxttest2;
	}
      if (y != answer)
	{
	  e = y - answer;
	  if (answer != 0.0L)
	    e = e / answer;
	  if (e < 0)
	    e = -e;
	  if (e > test2[i].thresh * MACHEPL)
	    {
wrongtwo:
	      printf ("%s (%.20Le, %.20Le) = %.20Le\n    should be %.20Le\n",
		      test2[i].name, x1, x2, y, answer);
	      nerrors += 1;
	    }
	}
nxttest2:
      ntests += 1;
      i += 1;
    }


  i = 0;
  for (;;)
    {
      fun3 = test3[i].func;
      if (fun3 == NULL)
	break;
      x1 = *(test3[i].arg1);
      k = (*(fun3)) (x1);
      ianswer = test3[i].ianswer;
      if (k != ianswer)
	{
	  printf ("%s (%.20Le) = %d\n    should be. %d\n",
		  test3[i].name, x1, k, ianswer);
	  nerrors += 1;
	}
      ntests += 1;
      i += 1;
    }

  printf ("%d errors in %d tests\n", nerrors, ntests);
  return (nerrors);
}
#endif	/* LDBL_MANT_DIG == 113 */
