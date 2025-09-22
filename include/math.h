/*	$OpenBSD: math.h,v 1.36 2018/03/10 20:52:58 kettenis Exp $	*/
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
 * from: @(#)fdlibm.h 5.1 93/09/24
 */

#ifndef _MATH_H_
#define _MATH_H_

#include <sys/_types.h>
#include <sys/limits.h>

__BEGIN_DECLS
/*
 * ANSI/POSIX
 */
extern char __infinity[];
#if __GNUC_PREREQ__(3, 3)
#define HUGE_VAL	__builtin_huge_val()
#else /* __GNUC_PREREQ__(3, 3) */
#define HUGE_VAL	(*(double *)(void *)__infinity)
#endif /* __GNUC_PREREQ__(3, 3) */

/*
 * C99
 */
#if __ISO_C_VISIBLE >= 1999
typedef	__double_t	double_t;
typedef	__float_t	float_t;

#if __GNUC_PREREQ__(3, 3)
#define	HUGE_VALF	__builtin_huge_valf()
#define	HUGE_VALL	__builtin_huge_vall()
#define	INFINITY	__builtin_inff()
#define	NAN		__builtin_nanf("")
#else /* __GNUC_PREREQ__(3, 3) */
#define	HUGE_VALF	((float)HUGE_VAL)
#define	HUGE_VALL	((long double)HUGE_VAL)
#define	INFINITY	HUGE_VALF
extern char __nan[];
#define	NAN		(*(float *)(void *)__nan)
#endif /* __GNUC_PREREQ__(3, 3) */

#define	FP_INFINITE	0x01
#define	FP_NAN		0x02
#define	FP_NORMAL	0x04
#define	FP_SUBNORMAL	0x08
#define	FP_ZERO		0x10

#define FP_ILOGB0	(-INT_MAX)
#define FP_ILOGBNAN	INT_MAX

#ifdef	__FP_FAST_FMA
#define	FP_FAST_FMA	1
#endif	/* __FP_FAST_FMA */

#ifdef	__FP_FAST_FMAF
#define	FP_FAST_FMAF	1
#endif	/* __FP_FAST_FMAF */

#ifdef	__FP_FAST_FMAL
#define	FP_FAST_FMAL	1
#endif	/* __FP_FAST_FMAL */

#define	MATH_ERRNO	1
#define	MATH_ERREXCEPT	2
#define	math_errhandling	MATH_ERREXCEPT

#define fpclassify(x) \
	((sizeof (x) == sizeof (float)) ? \
		__fpclassifyf(x) \
	: (sizeof (x) == sizeof (double)) ? \
		__fpclassify(x) \
	:	__fpclassifyl(x))
#define isfinite(x) \
	((sizeof (x) == sizeof (float)) ? \
		__isfinitef(x) \
	: (sizeof (x) == sizeof (double)) ? \
		__isfinite(x) \
	:	__isfinitel(x))
#define isnormal(x) \
	((sizeof (x) == sizeof (float)) ? \
		__isnormalf(x) \
	: (sizeof (x) == sizeof (double)) ? \
		__isnormal(x) \
	:	__isnormall(x))
#define signbit(x) \
	((sizeof (x) == sizeof (float)) ? \
		__signbitf(x) \
	: (sizeof (x) == sizeof (double)) ? \
		__signbit(x) \
	:	__signbitl(x))

#define	isgreater(x, y)		(!isunordered((x), (y)) && (x) > (y))
#define	isgreaterequal(x, y)	(!isunordered((x), (y)) && (x) >= (y))
#define	isless(x, y)		(!isunordered((x), (y)) && (x) < (y))
#define	islessequal(x, y)	(!isunordered((x), (y)) && (x) <= (y))
#define	islessgreater(x, y)	(!isunordered((x), (y)) && \
					((x) > (y) || (y) > (x)))
#define	isunordered(x, y)	(isnan(x) || isnan(y))
#endif /* __ISO_C_VISIBLE >= 1999 */

#define isinf(x) \
	((sizeof (x) == sizeof (float)) ? \
		__isinff(x) \
	: (sizeof (x) == sizeof (double)) ? \
		__isinf(x) \
	:	__isinfl(x))
#define isnan(x) \
	((sizeof (x) == sizeof (float)) ? \
		__isnanf(x) \
	: (sizeof (x) == sizeof (double)) ? \
		__isnan(x) \
	:	__isnanl(x))

/*
 * XOPEN/SVID
 */
