/*	$OpenBSD: modf_test.c,v 1.2 2023/08/13 06:57:04 miod Exp $	*/

#include <assert.h>
#include <math.h>

/* Test for bug introduced in 4.4BSD modf() on sparc */
/* Public domain, 2014, Tobias Ulmer <tobiasu@tmux.org> */

#define BIGFLOAT (5e15) /* Number large enough to trigger the "big" case */

void
modf_sparc(void)
{
	double f, i;

	f = modf(BIGFLOAT, &i);
	assert(i == BIGFLOAT);
	assert(f == 0.0);

	/* Repeat, maybe we were lucky */
	f = modf(BIGFLOAT, &i);
	assert(i == BIGFLOAT);
	assert(f == 0.0);

	/* With negative number, for good measure */
	f = modf(-BIGFLOAT, &i);
	assert(i == -BIGFLOAT);
	assert(f == 0.0);
}

/* Test for modf() behaviour on Inf and Nan */
/* Written by Willemijn Coene.  Public domain */

void
modf_infnan(void)
{
	double f, i;

	f = modf(__builtin_inf(), &i);
	assert(isinf(i));
	assert(signbit(i) == 0);
	assert(f == 0.0);

	f = modf(-__builtin_inf(), &i);
	assert(isinf(i));
	assert(signbit(i) != 0);
	assert(f == -0.0);

	f = modf(NAN, &i);
	assert(isnan(i));
	assert(signbit(i) == 0);
	assert(isnan(f));
	assert(signbit(f) == 0);

	f = modf(-NAN, &i);
	assert(isnan(i));
	assert(signbit(i) != 0);
	assert(isnan(f));
	assert(signbit(f) != 0);
}

int
main(void)
{
	modf_sparc();
	modf_infnan();
}
