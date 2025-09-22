/* $OpenBSD: res_random.c,v 1.27 2024/09/03 18:21:55 op Exp $ */

/*
 * Copyright 1997 Niels Provos <provos@physnet.uni-hamburg.de>
 * Copyright 2008 Damien Miller <djm@openbsd.org>
 * All rights reserved.
 *
 * Theo de Raadt <deraadt@openbsd.org> came up with the idea of using
 * such a mathematical system to generate more random (yet non-repeating)
 * ids to solve the resolver/named problem.  But Niels designed the
 * actual system based on the constraints.
 *
 * Later modified by Damien Miller to wrap the LCG output in a 15-bit
 * permutation generator based on a Luby-Rackoff block cipher. This
 * ensures the output is non-repeating and preserves the MSB twiddle
 * trick, but makes it more resistant to LCG prediction.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* 
 * seed = random 15bit
 * n = prime, g0 = generator to n,
 * j = random so that gcd(j,n-1) == 1
 * g = g0^j mod n will be a generator again.
 *
 * X[0] = random seed.
 * X[n] = a*X[n-1]+b mod m is a Linear Congruential Generator
 * with a = 7^(even random) mod m, 
 *      b = random with gcd(b,m) == 1
 *      m = 31104 and a maximal period of m-1.
 *
 * The transaction id is determined by:
 * id[n] = seed xor (g^X[n] mod n)
 *
 * Effectively the id is restricted to the lower 15 bits, thus
 * yielding two different cycles by toggling the msb on and off.
 * This avoids reuse issues caused by reseeding.
 *
 * The output of this generator is then randomly permuted though a
 * custom 15 bit Luby-Rackoff block cipher.
 */

#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <resolv.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "thread_private.h"

#define RU_OUT  	180	/* Time after which will be reseeded */
#define RU_MAX		30000	/* Uniq cycle, avoid blackjack prediction */
#define RU_GEN		2	/* Starting generator */
#define RU_N		32749	/* RU_N-1 = 2*2*3*2729 */
#define RU_AGEN		7	/* determine ru_a as RU_AGEN^(2*rand) */
#define RU_M		31104	/* RU_M = 2^7*3^5 - don't change */
#define RU_ROUNDS	11	/* Number of rounds for permute (odd) */

struct prf_ctx {
	/* PRF lookup table for odd rounds (7 bits input to 8 bits output) */
	u_char prf7[(RU_ROUNDS / 2) * (1 << 7)];

	/* PRF lookup table for even rounds (8 bits input to 7 bits output) */
	u_char prf8[((RU_ROUNDS + 1) / 2) * (1 << 8)];
};

#define PFAC_N 3
static const u_int16_t pfacts[PFAC_N] = {
	2, 
	3,
	2729
};

static u_int16_t ru_x;
static u_int16_t ru_seed, ru_seed2;
static u_int16_t ru_a, ru_b;
static u_int16_t ru_g;
static u_int16_t ru_counter = 0;
static u_int16_t ru_msb = 0;
static struct prf_ctx *ru_prf = NULL;
static time_t ru_reseed;
static pid_t ru_pid;

static u_int16_t pmod(u_int16_t, u_int16_t, u_int16_t);
static void res_initid(void);

/*
 * Do a fast modular exponation, returned value will be in the range
 * of 0 - (mod-1)
 */
static u_int16_t
pmod(u_int16_t gen, u_int16_t exp, u_int16_t mod)
{
	u_int16_t s, t, u;

	s = 1;
	t = gen;
	u = exp;

	while (u) {
		if (u & 1)
			s = (s * t) % mod;
		u >>= 1;
		t = (t * t) % mod;
	}
	return (s);
}

/*
 * 15-bit permutation based on Luby-Rackoff block cipher
 */
