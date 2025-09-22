/*	$OpenBSD: lgamma.c,v 1.3 2016/10/23 18:46:03 otto Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#define __POSIX_VISIBLE 201403

#include <assert.h>
#include <math.h>
#include <float.h>


int
main(int argc, char *argv[])
{
	assert(isnan(lgamma(NAN)));
	assert(isnan(lgammaf(NAN)));

	signgam = 0;
	assert(lgamma(-HUGE_VAL) == HUGE_VAL && signgam == 1);
	signgam = 0;
	assert(lgammaf(-HUGE_VALF) == HUGE_VALF && signgam == 1);
	signgam = 0;
	assert(lgammal(-HUGE_VALL) == HUGE_VALL && signgam == 1);

	signgam = 0;
	assert(lgamma(HUGE_VAL) == HUGE_VAL && signgam == 1);
	signgam = 0;
	assert(lgammaf(HUGE_VALF) == HUGE_VALF && signgam == 1);
	signgam = 0;
	assert(lgammal(HUGE_VALL) == HUGE_VALL && signgam == 1);

	signgam = 0;
	assert(lgamma(-0.0) == HUGE_VAL && signgam == -1);
	signgam = 0;
	assert(lgammaf(-0.0F) == HUGE_VALF && signgam == -1);
	signgam = 0;
	assert(lgammal(-0.0L) == HUGE_VALL && signgam == -1);

	signgam = 0;
	assert(lgamma(0.0) == HUGE_VAL && signgam == 1);
	signgam = 0;
	assert(lgammaf(0.0F) == HUGE_VALF && signgam == 1);
	signgam = 0;
	assert(lgammal(0.0L) == HUGE_VALL && signgam == 1);

	signgam = 0;
	assert(lgamma(1.0) == 0.0 && signgam == 1);
	signgam = 0;
	assert(lgammaf(1.0F) == 0.0F && signgam == 1);
	signgam = 0;
	assert(lgammal(1.0L) == 0.0L && signgam == 1);

	signgam = 0;
	assert(fabs(lgamma(3.0) - M_LN2) < DBL_EPSILON && signgam == 1);
	signgam = 0;
	assert(fabsf(lgammaf(3.0F) - (float)M_LN2) < FLT_EPSILON && signgam == 1);
	signgam = 0;
	assert(fabsl(lgammal(3.0L) - M_LN2l) < LDBL_EPSILON && signgam == 1);

	return (0);
}
