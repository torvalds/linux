/*	$OpenBSD: testvect.c,v 1.2 2013/08/02 20:23:28 kettenis Exp $	*/

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
   See C9X section F.9.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int __isfinite (double);

/* C9X spells lgam lgamma.  */
#define GLIBC2 1

extern double PI;
static double MPI, PIO2, MPIO2, PIO4, MPIO4, THPIO4, MTHPIO4;

#if 0
#define PI 3.141592653589793238463E0
#define PIO2 1.570796326794896619231E0
#define PIO4 7.853981633974483096157E-1
#define THPIO4 2.35619449019234492884698
#define SQRT2 1.414213562373095048802E0
#define SQRTH 7.071067811865475244008E-1
#define INF (1.0/0.0)
#define MINF (-1.0/0.0)
#endif

extern double MACHEP, SQRTH, SQRT2;
extern double NAN, INFINITY, NEGZERO;
static double INF, MINF;
static double ZERO, MZERO, HALF, MHALF, ONE, MONE, TWO, MTWO, THREE, MTHREE;
/* #define NAN (1.0/0.0 - 1.0/0.0) */

/* Functions of one variable.  */
double log (double);
double exp ( double);
double atan (double);
double sin (double);
double cos (double);
double tan (double);
double acos (double);
double asin (double);
double acosh (double);
double asinh (double);
double atanh (double);
double sinh (double);
double cosh (double);
double tanh (double);
double exp2 (double);
double expm1 (double);
double log10 (double);
double log1p (double);
double log2 (double);
double fabs (double);
double erf (double);
double erfc (double);
double tgamma (double);
double floor (double);
double ceil (double);
double cbrt (double);
#if GLIBC2
double lgamma (double);
#else
double lgam (double);
#endif

struct oneargument
  {
    char *name;			/* Name of the function. */
    double (*func) (double);
    double *arg1;
    double *answer;
    int thresh;			/* Error report threshold. */
  };