static u_int
permute15(u_int in)
{
	int i;
	u_int left, right, tmp;

	if (ru_prf == NULL)
		return in;

	left = (in >> 8) & 0x7f;
	right = in & 0xff;

	/*
	 * Each round swaps the width of left and right. Even rounds have
	 * a 7-bit left, odd rounds have an 8-bit left.	Since this uses an
	 * odd number of rounds, left is always 8 bits wide at the end.
	 */
	for (i = 0; i < RU_ROUNDS; i++) {
		if ((i & 1) == 0)
			tmp = ru_prf->prf8[(i << (8 - 1)) | right] & 0x7f;
		else
			tmp = ru_prf->prf7[((i - 1) << (7 - 1)) | right];
		tmp ^= left;
		left = right;
		right = tmp;
	}

	return (right << 8) | left;
}

/* 
 * Initializes the seed and chooses a suitable generator. Also toggles 
 * the msb flag. The msb flag is used to generate two distinct
 * cycles of random numbers and thus avoiding reuse of ids.
 *
 * This function is called from res_randomid() when needed, an 
 * application does not have to worry about it.
 */
static void 
res_initid(void)
{
	u_int16_t j, i;
	u_int32_t tmp;
	int noprime = 1;
	struct timespec ts;

	ru_x = arc4random_uniform(RU_M);

	/* 15 bits of random seed */
	tmp = arc4random();
	ru_seed = (tmp >> 16) & 0x7FFF;
	ru_seed2 = tmp & 0x7FFF;

	/* Determine the LCG we use */
	tmp = arc4random();
	ru_b = (tmp & 0xfffe) | 1;
	ru_a = pmod(RU_AGEN, (tmp >> 16) & 0xfffe, RU_M);
	while (ru_b % 3 == 0)
		ru_b += 2;
	
	j = arc4random_uniform(RU_N);

	/* 
	 * Do a fast gcd(j,RU_N-1), so we can find a j with
	 * gcd(j, RU_N-1) == 1, giving a new generator for
	 * RU_GEN^j mod RU_N
	 */

	while (noprime) {
		for (i = 0; i < PFAC_N; i++)
			if (j % pfacts[i] == 0)
				break;

		if (i >= PFAC_N)
			noprime = 0;
		else 
			j = (j + 1) % RU_N;
	}

	ru_g = pmod(RU_GEN, j, RU_N);
	ru_counter = 0;

	/* Initialise PRF for Luby-Rackoff permutation */
	if (ru_prf == NULL)
		ru_prf = malloc(sizeof(*ru_prf));
	if (ru_prf != NULL)
		arc4random_buf(ru_prf, sizeof(*ru_prf));

	WRAP(clock_gettime)(CLOCK_MONOTONIC, &ts);
	ru_reseed = ts.tv_sec + RU_OUT;
	ru_msb = ru_msb == 0x8000 ? 0 : 0x8000; 
}

u_int
__res_randomid(void)
{
	struct timespec ts;
	pid_t pid;
	u_int r;
	static void *randomid_mutex;

	WRAP(clock_gettime)(CLOCK_MONOTONIC, &ts);
	pid = getpid();

	_MUTEX_LOCK(&randomid_mutex);

	if (ru_counter >= RU_MAX || ts.tv_sec > ru_reseed || pid != ru_pid) {
		res_initid();
		ru_pid = pid;
	}

	/* Linear Congruential Generator */
	ru_x = (ru_a * ru_x + ru_b) % RU_M;
	ru_counter++;

	r = permute15(ru_seed ^ pmod(ru_g, ru_seed2 + ru_x, RU_N)) | ru_msb;

	_MUTEX_UNLOCK(&randomid_mutex);

	return (r);
}
DEF_STRONG(__res_randomid);

#if 0
int
main(int argc, char **argv)
{
	int i, n;
	u_int16_t wert;

	res_initid();

	printf("Generator: %u\n", ru_g);
	printf("Seed: %u\n", ru_seed);
	printf("Reseed at %ld\n", ru_reseed);
	printf("Ru_X: %u\n", ru_x);
	printf("Ru_A: %u\n", ru_a);
	printf("Ru_B: %u\n", ru_b);

	n = argc > 1 ? atoi(argv[1]) : 60001;
	for (i=0;i<n;i++) {
		wert = res_randomid();
		printf("%u\n", wert);
	}
	return 0;
}
#endif