#if __BSD_VISIBLE || __XPG_VISIBLE
#define	M_E		((double)2.7182818284590452354)  /* e */
#define	M_LOG2E		((double)1.4426950408889634074)  /* log 2e */
#define	M_LOG10E	((double)0.43429448190325182765) /* log 10e */
#define	M_LN2		((double)0.69314718055994530942) /* log e2 */
#define	M_LN10		((double)2.30258509299404568402) /* log e10 */
#define	M_PI		((double)3.14159265358979323846) /* pi */
#define	M_PI_2		((double)1.57079632679489661923) /* pi/2 */
#define	M_PI_4		((double)0.78539816339744830962) /* pi/4 */
#define	M_1_PI		((double)0.31830988618379067154) /* 1/pi */
#define	M_2_PI		((double)0.63661977236758134308) /* 2/pi */
#define	M_2_SQRTPI	((double)1.12837916709551257390) /* 2/sqrt(pi) */
#define	M_SQRT2		((double)1.41421356237309504880) /* sqrt(2) */
#define	M_SQRT1_2	((double)0.70710678118654752440) /* 1/sqrt(2) */

#define	MAXFLOAT	((float)3.40282346638528860e+38)

extern int signgam;
#endif /* __BSD_VISIBLE || __XPG_VISIBLE */

#if __POSIX_VISIBLE >= 201403
#define	M_El		2.718281828459045235360287471352662498L /* e */
#define	M_LOG2El	1.442695040888963407359924681001892137L /* log 2e */
#define	M_LOG10El	0.434294481903251827651128918916605082L /* log 10e */
#define	M_LN2l		0.693147180559945309417232121458176568L /* log e2 */
#define	M_LN10l		2.302585092994045684017991454684364208L /* log e10 */
#define	M_PIl		3.141592653589793238462643383279502884L /* pi */
#define	M_PI_2l		1.570796326794896619231321691639751442L /* pi/2 */
#define	M_PI_4l		0.785398163397448309615660845819875721L /* pi/4 */
#define	M_1_PIl		0.318309886183790671537767526745028724L /* 1/pi */
#define	M_2_PIl		0.636619772367581343075535053490057448L /* 2/pi */
#define	M_2_SQRTPIl	1.128379167095512573896158903121545172L /* 2/sqrt(pi) */
#define	M_SQRT2l	1.414213562373095048801688724209698079L /* sqrt(2) */
#define	M_SQRT1_2l	0.707106781186547524400844362104849039L /* 1/sqrt(2) */
#endif /* __POSIX_VISIBLE >= 201403 */

#if __BSD_VISIBLE
#define	HUGE		MAXFLOAT
#endif /* __BSD_VISIBLE */

/*
 * ANSI/POSIX
 */
double acos(double);
double asin(double);
double atan(double);
double atan2(double, double);
double cos(double);
double sin(double);
double tan(double);

double cosh(double);
double sinh(double);
double tanh(double);

double exp(double);
double frexp(double, int *);
double ldexp(double, int);
double log(double);
double log10(double);
double modf(double, double *);

double pow(double, double);
double sqrt(double);

double ceil(double);
double fabs(double);
double floor(double);
double fmod(double, double);

/*
 * C99
 */
#if __BSD_VISIBLE || __ISO_C_VISIBLE >= 1999 || __XPG_VISIBLE
double acosh(double);
double asinh(double);
double atanh(double);

double exp2(double);
double expm1(double);
int ilogb(double);
double log1p(double);
double log2(double);
double logb(double);
double scalbn(double, int);
double scalbln(double, long int);

double cbrt(double);
double hypot(double, double);

double erf(double);
double erfc(double);
double lgamma(double);
double tgamma(double);

double nearbyint(double);
double rint(double);
long int lrint(double);
long long int llrint(double);
double round(double);
long int lround(double);
long long int llround(double);
double trunc(double);

double remainder(double, double);
double remquo(double, double, int *);

double copysign(double, double);
double nan(const char *);
double nextafter(double, double);
double nexttoward(double, long double);

double fdim(double, double);
double fmax(double, double);
double fmin(double, double);

double fma(double, double, double);
#endif /* __BSD_VISIBLE || __ISO_C_VISIBLE >= 1999 || __XPG_VISIBLE */

#if __BSD_VISIBLE || __XPG_VISIBLE
double j0(double);
double j1(double);
double jn(int, double);
double scalb(double, double);
double y0(double);
double y1(double);
double yn(int, double);
#endif /* __BSD_VISIBLE || __XPG_VISIBLE */

#if __BSD_VISIBLE || __XPG_VISIBLE <= 500
double gamma(double);
#endif /* __BSD_VISIBLE || __XPG_VISIBLE <= 500 */

/*
 * BSD math library entry points
 */
#if __BSD_VISIBLE
double drem(double, double);
int finite(double);

/*
 * Reentrant version of gamma & lgamma; passes signgam back by reference
 * as the second argument; user must allocate space for signgam.
 */
double gamma_r(double, int *);
double lgamma_r(double, int *);

void sincos(double, double *, double *);

/*
 * IEEE Test Vector
 */
double significand(double);
#endif /* __BSD_VISIBLE */

/*
 * Float versions of C99 functions
 */
#if __ISO_C_VISIBLE >= 1999
float acosf(float);
float asinf(float);
float atanf(float);
float atan2f(float, float);
float cosf(float);
float sinf(float);
float tanf(float);