static struct oneargument test1[] =
{
  {"atan", atan, &ONE, &PIO4, 0},
  {"sin", sin, &PIO2, &ONE, 0},
#if 0
  {"cos", cos, &PIO4, &SQRTH, 0},
  {"sin", sin, 32767., 1.8750655394138942394239E-1, 0},
  {"cos", cos, 32767., 9.8226335176928229845654E-1, 0},
  {"tan", tan, 32767., 1.9089234430221485740826E-1, 0},
  {"sin", sin, 8388607., 9.9234509376961249835628E-1, 0},
  {"cos", cos, 8388607., -1.2349580912475928183718E-1, 0},
  {"tan", tan, 8388607., -8.0354556223613614748329E0, 0},
  /*
  {"sin", sin, 2147483647., -7.2491655514455639054829E-1, 0},
  {"cos", cos, 2147483647., -6.8883669187794383467976E-1, 0},
  {"tan", tan, 2147483647., 1.0523779637351339136698E0, 0},
  */
  {"cos", cos, &PIO2, 6.1232339957367574e-17, 1},
  {"sin", sin, &PIO4, &SQRTH, 1},
#endif
  {"acos", acos, &NAN, &NAN, 0},
  {"acos", acos, &ONE, &ZERO, 0},
  {"acos", acos, &TWO, &NAN, 0},
  {"acos", acos, &MTWO, &NAN, 0},
  {"asin", asin, &NAN, &NAN, 0},
  {"asin", asin, &ZERO, &ZERO, 0},
  {"asin", asin, &MZERO, &MZERO, 0},
  {"asin", asin, &TWO, &NAN, 0},
  {"asin", asin, &MTWO, &NAN, 0},
  {"atan", atan, &NAN, &NAN, 0},
  {"atan", atan, &ZERO, &ZERO, 0},
  {"atan", atan, &MZERO, &MZERO, 0},
  {"atan", atan, &INF, &PIO2, 0},
  {"atan", atan, &MINF, &MPIO2, 0},
  {"cos", cos, &NAN, &NAN, 0},
  {"cos", cos, &ZERO, &ONE, 0},
  {"cos", cos, &MZERO, &ONE, 0},
  {"cos", cos, &INF, &NAN, 0},
  {"cos", cos, &MINF, &NAN, 0},
  {"sin", sin, &NAN, &NAN, 0},
  {"sin", sin, &MZERO, &MZERO, 0},
  {"sin", sin, &ZERO, &ZERO, 0},
  {"sin", sin, &INF, &NAN, 0},
  {"sin", sin, &MINF, &NAN, 0},
  {"tan", tan, &NAN, &NAN, 0},
  {"tan", tan, &ZERO, &ZERO, 0},
  {"tan", tan, &MZERO, &MZERO, 0},
  {"tan", tan, &INF, &NAN, 0},
  {"tan", tan, &MINF, &NAN, 0},
  {"acosh", acosh, &NAN, &NAN, 0},
  {"acosh", acosh, &ONE, &ZERO, 0},
  {"acosh", acosh, &INF, &INF, 0},
  {"acosh", acosh, &HALF, &NAN, 0},
  {"acosh", acosh, &MONE, &NAN, 0},
  {"asinh", asinh, &NAN, &NAN, 0},
  {"asinh", asinh, &ZERO, &ZERO, 0},
  {"asinh", asinh, &MZERO, &MZERO, 0},
  {"asinh", asinh, &INF, &INF, 0},
  {"asinh", asinh, &MINF, &MINF, 0},
  {"atanh", atanh, &NAN, &NAN, 0},
  {"atanh", atanh, &ZERO, &ZERO, 0},
  {"atanh", atanh, &MZERO, &MZERO, 0},
  {"atanh", atanh, &ONE, &INF, 0},
  {"atanh", atanh, &MONE, &MINF, 0},
  {"atanh", atanh, &TWO, &NAN, 0},
  {"atanh", atanh, &MTWO, &NAN, 0},
  {"cosh", cosh, &NAN, &NAN, 0},
  {"cosh", cosh, &ZERO, &ONE, 0},
  {"cosh", cosh, &MZERO, &ONE, 0},
  {"cosh", cosh, &INF, &INF, 0},
  {"cosh", cosh, &MINF, &INF, 0},
  {"sinh", sinh, &NAN, &NAN, 0},
  {"sinh", sinh, &ZERO, &ZERO, 0},
  {"sinh", sinh, &MZERO, &MZERO, 0},
  {"sinh", sinh, &INF, &INF, 0},
  {"sinh", sinh, &MINF, &MINF, 0},
  {"tanh", tanh, &NAN, &NAN, 0},
  {"tanh", tanh, &ZERO, &ZERO, 0},
  {"tanh", tanh, &MZERO, &MZERO, 0},
  {"tanh", tanh, &INF, &ONE, 0},
  {"tanh", tanh, &MINF, &MONE, 0},
  {"exp", exp, &NAN, &NAN, 0},
  {"exp", exp, &ZERO, &ONE, 0},
  {"exp", exp, &MZERO, &ONE, 0},
  {"exp", exp, &INF, &INF, 0},
  {"exp", exp, &MINF, &ZERO, 0},
#if !GLIBC2
  {"exp2", exp2, &NAN, &NAN, 0},
  {"exp2", exp2, &ZERO, &ONE, 0},
  {"exp2", exp2, &MZERO, &ONE, 0},
  {"exp2", exp2, &INF, &INF, 0},
  {"exp2", exp2, &MINF, &ZERO, 0},
#endif
  {"expm1", expm1, &NAN, &NAN, 0},
  {"expm1", expm1, &ZERO, &ZERO, 0},
  {"expm1", expm1, &MZERO, &MZERO, 0},
  {"expm1", expm1, &INF, &INF, 0},
  {"expm1", expm1, &MINF, &MONE, 0},
  {"log", log, &NAN, &NAN, 0},
  {"log", log, &ZERO, &MINF, 0},
  {"log", log, &MZERO, &MINF, 0},
  {"log", log, &ONE, &ZERO, 0},
  {"log", log, &MONE, &NAN, 0},
  {"log", log, &INF, &INF, 0},
  {"log10", log10, &NAN, &NAN, 0},
  {"log10", log10, &ZERO, &MINF, 0},
  {"log10", log10, &MZERO, &MINF, 0},
  {"log10", log10, &ONE, &ZERO, 0},
  {"log10", log10, &MONE, &NAN, 0},
  {"log10", log10, &INF, &INF, 0},
  {"log1p", log1p, &NAN, &NAN, 0},
  {"log1p", log1p, &ZERO, &ZERO, 0},
  {"log1p", log1p, &MZERO, &MZERO, 0},
  {"log1p", log1p, &MONE, &MINF, 0},
  {"log1p", log1p, &MTWO, &NAN, 0},
  {"log1p", log1p, &INF, &INF, 0},
#if !GLIBC2
  {"log2", log2, &NAN, &NAN, 0},
  {"log2", log2, &ZERO, &MINF, 0},
  {"log2", log2, &MZERO, &MINF, 0},
  {"log2", log2, &MONE, &NAN, 0},
  {"log2", log2, &INF, &INF, 0},
#endif
  /*  {"fabs", fabs, NAN, NAN, 0}, */
  {"fabs", fabs, &ONE, &ONE, 0},
  {"fabs", fabs, &MONE, &ONE, 0},
  {"fabs", fabs, &ZERO, &ZERO, 0},
  {"fabs", fabs, &MZERO, &ZERO, 0},
  {"fabs", fabs, &INF, &INF, 0},
  {"fabs", fabs, &MINF, &INF, 0},
  {"cbrt", cbrt, &NAN, &NAN, 0},
  {"cbrt", cbrt, &ZERO, &ZERO, 0},
  {"cbrt", cbrt, &MZERO, &MZERO, 0},
  {"cbrt", cbrt, &INF, &INF, 0},
  {"cbrt", cbrt, &MINF, &MINF, 0},
  {"erf", erf, &NAN, &NAN, 0},
  {"erf", erf, &ZERO, &ZERO, 0},
  {"erf", erf, &MZERO, &MZERO, 0},
  {"erf", erf, &INF, &ONE, 0},
  {"erf", erf, &MINF, &MONE, 0},
  {"erfc", erfc, &NAN, &NAN, 0},
  {"erfc", erfc, &INF, &ZERO, 0},
  {"erfc", erfc, &MINF, &TWO, 0},
  {"tgamma", tgamma, &NAN, &NAN, 0},
  {"tgamma", tgamma, &INF, &INF, 0},
  {"tgamma", tgamma, &MONE, &NAN, 0},
  {"tgamma", tgamma, &ZERO, &INF, 0},
  {"tgamma", tgamma, &MINF, &NAN, 0},
#if GLIBC2
  {"lgamma", lgamma, &NAN, &NAN, 0},
  {"lgamma", lgamma, &INF, &INF, 0},
  {"lgamma", lgamma, &MONE, &INF, 0},
  {"lgamma", lgamma, &ZERO, &INF, 0},
  {"lgamma", lgamma, &MINF, &INF, 0},
#else
  {"lgam", lgam, &NAN, &NAN, 0},
  {"lgam", lgam, &INF, &INF, 0},
  {"lgam", lgam, &MONE, &INF, 0},
  {"lgam", lgam, &ZERO, &INF, 0},
  {"lgam", lgam, &MINF, &INF, 0},
#endif
  {"ceil", ceil, &NAN, &NAN, 0},
  {"ceil", ceil, &ZERO, &ZERO, 0},
  {"ceil", ceil, &MZERO, &MZERO, 0},
  {"ceil", ceil, &INF, &INF, 0},
  {"ceil", ceil, &MINF, &MINF, 0},
  {"floor", floor, &NAN, &NAN, 0},
  {"floor", floor, &ZERO, &ZERO, 0},
  {"floor", floor, &MZERO, &MZERO, 0},
  {"floor", floor, &INF, &INF, 0},
  {"floor", floor, &MINF, &MINF, 0},
  {"null", NULL, &ZERO, &ZERO, 0},
};

