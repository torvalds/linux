/*	$OpenBSD: tgamma.c,v 1.3 2016/10/24 15:34:59 otto Exp $	*/

/*	Written by Martynas Venckus, 2008,  Public domain.	*/

#include <err.h>
#include <errno.h>
#include <math.h>
#include <float.h>

extern int errno;

#if defined(__vax__)
#define _IEEE		0
#else
#define _IEEE		1
#endif

double
infnan(int iarg)
{
	switch (iarg) {
	case  ERANGE:
		errno = ERANGE;
		return (HUGE);
	case -ERANGE:
		errno = EDOM;
		return (-HUGE);
	default:
		errno = EDOM;
		return (0);
	}
}

int
_isinf(double x)
{
	if (_IEEE) {
		return isinf(x);
	}
	else {
		return errno == ERANGE;
	}
}

int
_isnan(double x)
{
	if (_IEEE) {
		return isnan(x);
	}
	else {
		return errno == ERANGE;
	}
}

int
main(void)
{
	double x;

	/* Random values, approx. -177.79..171.63 */
	x = tgamma(11.0);			/* (11 - 1)! */
	if (floor(x) != 3628800.0)
		errx(1, "tgamma(11.0) = %f", x);

	x = tgamma(3.5);			/* 15/8 * sqrt(pi) */
	if (floor(x * 100) != 332.0)
		errx(1, "tgamma(3.5) = %f", x);

	x = tgamma(-0.5);			/* -2 * sqrt(pi) */
	if (floor(x * 100) != -355.0)
		errx(1, "tgamma(-0.5) = %f", x);

	/* Special cases */
	x = tgamma(-1);				/* Negative integers */
	if (!_isnan(x))
		errx(1, "tgamma(-1) = %f", x);

	x = tgamma(-177.8);			/* x ~< -177.79 */
	if (fabs(x) > DBL_EPSILON)
		errx(1, "tgamma(-177.8) = %f", x);

	x = tgamma(171.64);			/* x ~> 171.63 */
	if (!_isinf(x))
		errx(1, "tgamma(171.64) = %f", x);

	x = tgamma(0.0);
	if (!_isinf(x) || x < 0)
		errx(1, "tgamma(0) = %f", x);

	x = tgamma(-0.0);
	if (!_isinf(x) || x > 0)
		errx(1, "tgamma(0) = %f", x);

	x = tgamma(-HUGE_VAL);
	if (!_isnan(x))
		errx(1, "tgamma(-HUGE_VAL) = %f", x);

	x = tgamma(HUGE_VAL);
	if (!_isinf(x))
		errx(1, "tgamma(HUGE_VAL) = %f", x);

#ifdef NAN
	x = tgamma(NAN);
	if (!_isnan(x))
		errx(1, "tgamma(NaN) = %f", x);
#endif

	return 0;
}