float acoshf(float);
float asinhf(float);
float atanhf(float);
float coshf(float);
float sinhf(float);
float tanhf(float);

float expf(float);
float exp2f(float);
float expm1f(float);
float frexpf(float, int *);
int ilogbf(float);
float ldexpf(float, int);
float logf(float);
float log10f(float);
float log1pf(float);
float log2f(float);
float logbf(float);
float modff(float, float *);
float scalbnf(float, int);
float scalblnf(float, long int);

float cbrtf(float);
float fabsf(float);
float hypotf(float, float);
float powf(float, float);
float sqrtf(float);

float erff(float);
float erfcf(float);
float lgammaf(float);
float tgammaf(float);

float ceilf(float);
float floorf(float);
float nearbyintf(float);
float rintf(float);
long int lrintf(float);
long long int llrintf(float);
float roundf(float);
long int lroundf(float);
long long int llroundf(float);
float truncf(float);

float fmodf(float, float);
float remainderf(float, float);
float remquof(float, float, int *);

float copysignf(float, float);
float nanf(const char *);
float nextafterf(float, float);
float nexttowardf(float, long double);

float fdimf(float, float);
float fmaxf(float, float);
float fminf(float, float);

float fmaf(float, float, float);
#endif /* __ISO_C_VISIBLE >= 1999 */

#if __BSD_VISIBLE || __XPG_VISIBLE
float j0f(float);
float j1f(float);
float jnf(int, float);
float scalbf(float, float);
float y0f(float);
float y1f(float);
float ynf(int, float);
#endif /* __BSD_VISIBLE || __XPG_VISIBLE */

#if __BSD_VISIBLE || __XPG_VISIBLE <= 500
float gammaf(float);
#endif /* __BSD_VISIBLE || __XPG_VISIBLE <= 500 */

/*
 * Float versions of BSD math library entry points
 */
#if __BSD_VISIBLE
float dremf(float, float);
int finitef(float);
int isinff(float);
int isnanf(float);

/*
 * Float versions of reentrant version of gamma & lgamma; passes
 * signgam back by reference as the second argument; user must
 * allocate space for signgam.
 */
float gammaf_r(float, int *);
float lgammaf_r(float, int *);

void sincosf(float, float *, float *);

/*
 * Float version of IEEE Test Vector
 */
float significandf(float);
#endif /* __BSD_VISIBLE */

/*
 * Long double versions of C99 functions
 */
#if __ISO_C_VISIBLE >= 1999
long double acosl(long double);
long double asinl(long double);
long double atanl(long double);
long double atan2l(long double, long double);
long double cosl(long double);
long double sinl(long double);
long double tanl(long double);

long double acoshl(long double);
long double asinhl(long double);
long double atanhl(long double);
long double coshl(long double);
long double sinhl(long double);
long double tanhl(long double);

long double expl(long double);
long double exp2l(long double);
long double expm1l(long double);
long double frexpl(long double, int *);
int ilogbl(long double);
long double ldexpl(long double, int);
long double logl(long double);
long double log10l(long double);
long double log1pl(long double);
long double log2l(long double);
long double logbl(long double);
long double modfl(long double, long double *);
long double scalbnl(long double, int);
long double scalblnl(long double, long int);

long double cbrtl(long double);
long double fabsl(long double);
long double hypotl(long double, long double);
long double powl(long double, long double);
long double sqrtl(long double);

long double erfl(long double);
long double erfcl(long double);
long double lgammal(long double);
long double tgammal(long double);

long double ceill(long double);
long double floorl(long double);
long double nearbyintl(long double);
long double rintl(long double);
long int lrintl(long double);
long long int llrintl(long double);
long double roundl(long double);
long int lroundl(long double);
long long int llroundl(long double);
long double truncl(long double);

long double fmodl(long double, long double);
long double remainderl(long double, long double);
long double remquol(long double, long double, int *);

long double copysignl(long double, long double);
long double nanl(const char *);
long double nextafterl(long double, long double);
long double nexttowardl(long double, long double);

long double fdiml(long double, long double);
long double fmaxl(long double, long double);
long double fminl(long double, long double);

long double fmal(long double, long double, long double);
#endif /* __ISO_C_VISIBLE >= 1999 */

/*
 * Long double versions of BSD math library entry points
 */
#if __BSD_VISIBLE
void sincosl(long double, long double *, long double *);
#endif

/*
 * Library implementation
 */
int __fpclassify(double);
int __fpclassifyf(float);
int __fpclassifyl(long double);
int __isfinite(double);
int __isfinitef(float);
int __isfinitel(long double);
int __isinf(double);
int __isinff(float);
int __isinfl(long double);
int __isnan(double);
int __isnanf(float);
int __isnanl(long double);
int __isnormal(double);
int __isnormalf(float);
int __isnormall(long double);
int __signbit(double);
int __signbitf(float);
int __signbitl(long double);
__END_DECLS

#endif /* !_MATH_H_ */