/* Functions of two variables.  */
double atan2 (double, double);
double pow (double, double);

struct twoarguments
  {
    char *name;			/* Name of the function. */
    double (*func) (double, double);
    double *arg1;
    double *arg2;
    double *answer;
    int thresh;
  };

static struct twoarguments test2[] =
{
  {"atan2", atan2, &ZERO, &ONE, &ZERO, 0},
  {"atan2", atan2, &MZERO, &ONE, &MZERO, 0},
  {"atan2", atan2, &ZERO, &ZERO, &ZERO, 0},
  {"atan2", atan2, &MZERO, &ZERO, &MZERO, 0},
  {"atan2", atan2, &ZERO, &MONE, &PI, 0},
  {"atan2", atan2, &MZERO, &MONE, &MPI, 0},
  {"atan2", atan2, &ZERO, &MZERO, &PI, 0},
  {"atan2", atan2, &MZERO, &MZERO, &MPI, 0},
  {"atan2", atan2, &ONE, &ZERO, &PIO2, 0},
  {"atan2", atan2, &ONE, &MZERO, &PIO2, 0},
  {"atan2", atan2, &MONE, &ZERO, &MPIO2, 0},
  {"atan2", atan2, &MONE, &MZERO, &MPIO2, 0},
  {"atan2", atan2, &ONE, &INF, &ZERO, 0},
  {"atan2", atan2, &MONE, &INF, &MZERO, 0},
  {"atan2", atan2, &INF, &ONE, &PIO2, 0},
  {"atan2", atan2, &INF, &MONE, &PIO2, 0},
  {"atan2", atan2, &MINF, &ONE, &MPIO2, 0},
  {"atan2", atan2, &MINF, &MONE, &MPIO2, 0},
  {"atan2", atan2, &ONE, &MINF, &PI, 0},
  {"atan2", atan2, &MONE, &MINF, &MPI, 0},
  {"atan2", atan2, &INF, &INF, &PIO4, 0},
  {"atan2", atan2, &MINF, &INF, &MPIO4, 0},
  {"atan2", atan2, &INF, &MINF, &THPIO4, 0},
  {"atan2", atan2, &MINF, &MINF, &MTHPIO4, 0},
  {"atan2", atan2, &ONE, &ONE, &PIO4, 0},
  {"atan2", atan2, &NAN, &ONE, &NAN, 0},
  {"atan2", atan2, &ONE, &NAN, &NAN, 0},
  {"atan2", atan2, &NAN, &NAN, &NAN, 0},
  {"pow", pow, &ONE, &ZERO, &ONE, 0},
  {"pow", pow, &ONE, &MZERO, &ONE, 0},
  {"pow", pow, &MONE, &ZERO, &ONE, 0},
  {"pow", pow, &MONE, &MZERO, &ONE, 0},
  {"pow", pow, &INF, &ZERO, &ONE, 0},
  {"pow", pow, &INF, &MZERO, &ONE, 0},
  {"pow", pow, &NAN, &ZERO, &ONE, 0},
  {"pow", pow, &NAN, &MZERO, &ONE, 0},
  {"pow", pow, &TWO, &INF, &INF, 0},
  {"pow", pow, &MTWO, &INF, &INF, 0},
  {"pow", pow, &HALF, &INF, &ZERO, 0},
  {"pow", pow, &MHALF, &INF, &ZERO, 0},
  {"pow", pow, &TWO, &MINF, &ZERO, 0},
  {"pow", pow, &MTWO, &MINF, &ZERO, 0},
  {"pow", pow, &HALF, &MINF, &INF, 0},
  {"pow", pow, &MHALF, &MINF, &INF, 0},
  {"pow", pow, &INF, &HALF, &INF, 0},
  {"pow", pow, &INF, &TWO, &INF, 0},
  {"pow", pow, &INF, &MHALF, &ZERO, 0},
  {"pow", pow, &INF, &MTWO, &ZERO, 0},
  {"pow", pow, &MINF, &THREE, &MINF, 0},
  {"pow", pow, &MINF, &TWO, &INF, 0},
  {"pow", pow, &MINF, &MTHREE, &MZERO, 0},
  {"pow", pow, &MINF, &MTWO, &ZERO, 0},
  {"pow", pow, &NAN, &ONE, &NAN, 0},
  {"pow", pow, &ONE, &NAN, &ONE, 0},
  {"pow", pow, &NAN, &NAN, &NAN, 0},
  {"pow", pow, &ONE, &INF, &ONE, 0},
  {"pow", pow, &MONE, &INF, &ONE, 0},
  {"pow", pow, &ONE, &MINF, &ONE, 0},
  {"pow", pow, &MONE, &MINF, &ONE, 0},
  {"pow", pow, &MTWO, &HALF, &NAN, 0},
  {"pow", pow, &ZERO, &MTHREE, &INF, 0},
  {"pow", pow, &MZERO, &MTHREE, &MINF, 0},
  {"pow", pow, &ZERO, &MHALF, &INF, 0},
  {"pow", pow, &MZERO, &MHALF, &INF, 0},
  {"pow", pow, &ZERO, &THREE, &ZERO, 0},
  {"pow", pow, &MZERO, &THREE, &MZERO, 0},
  {"pow", pow, &ZERO, &HALF, &ZERO, 0},
  {"pow", pow, &MZERO, &HALF, &ZERO, 0},
  {"null", NULL, &ZERO, &ZERO, &ZERO, 0},
};

