/*-
 * Copyright (c) 2004 Stefan Farfeleder <stefanf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
/* All of our functions have side effects, __pure2 causes functions calls to
 * be optimised away.  Stop that. */
#undef __pure2
#define	__pure2

#include <assert.h>
#include <stdio.h>
#include <tgmath.h>

int n_float, n_double, n_long_double;
int n_float_complex, n_double_complex, n_long_double_complex;

int currtest = 0;

#define	TGMACRO(FNC)							\
	TGMACRO_REAL(FNC)						\
	TGMACRO_COMPLEX(c ## FNC)

#define	TGMACRO_REAL(FNC)						\
	float (FNC ## f)(float x) { n_float++; }			\
	double (FNC)(double x) { n_double++; }				\
	long double (FNC ## l)(long double x) { n_long_double++; }

#define	TGMACRO_REAL_REAL(FNC)						\
	float (FNC ## f)(float x, float y) { n_float++; }		\
	double (FNC)(double x, double y) { n_double++; }		\
	long double							\
	(FNC ## l)(long double x, long double y) { n_long_double++; }

#define	TGMACRO_REAL_FIXED_RET(FNC, TYPE)				\
	TYPE (FNC ## f)(float x) { n_float++; }				\
	TYPE (FNC)(double x) { n_double++; }				\
	TYPE (FNC ## l)(long double x) { n_long_double++; }

#define	TGMACRO_COMPLEX(FNC)						\
	float complex (FNC ## f)(float complex x) { n_float_complex++; }\
	double complex (FNC)(double complex x) { n_double_complex++; }	\
	long double complex						\
	(FNC ## l)(long double complex x) { n_long_double_complex++; }

#define	TGMACRO_COMPLEX_REAL_RET(FNC)					\
	float (FNC ## f)(float complex x) { n_float_complex++; }	\
	double (FNC)(double complex x) { n_double_complex++; }		\
	long double							\
	(FNC ## l)(long double complex x) { n_long_double_complex++; }


/* 7.22#4 */
TGMACRO(acos)
TGMACRO(asin)
TGMACRO(atan)
TGMACRO(acosh)
TGMACRO(asinh)
TGMACRO(atanh)
TGMACRO(cos)
TGMACRO(sin)
TGMACRO(tan)
TGMACRO(cosh)
TGMACRO(sinh)
TGMACRO(tanh)
TGMACRO(exp)
TGMACRO(log)
TGMACRO_REAL_REAL(pow)
float complex (cpowf)(float complex x, float complex y) { n_float_complex++; }
double complex
(cpow)(double complex x, double complex y) { n_double_complex++; }
long double complex
(cpowl)(long double complex x, long double complex y)
{ n_long_double_complex++; }
TGMACRO(sqrt)
TGMACRO_REAL(fabs)
TGMACRO_COMPLEX_REAL_RET(cabs)

/* 7.22#5 */
TGMACRO_REAL_REAL(atan2)
TGMACRO_REAL(cbrt)
TGMACRO_REAL(ceil)
TGMACRO_REAL_REAL(copysign)
TGMACRO_REAL(erf)
TGMACRO_REAL(erfc)
TGMACRO_REAL(exp2)
TGMACRO_REAL(expm1)
TGMACRO_REAL_REAL(fdim)
TGMACRO_REAL(floor)
float (fmaf)(float x, float y, float z) { n_float++; }
double (fma)(double x, double y, double z) { n_double++; }
long double
(fmal)(long double x, long double y, long double z) { n_long_double++; }
TGMACRO_REAL_REAL(fmax)
TGMACRO_REAL_REAL(fmin)
TGMACRO_REAL_REAL(fmod)
float (frexpf)(float x, int *e) { n_float++; }
double (frexp)(double x, int *e) { n_double++; }
long double (frexpl)(long double x, int *e) { n_long_double++; }
TGMACRO_REAL_REAL(hypot)
TGMACRO_REAL_FIXED_RET(ilogb, int)
float (ldexpf)(float x, int e) { n_float++; }
double (ldexp)(double x, int e) { n_double++; }
long double (ldexpl)(long double x, int e) { n_long_double++; }
TGMACRO_REAL(lgamma)
TGMACRO_REAL_FIXED_RET(llrint, long long)
TGMACRO_REAL_FIXED_RET(llround, long long)
TGMACRO_REAL(log10)
TGMACRO_REAL(log1p)
TGMACRO_REAL(log2)
TGMACRO_REAL(logb)
TGMACRO_REAL_FIXED_RET(lrint, long)
TGMACRO_REAL_FIXED_RET(lround, long)
TGMACRO_REAL(nearbyint)
TGMACRO_REAL_REAL(nextafter)
float (nexttowardf)(float x, long double y) { n_float++; }
double (nexttoward)(double x, long double y) { n_double++; }
long double (nexttowardl)(long double x, long double y) { n_long_double++; }
TGMACRO_REAL_REAL(remainder)
float (remquof)(float x, float y, int *q) { n_float++; }
double (remquo)(double x, double y, int *q) { n_double++; }
long double (remquol)(long double x, long double y, int *q) { n_long_double++; }
TGMACRO_REAL(rint)
TGMACRO_REAL(round)
float (scalbnf)(float x, int n) { n_float++; }
double (scalbn)(double x, int n) { n_double++; }
long double (scalbnl)(long double x, int n) { n_long_double++; }
float (scalblnf)(float x, long n) { n_float++; }
double (scalbln)(double x, long n) { n_double++; }
long double (scalblnl)(long double x, long n) { n_long_double++; }
TGMACRO_REAL(tgamma)
TGMACRO_REAL(trunc)

/* 7.22#6 */
TGMACRO_COMPLEX_REAL_RET(carg)
TGMACRO_COMPLEX_REAL_RET(cimag)
TGMACRO_COMPLEX(conj)
TGMACRO_COMPLEX(cproj)
TGMACRO_COMPLEX_REAL_RET(creal)


long double ld;
double d;
float f;
long double complex ldc;
double complex dc;
float complex fc;
unsigned long long ull;
int i;
_Bool b;

#define	SAMETYPE(EXP, TYPE)						\
	__builtin_types_compatible_p(__typeof__(EXP), TYPE)

#define	CLEAR_COUNTERS							\
	(n_float = n_double = n_long_double = 0,			\
	    n_float_complex = n_double_complex = n_long_double_complex = 0, 1)

#define	RUN_TEST(EXP, TYPE)	(EXP, SAMETYPE(EXP, TYPE))

#define	PASS_REAL_ARG_REAL_RET(FNC) PASS_REAL_ARG_REAL_RET_(FNC,)

#define	PASS_REAL_ARG_REAL_RET_(FNC, SUFFIX)				\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(1.l), long double) &&				\
	RUN_TEST(FNC(ld), long double) &&				\
	n_long_double ## SUFFIX == 2 &&					\
	RUN_TEST(FNC(1.), double) &&					\
	RUN_TEST(FNC(d), double) &&					\
	RUN_TEST(FNC(1ull), double) &&					\
	RUN_TEST(FNC(ull), double) &&					\
	RUN_TEST(FNC(1), double) &&					\
	RUN_TEST(FNC(i), double) &&					\
	RUN_TEST(FNC((_Bool)0), double) &&				\
	RUN_TEST(FNC(b), double) &&					\
	n_double ## SUFFIX == 8 &&					\
	RUN_TEST(FNC(1.f), float) &&					\
	RUN_TEST(FNC(f), float) &&					\
	n_float ## SUFFIX == 2

#define	PASS_REAL_ARG_FIXED_RET(FNC, RET)				\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(1.l), RET) &&					\
	RUN_TEST(FNC(ld), RET) &&					\
	n_long_double == 2 &&						\
	RUN_TEST(FNC(1.), RET) &&					\
	RUN_TEST(FNC(d), RET) &&					\
	RUN_TEST(FNC(1ull), RET) &&					\
	RUN_TEST(FNC(ull), RET) &&					\
	RUN_TEST(FNC(1), RET) &&					\
	RUN_TEST(FNC(i), RET) &&					\
	RUN_TEST(FNC((_Bool)0), RET) &&					\
	RUN_TEST(FNC(b), RET) &&					\
	n_double == 8 &&						\
	RUN_TEST(FNC(1.f), RET) &&					\
	RUN_TEST(FNC(f), RET) &&					\
	n_float == 2

#define	PASS_REAL_FIXED_ARG_REAL_RET(FNC, ARG2)				\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(1.l, ARG2), long double) &&			\
	RUN_TEST(FNC(ld, ARG2), long double) &&				\
	n_long_double == 2 &&						\
	RUN_TEST(FNC(1., ARG2), double) &&				\
	RUN_TEST(FNC(d, ARG2), double) &&				\
	RUN_TEST(FNC(1ull, ARG2), double) &&				\
	RUN_TEST(FNC(ull, ARG2), double) &&				\
	RUN_TEST(FNC(1, ARG2), double) &&				\
	RUN_TEST(FNC(i, ARG2), double) &&				\
	RUN_TEST(FNC((_Bool)0, ARG2), double) &&			\
	RUN_TEST(FNC(b, ARG2), double) &&				\
	n_double == 8 &&						\
	RUN_TEST(FNC(1.f, ARG2), float) &&				\
	RUN_TEST(FNC(f, ARG2), float) &&				\
	n_float == 2

#define	PASS_REAL_REAL_ARG_REAL_RET(FNC)				\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(1.l, 1.l), long double) &&				\
	RUN_TEST(FNC(1.l, 1.), long double) &&				\
	RUN_TEST(FNC(1.l, 1.f), long double) &&				\
	RUN_TEST(FNC(1.l, 1), long double) &&				\
	RUN_TEST(FNC(1.l, (_Bool)0), long double) &&			\
	RUN_TEST(FNC(1.l, ld), long double) &&				\
	RUN_TEST(FNC(1., ld), long double) &&				\
	RUN_TEST(FNC(1.f, ld), long double) &&				\
	RUN_TEST(FNC(1, ld), long double) &&				\
	RUN_TEST(FNC((_Bool)0, ld), long double) &&			\
	n_long_double == 10 &&						\
	RUN_TEST(FNC(d, 1.), double) &&					\
	RUN_TEST(FNC(d, 1.f), double) &&				\
	RUN_TEST(FNC(d, 1l), double) &&					\
	RUN_TEST(FNC(d, (_Bool)0), double) &&				\
	RUN_TEST(FNC(1., 1.), double) &&				\
	RUN_TEST(FNC(1.f, 1.), double) &&				\
	RUN_TEST(FNC(1l, 1.), double) &&				\
	RUN_TEST(FNC((_Bool)0, 1.), double) &&				\
	RUN_TEST(FNC(1ull, f), double) &&				\
	RUN_TEST(FNC(1.f, ull), double) &&				\
	RUN_TEST(FNC(1, 1l), double) &&					\
	RUN_TEST(FNC(1u, i), double) &&					\
	RUN_TEST(FNC((_Bool)0, 1.f), double) &&				\
	RUN_TEST(FNC(1.f, b), double) &&				\
	n_double == 14 &&						\
	RUN_TEST(FNC(1.f, 1.f), float) &&				\
	RUN_TEST(FNC(1.f, 1.f), float) &&				\
	RUN_TEST(FNC(f, 1.f), float) &&					\
	RUN_TEST(FNC(f, f), float) &&					\
	n_float == 4

#define	PASS_REAL_REAL_FIXED_ARG_REAL_RET(FNC, ARG3)			\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(1.l, 1.l, ARG3), long double) &&			\
	RUN_TEST(FNC(1.l, 1., ARG3), long double) &&			\
	RUN_TEST(FNC(1.l, 1.f, ARG3), long double) &&			\
	RUN_TEST(FNC(1.l, 1, ARG3), long double) &&			\
	RUN_TEST(FNC(1.l, (_Bool)0, ARG3), long double) &&		\
	RUN_TEST(FNC(1.l, ld, ARG3), long double) &&			\
	RUN_TEST(FNC(1., ld, ARG3), long double) &&			\
	RUN_TEST(FNC(1.f, ld, ARG3), long double) &&			\
	RUN_TEST(FNC(1, ld, ARG3), long double) &&			\
	RUN_TEST(FNC((_Bool)0, ld, ARG3), long double) &&		\
	n_long_double == 10 &&						\
	RUN_TEST(FNC(d, 1., ARG3), double) &&				\
	RUN_TEST(FNC(d, 1.f, ARG3), double) &&				\
	RUN_TEST(FNC(d, 1l, ARG3), double) &&				\
	RUN_TEST(FNC(d, (_Bool)0, ARG3), double) &&			\
	RUN_TEST(FNC(1., 1., ARG3), double) &&				\
	RUN_TEST(FNC(1.f, 1., ARG3), double) &&				\
	RUN_TEST(FNC(1l, 1., ARG3), double) &&				\
	RUN_TEST(FNC((_Bool)0, 1., ARG3), double) &&			\
	RUN_TEST(FNC(1ull, f, ARG3), double) &&				\
	RUN_TEST(FNC(1.f, ull, ARG3), double) &&			\
	RUN_TEST(FNC(1, 1l, ARG3), double) &&				\
	RUN_TEST(FNC(1u, i, ARG3), double) &&				\
	RUN_TEST(FNC((_Bool)0, 1.f, ARG3), double) &&			\
	RUN_TEST(FNC(1.f, b, ARG3), double) &&				\
	n_double == 14 &&						\
	RUN_TEST(FNC(1.f, 1.f, ARG3), float) &&				\
	RUN_TEST(FNC(1.f, 1.f, ARG3), float) &&				\
	RUN_TEST(FNC(f, 1.f, ARG3), float) &&				\
	RUN_TEST(FNC(f, f, ARG3), float) &&				\
	n_float == 4

#define	PASS_REAL_REAL_REAL_ARG_REAL_RET(FNC)				\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(ld, d, f), long double) &&				\
	RUN_TEST(FNC(1, ld, ld), long double) &&			\
	RUN_TEST(FNC(1, d, ld), long double) &&				\
	n_long_double == 3 &&						\
	RUN_TEST(FNC(1, f, 1.f), double) &&				\
	RUN_TEST(FNC(f, d, 1.f), double) &&				\
	RUN_TEST(FNC(f, 1.f, 1.), double) &&				\
	n_double == 3 &&						\
	RUN_TEST(FNC(f, 1.f, f), float) &&				\
	n_float == 1

#define	PASS_REAL_ARG_COMPLEX_RET(FNC)					\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(1.l), long double complex) &&			\
	RUN_TEST(FNC(ld), long double complex) &&			\
	n_long_double_complex == 2 &&					\
	RUN_TEST(FNC(1.), double complex) &&				\
	RUN_TEST(FNC(d), double complex) &&				\
	RUN_TEST(FNC(1l), double complex) &&				\
	RUN_TEST(FNC(i), double complex) &&				\
	RUN_TEST(FNC(b), double complex) &&				\
	n_double_complex == 5 &&					\
	RUN_TEST(FNC(1.f), float complex) &&				\
	RUN_TEST(FNC(f), float complex) &&				\
	n_float_complex == 2

#define	PASS_COMPLEX_ARG_COMPLEX_RET(FNC)				\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(ldc), long double complex) &&			\
	n_long_double_complex == 1 &&					\
	RUN_TEST(FNC(dc), double complex) &&				\
	n_double_complex == 1 &&					\
	RUN_TEST(FNC(fc), float complex) &&				\
	RUN_TEST(FNC(I), float complex) &&				\
	n_float_complex == 2

#define	PASS_COMPLEX_ARG_REAL_RET(FNC)					\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(ldc), long double) &&				\
	n_long_double_complex == 1 &&					\
	RUN_TEST(FNC(dc), double) &&					\
	n_double_complex == 1 &&					\
	RUN_TEST(FNC(fc), float) &&					\
	RUN_TEST(FNC(I), float) &&					\
	n_float_complex == 2

#define	PASS_COMPLEX_COMPLEX_ARG_COMPLEX_RET(FNC)			\
	CLEAR_COUNTERS &&						\
	RUN_TEST(FNC(ldc, ldc), long double complex) &&			\
	RUN_TEST(FNC(ldc, dc), long double complex) &&			\
	RUN_TEST(FNC(ldc, fc), long double complex) &&			\
	RUN_TEST(FNC(ldc, ld), long double complex) &&			\
	RUN_TEST(FNC(ldc, d), long double complex) &&			\
	RUN_TEST(FNC(ldc, f), long double complex) &&			\
	RUN_TEST(FNC(ldc, i), long double complex) &&			\
	RUN_TEST(FNC(dc, ldc), long double complex) &&			\
	RUN_TEST(FNC(I, ldc), long double complex) &&			\
	RUN_TEST(FNC(1.l, ldc), long double complex) &&			\
	RUN_TEST(FNC(1., ldc), long double complex) &&			\
	RUN_TEST(FNC(1.f, ldc), long double complex) &&			\
	RUN_TEST(FNC(1, ldc), long double complex) &&			\
	RUN_TEST(FNC(ld, dc), long double complex) &&			\
	RUN_TEST(FNC(ld, fc), long double complex) &&			\
	RUN_TEST(FNC(I, 1.l), long double complex) &&			\
	RUN_TEST(FNC(dc, 1.l), long double complex) &&			\
	n_long_double_complex == 17 &&					\
	RUN_TEST(FNC(dc, dc), double complex) &&			\
	RUN_TEST(FNC(dc, fc), double complex) &&			\
	RUN_TEST(FNC(dc, d), double complex) &&				\
	RUN_TEST(FNC(dc, f), double complex) &&				\
	RUN_TEST(FNC(dc, ull), double complex) &&			\
	RUN_TEST(FNC(I, dc), double complex) &&				\
	RUN_TEST(FNC(1., dc), double complex) &&			\
	RUN_TEST(FNC(1, dc), double complex) &&				\
	RUN_TEST(FNC(fc, d), double complex) &&				\
	RUN_TEST(FNC(1, I), double complex) &&				\
	n_double_complex == 10 &&					\
	RUN_TEST(FNC(fc, fc), float complex) &&				\
	RUN_TEST(FNC(fc, I), float complex) &&				\
	RUN_TEST(FNC(1.f, fc), float complex) &&			\
	n_float_complex == 3

int failed = 0;
#define	PRINT(STR, X) do {						\
	currtest++;							\
	int result = (X);						\
	if (!result)							\
		failed = 1;						\
	printf("%s %d - %s\n", result ? "ok" : "not ok", currtest, (STR));		\
	fflush(stdout);							\
} while (0)

int
main(void)
{
	printf("1..60\n");

	/* 7.22#4 */
	PRINT("acos",
	    PASS_REAL_ARG_REAL_RET(acos) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(acos));

	PRINT("asin",
	    PASS_REAL_ARG_REAL_RET(asin) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(asin));

	PRINT("atan",
	    PASS_REAL_ARG_REAL_RET(atan) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(atan));

	PRINT("acosh",
	    PASS_REAL_ARG_REAL_RET(acosh) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(acosh));

	PRINT("asinh",
	    PASS_REAL_ARG_REAL_RET(asinh) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(asinh));

	PRINT("atanh",
	    PASS_REAL_ARG_REAL_RET(atanh) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(atanh));

	PRINT("cos",
	    PASS_REAL_ARG_REAL_RET(cos) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(cos));

	PRINT("sin",
	    PASS_REAL_ARG_REAL_RET(sin) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(sin));

	PRINT("tan",
	    PASS_REAL_ARG_REAL_RET(tan) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(tan));

	PRINT("cosh",
	    PASS_REAL_ARG_REAL_RET(cosh) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(cosh));

	PRINT("sinh",
	    PASS_REAL_ARG_REAL_RET(sinh) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(sinh));

	PRINT("tanh",
	    PASS_REAL_ARG_REAL_RET(tanh) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(tanh));

	PRINT("exp",
	    PASS_REAL_ARG_REAL_RET(exp) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(exp));

	PRINT("log",
	    PASS_REAL_ARG_REAL_RET(log) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(log));

	PRINT("pow",
	    PASS_REAL_REAL_ARG_REAL_RET(pow) &&
	    PASS_COMPLEX_COMPLEX_ARG_COMPLEX_RET(pow));

	PRINT("sqrt",
	    PASS_REAL_ARG_REAL_RET(sqrt) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(sqrt));

	PRINT("fabs",
	    PASS_REAL_ARG_REAL_RET(fabs) &&
	    PASS_COMPLEX_ARG_REAL_RET(fabs));

	/* 7.22#5 */
	PRINT("atan2",
	    PASS_REAL_REAL_ARG_REAL_RET(atan2));

	PRINT("cbrt",
	    PASS_REAL_ARG_REAL_RET(cbrt));

	PRINT("ceil",
	    PASS_REAL_ARG_REAL_RET(ceil));

	PRINT("copysign",
	    PASS_REAL_REAL_ARG_REAL_RET(copysign));

	PRINT("erf",
	    PASS_REAL_ARG_REAL_RET(erf));

	PRINT("erfc",
	    PASS_REAL_ARG_REAL_RET(erfc));

	PRINT("exp2",
	    PASS_REAL_ARG_REAL_RET(exp2));

	PRINT("expm1",
	    PASS_REAL_ARG_REAL_RET(expm1));

	PRINT("fdim",
	    PASS_REAL_REAL_ARG_REAL_RET(fdim));

	PRINT("floor",
	    PASS_REAL_ARG_REAL_RET(floor));

	PRINT("fma",
	    PASS_REAL_REAL_REAL_ARG_REAL_RET(fma));

	PRINT("fmax",
	    PASS_REAL_REAL_ARG_REAL_RET(fmax));

	PRINT("fmin",
	    PASS_REAL_REAL_ARG_REAL_RET(fmin));

	PRINT("fmod",
	    PASS_REAL_REAL_ARG_REAL_RET(fmod));

	PRINT("frexp",
	    PASS_REAL_FIXED_ARG_REAL_RET(frexp, &i));

	PRINT("hypot",
	    PASS_REAL_REAL_ARG_REAL_RET(hypot));

	PRINT("ilogb",
	    PASS_REAL_ARG_FIXED_RET(ilogb, int));

	PRINT("ldexp",
	    PASS_REAL_FIXED_ARG_REAL_RET(ldexp, 1) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(ldexp, ld) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(ldexp, ldc));

	PRINT("lgamma",
	    PASS_REAL_ARG_REAL_RET(lgamma));

	PRINT("llrint",
	    PASS_REAL_ARG_FIXED_RET(llrint, long long));

	PRINT("llround",
	    PASS_REAL_ARG_FIXED_RET(llround, long long));

	PRINT("log10",
	    PASS_REAL_ARG_REAL_RET(log10));

	PRINT("log1p",
	    PASS_REAL_ARG_REAL_RET(log1p));

	PRINT("log2",
	    PASS_REAL_ARG_REAL_RET(log2));

	PRINT("logb",
	    PASS_REAL_ARG_REAL_RET(logb));

	PRINT("lrint",
	    PASS_REAL_ARG_FIXED_RET(lrint, long));

	PRINT("lround",
	    PASS_REAL_ARG_FIXED_RET(lround, long));

	PRINT("nearbyint",
	    PASS_REAL_ARG_REAL_RET(nearbyint));

	PRINT("nextafter",
	    PASS_REAL_REAL_ARG_REAL_RET(nextafter));

	PRINT("nexttoward",
	    PASS_REAL_FIXED_ARG_REAL_RET(nexttoward, 1) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(nexttoward, ull) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(nexttoward, d) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(nexttoward, fc));

	PRINT("remainder",
	    PASS_REAL_REAL_ARG_REAL_RET(remainder));

	PRINT("remquo",
	    PASS_REAL_REAL_FIXED_ARG_REAL_RET(remquo, &i));

	PRINT("rint",
	    PASS_REAL_ARG_REAL_RET(rint));

	PRINT("round",
	    PASS_REAL_ARG_REAL_RET(round));

	PRINT("scalbn",
	    PASS_REAL_FIXED_ARG_REAL_RET(scalbn, 1) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(scalbn, b) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(scalbn, I));

	PRINT("scalbln",
	    PASS_REAL_FIXED_ARG_REAL_RET(scalbln, i) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(scalbln, 1.l) &&
	    PASS_REAL_FIXED_ARG_REAL_RET(scalbln, dc));

	PRINT("tgamma",
	    PASS_REAL_ARG_REAL_RET(tgamma));

	PRINT("trunc",
	    PASS_REAL_ARG_REAL_RET(trunc));

	/* 7.22#6 */
	PRINT("carg",
	    PASS_REAL_ARG_REAL_RET_(carg, _complex) &&
	    PASS_COMPLEX_ARG_REAL_RET(carg));

	PRINT("cimag",
	    PASS_REAL_ARG_REAL_RET_(cimag, _complex) &&
	    PASS_COMPLEX_ARG_REAL_RET(cimag));

	PRINT("conj",
	    PASS_REAL_ARG_COMPLEX_RET(conj) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(conj));

	PRINT("cproj",
	    PASS_REAL_ARG_COMPLEX_RET(cproj) &&
	    PASS_COMPLEX_ARG_COMPLEX_RET(cproj));

	PRINT("creal",
	    PASS_REAL_ARG_REAL_RET_(creal, _complex) &&
	    PASS_COMPLEX_ARG_REAL_RET(creal));
}
