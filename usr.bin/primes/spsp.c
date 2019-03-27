/*-
 * Copyright (c) 2014 Colin Percival
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
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stddef.h>
#include <stdint.h>

#include "primes.h"

/* Return a * b % n, where 0 < n. */
static uint64_t
mulmod(uint64_t a, uint64_t b, uint64_t n)
{
	uint64_t x = 0;
	uint64_t an = a % n;

	while (b != 0) {
		if (b & 1) {
			x += an;
			if ((x < an) || (x >= n))
				x -= n;
		}
		if (an + an < an)
			an = an + an - n;
		else if (an + an >= n)
			an = an + an - n;
		else
			an = an + an;
		b >>= 1;
	}

	return (x);
}

/* Return a^r % n, where 0 < n. */
static uint64_t
powmod(uint64_t a, uint64_t r, uint64_t n)
{
	uint64_t x = 1;

	while (r != 0) {
		if (r & 1)
			x = mulmod(a, x, n);
		a = mulmod(a, a, n);
		r >>= 1;
	}

	return (x);
}

/* Return non-zero if n is a strong pseudoprime to base p. */
static int
spsp(uint64_t n, uint64_t p)
{
	uint64_t x;
	uint64_t r = n - 1;
	int k = 0;

	/* Compute n - 1 = 2^k * r. */
	while ((r & 1) == 0) {
		k++;
		r >>= 1;
	}

	/* Compute x = p^r mod n.  If x = 1, n is a p-spsp. */
	x = powmod(p, r, n);
	if (x == 1)
		return (1);

	/* Compute x^(2^i) for 0 <= i < n.  If any are -1, n is a p-spsp. */
	while (k > 0) {
		if (x == n - 1)
			return (1);
		x = powmod(x, 2, n);
		k--;
	}

	/* Not a p-spsp. */
	return (0);
}

/* Test for primality using strong pseudoprime tests. */
int
isprime(ubig _n)
{
	uint64_t n = _n;

	/*
	 * Values from:
	 * C. Pomerance, J.L. Selfridge, and S.S. Wagstaff, Jr.,
	 * The pseudoprimes to 25 * 10^9, Math. Comp. 35(151):1003-1026, 1980.
	 */

	/* No SPSPs to base 2 less than 2047. */
	if (!spsp(n, 2))
		return (0);
	if (n < 2047ULL)
		return (1);

	/* No SPSPs to bases 2,3 less than 1373653. */
	if (!spsp(n, 3))
		return (0);
	if (n < 1373653ULL)
		return (1);

	/* No SPSPs to bases 2,3,5 less than 25326001. */
	if (!spsp(n, 5))
		return (0);
	if (n < 25326001ULL)
		return (1);

	/* No SPSPs to bases 2,3,5,7 less than 3215031751. */
	if (!spsp(n, 7))
		return (0);
	if (n < 3215031751ULL)
		return (1);

	/*
	 * Values from:
	 * G. Jaeschke, On strong pseudoprimes to several bases,
	 * Math. Comp. 61(204):915-926, 1993.
	 */

	/* No SPSPs to bases 2,3,5,7,11 less than 2152302898747. */
	if (!spsp(n, 11))
		return (0);
	if (n < 2152302898747ULL)
		return (1);

	/* No SPSPs to bases 2,3,5,7,11,13 less than 3474749660383. */
	if (!spsp(n, 13))
		return (0);
	if (n < 3474749660383ULL)
		return (1);

	/* No SPSPs to bases 2,3,5,7,11,13,17 less than 341550071728321. */
	if (!spsp(n, 17))
		return (0);
	if (n < 341550071728321ULL)
		return (1);

	/* No SPSPs to bases 2,3,5,7,11,13,17,19 less than 341550071728321. */
	if (!spsp(n, 19))
		return (0);
	if (n < 341550071728321ULL)
		return (1);

	/*
	 * Value from:
	 * Y. Jiang and Y. Deng, Strong pseudoprimes to the first eight prime
	 * bases, Math. Comp. 83(290):2915-2924, 2014.
	 */

	/* No SPSPs to bases 2..23 less than 3825123056546413051. */
	if (!spsp(n, 23))
		return (0);
	if (n < 3825123056546413051)
		return (1);

	/*
	 * Value from:
	 * J. Sorenson and J. Webster, Strong pseudoprimes to twelve prime
	 * bases, Math. Comp. 86(304):985-1003, 2017.
	 */

	/* No SPSPs to bases 2..37 less than 318665857834031151167461. */
	if (!spsp(n, 29))
		return (0);
	if (!spsp(n, 31))
		return (0);
	if (!spsp(n, 37))
		return (0);

	/* All 64-bit values are less than 318665857834031151167461. */
	return (1);
}
