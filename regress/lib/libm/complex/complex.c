/*	$OpenBSD: complex.c,v 1.2 2015/07/16 13:42:06 martynas Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <assert.h>
#include <complex.h>
#include <math.h>

#define	PREC	1000
#define	test(f, r, i)	(					\
	floor((__real__ (f)) * PREC) == floor((r) * PREC) && 	\
	floor((__imag__ (f)) * PREC) == floor((i) * PREC)	\
)
#define	testf(f, r, i)	(					\
	floorf((__real__ (f)) * PREC) == floorf((r) * PREC) && 	\
	floorf((__imag__ (f)) * PREC) == floorf((i) * PREC)	\
)
#define	testl(f, r, i)	(					\
	floorl((__real__ (f)) * PREC) == floorl((r) * PREC) && 	\
	floorl((__imag__ (f)) * PREC) == floorl((i) * PREC)	\
)

int
main(int argc, char *argv[])
{
	double complex r, z4 = -1.1 - 1.1 * I;
	float complex rf, z4f = -1.1F - 1.1F * I;
	long double complex rl, z4l = -1.1L - 1.1L * I;

	r = cacosh(z4);
	assert(test(r, 1.150127, -2.256295));
	r = casinh(z4);
	assert(test(r, -1.150127, -0.685498));
	r = catanh(z4);
	assert(test(r, -0.381870, -1.071985));

	rf = cacoshf(z4f);
	assert(testf(rf, 1.150127F, -2.256295F));
	rf = casinhf(z4f);
	assert(testf(rf, -1.150127F, -0.685498F));
	rf = catanhf(z4f);
	assert(testf(rf, -0.381870F, -1.071985F));

	rl = cacoshl(z4l);
	assert(testl(rl, 1.150127L, -2.256295L));
	rl = casinhl(z4l);
	assert(testl(rl, -1.150127L, -0.685498L));
	rl = catanhl(z4l);
	assert(testl(rl, -0.381870L, -1.071985L));

	return (0);
}
