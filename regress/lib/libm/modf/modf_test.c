/*	$OpenBSD: modf_test.c,v 1.1 2023/08/13 07:02:36 miod Exp $	*/

/*
 * Written by Willemijn Coene.  Public domain
 */

#include <assert.h>
#include <math.h>

void
modff_infnan(void)
{
	float f, i;

	f = modff(__builtin_inff(), &i);
	assert(isinf(i));
	assert(signbit(i) == 0);
	assert(f == 0.0f);

	f = modff(-__builtin_inff(), &i);
	assert(isinf(i));
	assert(signbit(i) != 0);
	assert(f == -0.0f);

	f = modff(NAN, &i);
	assert(isnan(i));
	assert(signbit(i) == 0);
	assert(isnan(f));
	assert(signbit(f) == 0);

	f = modff(-NAN, &i);
	assert(isnan(i));
	assert(signbit(i) != 0);
	assert(isnan(f));
	assert(signbit(f) != 0);
}

void
modfl_infnan(void)
{
	long double f, i;

	f = modfl(__builtin_infl(), &i);
	assert(isinf(i));
	assert(signbit(i) == 0);
	assert(f == 0.0L);

	f = modfl(-__builtin_infl(), &i);
	assert(isinf(i));
	assert(signbit(i) != 0);
	assert(f == -0.0L);

	f = modfl(NAN, &i);
	assert(isnan(i));
	assert(signbit(i) == 0);
	assert(isnan(f));
	assert(signbit(f) == 0);

	f = modfl(-NAN, &i);
	assert(isnan(i));
	assert(signbit(i) != 0);
	assert(isnan(f));
	assert(signbit(f) != 0);
}

int
main(void)
{
	modff_infnan();
	modfl_infnan();
}
