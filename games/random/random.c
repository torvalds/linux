/*	$OpenBSD: random.c,v 1.22 2023/02/18 08:52:39 miod Exp $	*/
/*	$NetBSD: random.c,v 1.3 1995/04/22 07:44:05 cgd Exp $	*/

/*
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Guy Harris at Network Appliance Corp.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2014 Taylor R. Campbell
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

#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

__dead void usage(void);

int
clz64(uint64_t x)
{
	static const uint64_t mask[] = {
		0xffffffff00000000ULL, 0xffff0000, 0xff00, 0xf0, 0xc, 0x2,
	};
	static const int zeroes[] = {
		32, 16, 8, 4, 2, 1,
	};
	int clz = 0;
	int i;

	if (x == 0)
		return 64;

	for (i = 0; i < 6; i++) {
		if ((x & mask[i]) == 0)
			clz += zeroes[i];
		else
			x >>= zeroes[i];
	}

	return clz;
}

uint64_t
random64(void)
{
	uint64_t r;

	arc4random_buf(&r, sizeof(uint64_t));

	return r;
}

/*
 * random_real: Generate a stream of bits uniformly at random and
 * interpret it as the fractional part of the binary expansion of a
 * number in [0, 1], 0.00001010011111010100...; then round it.
 */
double
random_real(void)
{
	int exponent = -64;
	uint64_t significand;
	int shift;

	/*
	 * Read zeros into the exponent until we hit a one; the rest
	 * will go into the significand.
	 */
	while (__predict_false((significand = random64()) == 0)) {
		exponent -= 64;
		/*
		 * If the exponent falls below -1074 = emin + 1 - p,
		 * the exponent of the smallest subnormal, we are
		 * guaranteed the result will be rounded to zero.  This
		 * case is so unlikely it will happen in realistic
		 * terms only if random64 is broken.
		 */
		if (__predict_false(exponent < -1074))
			return 0;
	}

	/*
	 * There is a 1 somewhere in significand, not necessarily in
	 * the most significant position.  If there are leading zeros,
	 * shift them into the exponent and refill the less-significant
	 * bits of the significand.  Can't predict one way or another
	 * whether there are leading zeros: there's a fifty-fifty
	 * chance, if random64 is uniformly distributed.
	 */
	shift = clz64(significand);
	if (shift != 0) {
		exponent -= shift;
		significand <<= shift;
		significand |= (random64() >> (64 - shift));
	}

	/*
	 * Set the sticky bit, since there is almost surely another 1
	 * in the bit stream.  Otherwise, we might round what looks
	 * like a tie to even when, almost surely, were we to look
	 * further in the bit stream, there would be a 1 breaking the
	 * tie.
	 */
	significand |= 1;

	/*
	 * Finally, convert to double (rounding) and scale by
	 * 2^exponent.
	 */
	return ldexp((double)significand, exponent);
}

int
main(int argc, char *argv[])
{
	double denom;
	int ch, random_exit, selected, unbuffer_output;
	char *ep;

	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");

	random_exit = unbuffer_output = 0;
	while ((ch = getopt(argc, argv, "erh")) != -1)
		switch (ch) {
		case 'e':
			random_exit = 1;
			break;
		case 'r':
			unbuffer_output = 1;
			break;
		default:
		case 'h':
			usage();
		}

	argc -= optind;
	argv += optind;

	switch (argc) {
	case 0:
		denom = 2;
		break;
	case 1:
		errno = 0;
		denom = strtod(*argv, &ep);
		if (errno == ERANGE)
			err(1, "%s", *argv);
		if (denom < 1 || *ep != '\0')
			errx(1, "denominator is not valid.");
		break;
	default:
		usage();
	}

	/* Return a random exit status between 0 and min(denom - 1, 255). */
	if (random_exit) {
		if (denom > 256)
			denom = 256;

		return arc4random_uniform(denom);
	}

	/*
	 * Act as a filter, randomly choosing lines of the standard input
	 * to write to the standard output.
	 */
	if (unbuffer_output)
		setvbuf(stdout, NULL, _IONBF, 0);

	/*
	 * Select whether to print the first line.  (Prime the pump.)
	 * We find a random number between 0 and 1 and, if it's < 1 / denom,
	 * we select the line.
	 */
	selected = random_real() < 1 / denom;
	while ((ch = getchar()) != EOF) {
		int retch;

		if (selected) {
			errno = 0;
			retch = putchar(ch);
			if (retch == EOF && errno)
				err(2, "putchar");
		}
		if (ch == '\n')
			/* Now see if the next line is to be printed. */
			selected = random_real() < 1 / denom;
	}
	if (ferror(stdin))
		err(2, "stdin");
	return 0;
}

void
usage(void)
{

	(void)fprintf(stderr, "usage: %s [-er] [denominator]\n", getprogname());
	exit(1);
}