/* Integer functions of one variable.  */

int isnan (double);
int __signbit (double);

struct intans
  {
    char *name;			/* Name of the function. */
    int (*func) (double);
    double *arg1;
    int ianswer;
  };

static struct intans test3[] =
{
  {"isfinite", __isfinite, &ZERO, 1},
  {"isfinite", __isfinite, &INF, 0},
  {"isfinite", __isfinite, &MINF, 0},
  {"isnan", isnan, &NAN, 1},
  {"isnan", isnan, &INF, 0},
  {"isnan", isnan, &ZERO, 0},
  {"isnan", isnan, &MZERO, 0},
  {"signbit", __signbit, &MZERO, 1},
  {"signbit", __signbit, &MONE, 1},
  {"signbit", __signbit, &ZERO, 0},
  {"signbit", __signbit, &ONE, 0},
  {"signbit", __signbit, &MINF, 1},
  {"signbit", __signbit, &INF, 0},
  {"null", NULL, &ZERO, 0},
};

static volatile double x1;
static volatile double x2;
static volatile double y;
static volatile double answer;

static void
pvec(x)
double x;
{
  union
  {
    double d;
    unsigned short s[4];
  } u;
  int i;

  u.d = x;
  for (i = 0; i < 4; i++)
    printf ("0x%04x ", u.s[i]);
  printf ("\n");
}


