/*	$OpenBSD: bn_shift.c,v 1.9 2023/03/11 14:02:26 jsing Exp $ */
/*
 * Copyright (c) 2022 Joel Sing <jsing@openbsd.org>
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

static const char *bn_shift_want_hex = \
    "02AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" \
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA8";

static int
check_shift_result(BIGNUM *bn1)
{
	BIGNUM *bn2 = NULL;
	char *s = NULL;
	int ret = 0;

	if (!BN_hex2bn(&bn2, bn_shift_want_hex)) {
		fprintf(stderr, "FAIL: BN_hex2bn() failed\n");
		goto failure;
	}
	if (BN_cmp(bn1, bn2) != 0) {
		fprintf(stderr, "FAIL: shifted result differs\n");
		if ((s = BN_bn2hex(bn1)) == NULL) {
			fprintf(stderr, "FAIL: BN_bn2hex()\n");
			goto failure;
		}
		fprintf(stderr, "Got:  %s\n", s);
		free(s);
		if ((s = BN_bn2hex(bn2)) == NULL) {
			fprintf(stderr, "FAIL: BN_bn2hex()\n");
			goto failure;
		}
		fprintf(stderr, "Want: %s\n", s);
	}

	ret = 1;

 failure:
	BN_free(bn2);
	free(s);

	return ret;
}

static int
test_bn_shift1(void)
{
	BIGNUM *bn1 = NULL, *bn2 = NULL;
	int i;
	int failed = 1;

	if ((bn1 = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN\n");
		goto failure;
	}
	if ((bn2 = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN\n");
		goto failure;
	}

	for (i = 1; i <= 256; i++) {
		if (!BN_set_bit(bn1, 1)) {
			fprintf(stderr, "FAIL: failed to set bit\n");
			goto failure;
		}
		if (!BN_lshift1(bn1, bn1)) {
			fprintf(stderr, "FAIL: failed to BN_lshift1()\n");
			goto failure;
		}
		if (!BN_lshift1(bn1, bn1)) {
			fprintf(stderr, "FAIL: failed to BN_lshift1()\n");
			goto failure;
		}
		if (!BN_rshift1(bn1, bn1)) {
			fprintf(stderr, "FAIL: failed to BN_rshift1()\n");
			goto failure;
		}
		if (!BN_lshift1(bn1, bn1)) {
			fprintf(stderr, "FAIL: failed to BN_lshift1()\n");
			goto failure;
		}
	}

	if (!check_shift_result(bn1))
		goto failure;

	/*
	 * Shift result into a different BN.
	 */
	if (!BN_lshift1(bn1, bn1)) {
		fprintf(stderr, "FAIL: failed to BN_lshift1()\n");
		goto failure;
	}
	if (!BN_rshift1(bn2, bn1)) {
		fprintf(stderr, "FAIL: failed to BN_rshift1()\n");
		goto failure;
	}

	if (!check_shift_result(bn2))
		goto failure;

	if (!BN_rshift1(bn2, bn2)) {
		fprintf(stderr, "FAIL: failed to BN_rshift1()\n");
		goto failure;
	}
	if (!BN_lshift1(bn1, bn2)) {
		fprintf(stderr, "FAIL: failed to BN_lshift1()\n");
		goto failure;
	}

	if (!check_shift_result(bn1))
		goto failure;

	failed = 0;

 failure:
	BN_free(bn1);
	BN_free(bn2);

	return failed;
}

