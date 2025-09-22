/*	$OpenBSD: ec_arithmetic.c,v 1.1 2025/08/03 08:29:39 jsing Exp $ */
/*
 * Copyright (c) 2022,2025 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/time.h>

#include <err.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/objects.h>

static void
benchmark_ec_point_add(const EC_GROUP *group, EC_POINT *result,
    const BIGNUM *scalar, const EC_POINT *a, const EC_POINT *b, BN_CTX *ctx)
{
	if (!EC_POINT_add(group, result, a, b, ctx))
		errx(1, "EC_POINT_add");
}

static void
benchmark_ec_point_dbl(const EC_GROUP *group, EC_POINT *result,
    const BIGNUM *scalar, const EC_POINT *a, const EC_POINT *b, BN_CTX *ctx)
{
	if (!EC_POINT_dbl(group, result, a, ctx))
		errx(1, "EC_POINT_dbl");
}

static void
benchmark_ec_point_mul_generator(const EC_GROUP *group, EC_POINT *result,
    const BIGNUM *scalar, const EC_POINT *a, const EC_POINT *b, BN_CTX *ctx)
{
	if (!EC_POINT_mul(group, result, scalar, NULL, NULL, ctx))
		errx(1, "EC_POINT_mul");
}

struct benchmark {
	int curve;
	const char *desc;
	void (*func)(const EC_GROUP *, EC_POINT *, const BIGNUM *,
	    const EC_POINT *, const EC_POINT *, BN_CTX *);
};

static const struct benchmark benchmarks[] = {
	{
		.curve = NID_X9_62_prime256v1,
		.desc = "EC_POINT_add() p256",
		.func = benchmark_ec_point_add,
	},
	{
		.curve = NID_secp384r1,
		.desc = "EC_POINT_add() p384",
		.func = benchmark_ec_point_add,
	},
	{
		.curve = NID_secp521r1,
		.desc = "EC_POINT_add() p521",
		.func = benchmark_ec_point_add,
	},
	{
		.curve = NID_X9_62_prime256v1,
		.desc = "EC_POINT_dbl() p256",
		.func = benchmark_ec_point_dbl,
	},
	{
		.curve = NID_secp384r1,
		.desc = "EC_POINT_dbl() p384",
		.func = benchmark_ec_point_dbl,
	},
	{
		.curve = NID_secp521r1,
		.desc = "EC_POINT_dbl() p521",
		.func = benchmark_ec_point_dbl,
	},
	{
		.curve = NID_X9_62_prime256v1,
		.desc = "EC_POINT_mul() generator p256",
		.func = benchmark_ec_point_mul_generator,
	},
	{
		.curve = NID_secp384r1,
		.desc = "EC_POINT_mul() generator p384",
		.func = benchmark_ec_point_mul_generator,
	},
	{
		.curve = NID_secp521r1,
		.desc = "EC_POINT_mul() generator p521",
		.func = benchmark_ec_point_mul_generator,
	},
};

#define N_BENCHMARKS (sizeof(benchmarks) / sizeof(benchmarks[0]))

static volatile sig_atomic_t benchmark_stop;

static void
benchmark_sig_alarm(int sig)
{
	benchmark_stop = 1;
}

static void
benchmark_run(const struct benchmark *bm, int seconds)
{
	struct timespec start, end, duration;
	EC_GROUP *group = NULL;
	EC_POINT *a = NULL, *b = NULL, *result = NULL;
	BIGNUM *order = NULL, *scalar = NULL;
	BN_CTX *ctx = NULL;
	int i;

	signal(SIGALRM, benchmark_sig_alarm);

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	if ((group = EC_GROUP_new_by_curve_name(bm->curve)) == NULL)
		errx(1, "EC_GROUP_new_by_curve_name");
	if ((order = BN_new()) == NULL)
		errx(1, "BN_new");
	if (!EC_GROUP_get_order(group, order, ctx))
		errx(1, "EC_GROUP_get_order");

	if ((scalar = BN_new()) == NULL)
		errx(1, "BN_new");
	if (!BN_rand_range(scalar, order))
		errx(1, "BN_rand_range");
	if (!BN_set_bit(scalar, EC_GROUP_order_bits(group) - 1))
		errx(1, "BN_set_bit");

	if ((result = EC_POINT_new(group)) == NULL)
		errx(1, "EC_POINT_new");
	if ((a = EC_POINT_new(group)) == NULL)
		errx(1, "EC_POINT_new");
	if ((b = EC_POINT_new(group)) == NULL)
		errx(1, "EC_POINT_new");

	if (!EC_POINT_mul(group, a, scalar, NULL, NULL, ctx))
		errx(1, "EC_POINT_mul");
	if (!EC_POINT_mul(group, b, scalar, NULL, NULL, ctx))
		errx(1, "EC_POINT_mul");

	benchmark_stop = 0;
	i = 0;
	alarm(seconds);

	clock_gettime(CLOCK_MONOTONIC, &start);

	fprintf(stderr, "Benchmarking %s for %ds: ", bm->desc, seconds);
	while (!benchmark_stop) {
		bm->func(group, result, scalar, a, b, ctx);
		i++;
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	timespecsub(&end, &start, &duration);
	fprintf(stderr, "%d iterations in %f seconds\n", i,
	    duration.tv_sec + duration.tv_nsec / 1000000000.0);

	EC_GROUP_free(group);
	EC_POINT_free(result);
	EC_POINT_free(a);
	EC_POINT_free(b);
	BN_free(order);
	BN_free(scalar);
	BN_CTX_free(ctx);
}

static void
benchmark_ec_mul_single(void)
{
	const struct benchmark *bm;
	size_t i;

	for (i = 0; i < N_BENCHMARKS; i++) {
		bm = &benchmarks[i];
		benchmark_run(bm, 5);
	}
}

int
main(int argc, char **argv)
{
	int benchmark = 0, failed = 0;

	if (argc == 2 && strcmp(argv[1], "--benchmark") == 0)
		benchmark = 1;

	if (benchmark && !failed)
		benchmark_ec_mul_single();

	return failed;
}