int
testvect ()
{
  int i, nerrors, k, ianswer, ntests;
  double (*fun1) (double);
  double (*fun2) (double, double);
  int (*fun3) (double);
  double e;
  union
    {
      double d;
      char c[8];
    } u, v;

  ZERO = 0.0;
  MZERO = NEGZERO;
  HALF = 0.5;
  MHALF = -HALF;
  ONE = 1.0;
  MONE = -ONE;
  TWO = 2.0;
  MTWO = -TWO;
  THREE = 3.0;
  MTHREE = -THREE;
  INF = INFINITY;
  MINF = -INFINITY;
  MPI = -PI;
  PIO2 = 0.5 * PI;
  MPIO2 = -PIO2;
  PIO4 = 0.5 * PIO2;
  MPIO4 = -PIO4;
  THPIO4 = 3.0 * PIO4;
  MTHPIO4 = -THPIO4;

  nerrors = 0;
  ntests = 0;
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
	  if (memcmp(u.c, v.c, 8) != 0)
	    {
	      if( isnan(v.d) && isnan(u.d) )
		goto nxttest1;
	      goto wrongone;
	    }
	  else
	    goto nxttest1;
	}
      if (y != answer)
	{
	  e = y - answer;
	  if (answer != 0.0)
	    e = e / answer;
	  if (e < 0)
	    e = -e;
	  if (e > test1[i].thresh * MACHEP)
	    {
wrongone:
	      printf ("%s (%.16e) = %.16e\n    should be %.16e\n",
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
	  if (memcmp(u.c, v.c, 8) != 0)
	    {
	      if( isnan(v.d) && isnan(u.d) )
		goto nxttest2;
#if 0
	      if( isnan(v.d) )
		pvec(v.d);
	      if( isnan(u.d) )
		pvec(u.d);
#endif
	    goto wrongtwo;
	    }
	  else
	    goto nxttest2;
	}
      if (y != answer)
	{
	  e = y - answer;
	  if (answer != 0.0)
	    e = e / answer;
	  if (e < 0)
	    e = -e;
	  if (e > test2[i].thresh * MACHEP)
	    {
wrongtwo:
	      printf ("%s (%.16e, %.16e) = %.16e\n    should be %.16e\n",
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
	  printf ("%s (%.16e) = %d\n    should be. %d\n",
		  test3[i].name, x1, k, ianswer);
	  nerrors += 1;
	}
      ntests += 1;
      i += 1;
    }

  printf ("%d errors in %d tests\n", nerrors, ntests);
  return (nerrors);
}
