/*-
 * Copyright (c) 2015 Dag-Erling Smørgrav
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

#ifdef _KERNEL
#include <sys/libkern.h>
#else
#include <stdio.h>
#include <strings.h>
#endif

#include "fp16.h"

/*
 * Compute the quare root of x, using Newton's method with 2^(log2(x)/2)
 * as the initial estimate.
 */
fp16_t
fp16_sqrt(fp16_t x)
{
	fp16_t y, delta;
	signed int log2x;

	/* special case */
	if (x == 0)
		return (0);

	/* shift toward 0 by half the logarithm */
	log2x = flsl(x) - 1;
	if (log2x >= 16) {
		y = x >> (log2x - 16) / 2;
	} else {
#if 0
		y = x << (16 - log2x) / 2;
#else
		/* XXX for now, return 0 for anything < 1 */
		return (0);
#endif
	}
	while (y > 0) {
		/* delta = y^2 / 2y */
		delta = fp16_div(fp16_sub(fp16_mul(y, y), x), y * 2);
		if (delta == 0)
			break;
		y = fp16_sub(y, delta);
	}
	return (y);
}

static fp16_t fp16_sin_table[256] = {
	    0,	  402,	  804,	 1206,	 1608,	 2010,	 2412,	 2814,
	 3215,	 3617,	 4018,	 4420,	 4821,	 5222,	 5622,	 6023,
	 6423,	 6823,	 7223,	 7623,	 8022,	 8421,	 8819,	 9218,
	 9616,	10013,	10410,	10807,	11204,	11600,	11995,	12390,
	12785,	13179,	13573,	13966,	14359,	14751,	15142,	15533,
	15923,	16313,	16702,	17091,	17479,	17866,	18253,	18638,
	19024,	19408,	19792,	20175,	20557,	20938,	21319,	21699,
	22078,	22456,	22833,	23210,	23586,	23960,	24334,	24707,
	25079,	25450,	25820,	26189,	26557,	26925,	27291,	27656,
	28020,	28383,	28745,	29105,	29465,	29824,	30181,	30538,
	30893,	31247,	31600,	31952,	32302,	32651,	32999,	33346,
	33692,	34036,	34379,	34721,	35061,	35400,	35738,	36074,
	36409,	36743,	37075,	37406,	37736,	38064,	38390,	38716,
	39039,	39362,	39682,	40002,	40319,	40636,	40950,	41263,
	41575,	41885,	42194,	42501,	42806,	43110,	43412,	43712,
	44011,	44308,	44603,	44897,	45189,	45480,	45768,	46055,
	46340,	46624,	46906,	47186,	47464,	47740,	48015,	48288,
	48558,	48828,	49095,	49360,	49624,	49886,	50146,	50403,
	50660,	50914,	51166,	51416,	51665,	51911,	52155,	52398,
	52639,	52877,	53114,	53348,	53581,	53811,	54040,	54266,
	54491,	54713,	54933,	55152,	55368,	55582,	55794,	56004,
	56212,	56417,	56621,	56822,	57022,	57219,	57414,	57606,
	57797,	57986,	58172,	58356,	58538,	58718,	58895,	59070,
	59243,	59414,	59583,	59749,	59913,	60075,	60235,	60392,
	60547,	60700,	60850,	60998,	61144,	61288,	61429,	61568,
	61705,	61839,	61971,	62100,	62228,	62353,	62475,	62596,
	62714,	62829,	62942,	63053,	63162,	63268,	63371,	63473,
	63571,	63668,	63762,	63854,	63943,	64030,	64115,	64197,
	64276,	64353,	64428,	64501,	64571,	64638,	64703,	64766,
	64826,	64884,	64939,	64992,	65043,	65091,	65136,	65179,
	65220,	65258,	65294,	65327,	65358,	65386,	65412,	65436,
	65457,	65475,	65491,	65505,	65516,	65524,	65531,	65534,
};

/*
 * Compute the sine of theta.
 */
fp16_t
fp16_sin(fp16_t theta)
{
	unsigned int i;

	i = 1024 * (theta % FP16_2PI) / FP16_2PI;
	switch (i / 256) {
	case 0:
		return (fp16_sin_table[i % 256]);
	case 1:
		return (fp16_sin_table[255 - i % 256]);
	case 2:
		return (-fp16_sin_table[i % 256]);
	case 3:
		return (-fp16_sin_table[255 - i % 256]);
	default:
		/* inconceivable! */
		return (0);
	}
}

/*
 * Compute the cosine of theta.
 */
fp16_t
fp16_cos(fp16_t theta)
{
	unsigned int i;

	i = 1024 * (theta % FP16_2PI) / FP16_2PI;
	switch (i / 256) {
	case 0:
		return (fp16_sin_table[255 - i % 256]);
	case 1:
		return (-fp16_sin_table[i % 256]);
	case 2:
		return (-fp16_sin_table[255 - i % 256]);
	case 3:
		return (fp16_sin_table[i % 256]);
	default:
		/* inconceivable! */
		return (0);
	}
}
