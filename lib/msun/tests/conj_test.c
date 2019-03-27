/*-
 * Copyright (c) 2008 David Schultz <das@FreeBSD.org>
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
 */

/*
 * Tests for conj{,f,l}()
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <complex.h>
#include <fenv.h>
#include <math.h>
#include <stdio.h>

#include "test-utils.h"

#pragma	STDC CX_LIMITED_RANGE	OFF

/* Make sure gcc doesn't use builtin versions of these or honor __pure2. */
static float complex (*libconjf)(float complex) = conjf;
static double complex (*libconj)(double complex) = conj;
static long double complex (*libconjl)(long double complex) = conjl;
static float (*libcrealf)(float complex) = crealf;
static double (*libcreal)(double complex) = creal;
static long double (*libcreall)(long double complex) = creall;
static float (*libcimagf)(float complex) = cimagf;
static double (*libcimag)(double complex) = cimag;
static long double (*libcimagl)(long double complex) = cimagl;

static const double tests[] = {
	/* a +  bI */
	0.0,	0.0,
	0.0,	1.0,
	1.0,	0.0,
	-1.0,	0.0,
	1.0,	-0.0,
	0.0,	-1.0,
	2.0,	4.0,
	0.0,	INFINITY,
	0.0,	-INFINITY,
	INFINITY, 0.0,
	NAN,	1.0,
	1.0,	NAN,
	NAN,	NAN,
	-INFINITY, INFINITY,
};

int
main(void)
{
	static const int ntests = sizeof(tests) / sizeof(tests[0]) / 2;
	complex float in;
	complex long double expected;
	int i;

	printf("1..%d\n", ntests * 3);

	for (i = 0; i < ntests; i++) {
		__real__ expected = __real__ in = tests[2 * i];
		__imag__ in = tests[2 * i + 1];
		__imag__ expected = -cimag(in);

		assert(fpequal(libcrealf(in), __real__ in));
		assert(fpequal(libcreal(in), __real__ in));
		assert(fpequal(libcreall(in), __real__ in));
		assert(fpequal(libcimagf(in), __imag__ in));
		assert(fpequal(libcimag(in), __imag__ in));
		assert(fpequal(libcimagl(in), __imag__ in));

		feclearexcept(FE_ALL_EXCEPT);
		if (!cfpequal(libconjf(in), expected)) {
			printf("not ok %d\t# conjf(%#.2g + %#.2gI): "
			       "wrong value\n",
			       3 * i + 1, creal(in), cimag(in));
		} else if (fetestexcept(FE_ALL_EXCEPT)) {
			printf("not ok %d\t# conjf(%#.2g + %#.2gI): "
			       "threw an exception\n",
			       3 * i + 1, creal(in), cimag(in));
		} else {
			printf("ok %d\t\t# conjf(%#.2g + %#.2gI)\n",
			       3 * i + 1, creal(in), cimag(in));
		}

		feclearexcept(FE_ALL_EXCEPT);
		if (!cfpequal(libconj(in), expected)) {
			printf("not ok %d\t# conj(%#.2g + %#.2gI): "
			       "wrong value\n",
			       3 * i + 2, creal(in), cimag(in));
		} else if (fetestexcept(FE_ALL_EXCEPT)) {
			printf("not ok %d\t# conj(%#.2g + %#.2gI): "
			       "threw an exception\n",
			       3 * i + 2, creal(in), cimag(in));
		} else {
			printf("ok %d\t\t# conj(%#.2g + %#.2gI)\n",
			       3 * i + 2, creal(in), cimag(in));
		}

		feclearexcept(FE_ALL_EXCEPT);
		if (!cfpequal(libconjl(in), expected)) {
			printf("not ok %d\t# conjl(%#.2g + %#.2gI): "
			       "wrong value\n",
			       3 * i + 3, creal(in), cimag(in));
		} else if (fetestexcept(FE_ALL_EXCEPT)) {
			printf("not ok %d\t# conjl(%#.2g + %#.2gI): "
			       "threw an exception\n",
			       3 * i + 3, creal(in), cimag(in));
		} else {
			printf("ok %d\t\t# conjl(%#.2g + %#.2gI)\n",
			       3 * i + 3, creal(in), cimag(in));
		}
	}

	return (0);
}