static int
test_bn_shift(void)
{
	BIGNUM *bn1 = NULL, *bn2 = NULL;
	int i;
	int failed = 1;

	if ((bn1 = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN 1\n");
		goto failure;
	}
	if ((bn2 = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN 2\n");
		goto failure;
	}

	for (i = 1; i <= 256; i++) {
		if (!BN_set_bit(bn1, 1)) {
			fprintf(stderr, "FAIL: failed to set bit\n");
			goto failure;
		}
		if (!BN_lshift(bn1, bn1, i + 1)) {
			fprintf(stderr, "FAIL: failed to BN_lshift()\n");
			goto failure;
		}
		if (!BN_rshift(bn1, bn1, i - 1)) {
			fprintf(stderr, "FAIL: failed to BN_rshift()\n");
			goto failure;
		}
	}

	if (!check_shift_result(bn1))
		goto failure;

	for (i = 0; i <= 256; i++) {
		if (!BN_lshift(bn1, bn1, i)) {
			fprintf(stderr, "FAIL: failed to BN_lshift()\n");
			goto failure;
		}
		if (i > 1) {
			if (!BN_set_bit(bn1, 1)) {
				fprintf(stderr, "FAIL: failed to set bit\n");
				goto failure;
			}
		}
	}

	if (BN_num_bytes(bn1) != 4177) {
		fprintf(stderr, "FAIL: BN has %d bytes, want 4177\n",
		    BN_num_bytes(bn1));
		goto failure;
	}

	for (i = 0; i <= 256; i++) {
		if (!BN_rshift(bn1, bn1, i)) {
			fprintf(stderr, "FAIL: failed to BN_rshift()\n");
			goto failure;
		}
	}

	if (!check_shift_result(bn1))
		goto failure;

	/*
	 * Shift result into a different BN.
	 */
	if (!BN_lshift(bn1, bn1, BN_BITS2 + 1)) {
		fprintf(stderr, "FAIL: failed to BN_lshift()\n");
		goto failure;
	}
	if (!BN_rshift(bn2, bn1, BN_BITS2 + 1)) {
		fprintf(stderr, "FAIL: failed to BN_rshift()\n");
		goto failure;
	}

	if (!check_shift_result(bn2))
		goto failure;

	if (!BN_rshift(bn2, bn2, 3)) {
		fprintf(stderr, "FAIL: failed to BN_rshift()\n");
		goto failure;
	}
	if (!BN_lshift(bn1, bn2, 3)) {
		fprintf(stderr, "FAIL: failed to BN_lshift()\n");
		goto failure;
	}

	if (!check_shift_result(bn1))
		goto failure;

	/*
	 * Shift of zero (equivalent to a copy).
	 */
	BN_zero(bn2);
	if (!BN_lshift(bn2, bn1, 0)) {
		fprintf(stderr, "FAIL: failed to BN_lshift()\n");
		goto failure;
	}

	if (!check_shift_result(bn2))
		goto failure;

	if (!BN_lshift(bn2, bn2, 0)) {
		fprintf(stderr, "FAIL: failed to BN_lshift()\n");
		goto failure;
	}

	if (!check_shift_result(bn2))
		goto failure;

	BN_zero(bn2);
	if (!BN_rshift(bn2, bn1, 0)) {
		fprintf(stderr, "FAIL: failed to BN_rshift()\n");
		goto failure;
	}

	if (!check_shift_result(bn2))
		goto failure;

	if (!BN_rshift(bn2, bn2, 0)) {
		fprintf(stderr, "FAIL: failed to BN_rshift()\n");
		goto failure;
	}

	if (!check_shift_result(bn2))
		goto failure;

	failed = 0;

 failure:
	BN_free(bn1);
	BN_free(bn2);

	return failed;
}

static int
test_bn_rshift_to_zero(void)
{
	BIGNUM *bn1 = NULL, *bn2 = NULL;
	int failed = 1;

	if (!BN_hex2bn(&bn1, "ffff")) {
		fprintf(stderr, "FAIL: BN_hex2bn() failed\n");
		goto failure;
	}
	if (!BN_lshift(bn1, bn1, BN_BITS2)) {
		fprintf(stderr, "FAIL: BN_lshift() failed\n");
		goto failure;
	}

	if ((bn2 = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: BN_new() failed\n");
		goto failure;
	}

	/* Shift all words. */
	if (!BN_rshift(bn2, bn1, BN_BITS2 * 2)) {
		fprintf(stderr, "FAIL: BN_rshift() failed\n");
		goto failure;
	}
	if (BN_is_zero(bn1)) {
		fprintf(stderr, "FAIL: BN is zero\n");
		goto failure;
	}
	if (!BN_is_zero(bn2)) {
		fprintf(stderr, "FAIL: BN is not zero\n");
		goto failure;
	}

	/* Shift to zero, with partial shift for top most word. */
	if (!BN_rshift(bn2, bn1, BN_BITS2 + 16)) {
		fprintf(stderr, "FAIL: BN_rshift() failed\n");
		goto failure;
	}
	if (BN_is_zero(bn1)) {
		fprintf(stderr, "FAIL: BN is zero\n");
		goto failure;
	}
	if (!BN_is_zero(bn2)) {
		fprintf(stderr, "FAIL: BN is not zero\n");
		goto failure;
	}

	/* Shift to zero of negative value. */
	if (!BN_one(bn1)) {
		fprintf(stderr, "FAIL: BN_one() failed\n");
		goto failure;
	}
	BN_set_negative(bn1, 1);
	if (!BN_rshift(bn1, bn1, 1)) {
		fprintf(stderr, "FAIL: BN_rshift() failed\n");
		goto failure;
	}
	if (!BN_is_zero(bn1)) {
		fprintf(stderr, "FAIL: BN is not zero\n");
		goto failure;
	}
	if (BN_is_negative(bn1)) {
		fprintf(stderr, "FAIL: BN is negative zero\n");
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(bn1);
	BN_free(bn2);

	return failed;
}

static void
benchmark_bn_lshift1(BIGNUM *bn)
{
	int i;

	if (!BN_set_bit(bn, 8192))
		errx(1, "BN_set_bit");

	if (!BN_one(bn))
		errx(1, "BN_one");

	for (i = 0; i < 8192; i++) {
		if (!BN_lshift1(bn, bn))
			errx(1, "BN_lshift1");
	}
}

static void
benchmark_bn_lshift(BIGNUM *bn, int n)
{
	int i;

	if (!BN_set_bit(bn, 8192 * n))
		errx(1, "BN_set_bit");

	if (!BN_one(bn))
		errx(1, "BN_one");

	for (i = 0; i < 8192; i++) {
		if (!BN_lshift(bn, bn, n))
			errx(1, "BN_lshift");
	}
}

static void
benchmark_bn_lshift_1(BIGNUM *bn)
{
	benchmark_bn_lshift(bn, 1);
}

static void
benchmark_bn_lshift_16(BIGNUM *bn)
{
	benchmark_bn_lshift(bn, 16);
}

static void
benchmark_bn_lshift_32(BIGNUM *bn)
{
	benchmark_bn_lshift(bn, 32);
}

static void
benchmark_bn_lshift_64(BIGNUM *bn)
{
	benchmark_bn_lshift(bn, 64);
}

static void
benchmark_bn_lshift_65(BIGNUM *bn)
{
	benchmark_bn_lshift(bn, 65);
}

static void
benchmark_bn_lshift_80(BIGNUM *bn)
{
	benchmark_bn_lshift(bn, 80);
}

static void
benchmark_bn_lshift_127(BIGNUM *bn)
{
	benchmark_bn_lshift(bn, 127);
}

static void
benchmark_bn_rshift1(BIGNUM *bn)
{
	int i;

	if (!BN_one(bn))
		errx(1, "BN_one");

	if (!BN_set_bit(bn, 8192))
		errx(1, "BN_set_bit");

	for (i = 0; i < 8192; i++) {
		if (!BN_rshift1(bn, bn))
			errx(1, "BN_rshift1");
	}
}

static void
benchmark_bn_rshift(BIGNUM *bn, int n)
{
	int i;

	if (!BN_one(bn))
		errx(1, "BN_one");

	if (!BN_set_bit(bn, 8192 * n))
		errx(1, "BN_set_bit");

	for (i = 0; i < 8192; i++) {
		if (!BN_rshift(bn, bn, n))
			errx(1, "BN_rshift");
	}
}

static void
benchmark_bn_rshift_1(BIGNUM *bn)
{
	benchmark_bn_rshift(bn, 1);
}

static void
benchmark_bn_rshift_16(BIGNUM *bn)
{
	benchmark_bn_rshift(bn, 16);
}

static void
benchmark_bn_rshift_32(BIGNUM *bn)
{
	benchmark_bn_rshift(bn, 32);
}

static void
benchmark_bn_rshift_64(BIGNUM *bn)
{
	benchmark_bn_rshift(bn, 64);
}

static void
benchmark_bn_rshift_65(BIGNUM *bn)
{
	benchmark_bn_rshift(bn, 65);
}

static void
benchmark_bn_rshift_80(BIGNUM *bn)
{
	benchmark_bn_rshift(bn, 80);
}

static void
benchmark_bn_rshift_127(BIGNUM *bn)
{
	benchmark_bn_rshift(bn, 127);
}

struct benchmark {
	const char *desc;
	void (*func)(BIGNUM *);
};

static const struct benchmark benchmarks[] = {
	{
		.desc = "BN_lshift1()",
		.func = benchmark_bn_lshift1,
	},
	{
		.desc = "BN_lshift(_, _, 1)",
		.func = benchmark_bn_lshift_1,
	},
	{
		.desc = "BN_lshift(_, _, 16)",
		.func = benchmark_bn_lshift_16,
	},
	{
		.desc = "BN_lshift(_, _, 32)",
		.func = benchmark_bn_lshift_32,
	},
	{
		.desc = "BN_lshift(_, _, 64)",
		.func = benchmark_bn_lshift_64,
	},
	{
		.desc = "BN_lshift(_, _, 65)",
		.func = benchmark_bn_lshift_65,
	},
	{
		.desc = "BN_lshift(_, _, 80)",
		.func = benchmark_bn_lshift_80,
	},
	{
		.desc = "BN_lshift(_, _, 127)",
		.func = benchmark_bn_lshift_127,
	},
	{
		.desc = "BN_rshift1()",
		.func = benchmark_bn_rshift1,
	},
	{
		.desc = "BN_rshift(_, _, 1)",
		.func = benchmark_bn_rshift_1,
	},
	{
		.desc = "BN_rshift(_, _, 16)",
		.func = benchmark_bn_rshift_16,
	},
	{
		.desc = "BN_rshift(_, _, 32)",
		.func = benchmark_bn_rshift_32,
	},
	{
		.desc = "BN_rshift(_, _, 64)",
		.func = benchmark_bn_rshift_64,
	},
	{
		.desc = "BN_rshift(_, _, 65)",
		.func = benchmark_bn_rshift_65,
	},
	{
		.desc = "BN_rshift(_, _, 80)",
		.func = benchmark_bn_rshift_80,
	},
	{
		.desc = "BN_rshift(_, _, 127)",
		.func = benchmark_bn_rshift_127,
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
	BIGNUM *bn;
	int i;

	signal(SIGALRM, benchmark_sig_alarm);

	if ((bn = BN_new()) == NULL)
		errx(1, "BN_new");

	benchmark_stop = 0;
	i = 0;
	alarm(seconds);

	clock_gettime(CLOCK_MONOTONIC, &start);

	fprintf(stderr, "Benchmarking %s for %ds: ", bm->desc, seconds);
	while (!benchmark_stop) {
		bm->func(bn);
		i++;
	}
	clock_gettime(CLOCK_MONOTONIC, &end);
	timespecsub(&end, &start, &duration);
	fprintf(stderr, "%d iterations in %f seconds\n", i,
	    duration.tv_sec + duration.tv_nsec / 1000000000.0);

	BN_free(bn);
}

static void
benchmark_bn_shift(void)
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

	failed |= test_bn_shift1();
	failed |= test_bn_shift();
	failed |= test_bn_rshift_to_zero();

	if (benchmark && !failed)
		benchmark_bn_shift();

	return failed;
}
