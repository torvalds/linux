/*	$OpenBSD: bn_isqrt.c,v 1.4 2023/08/03 18:53:56 tb Exp $ */
/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bn.h>

#include "bn_local.h"

#define N_TESTS		100

/* Sample squares between 2^128 and 2^4096. */
#define LOWER_BITS	128
#define UPPER_BITS	4096

extern const uint8_t is_square_mod_11[];
extern const uint8_t is_square_mod_63[];
extern const uint8_t is_square_mod_64[];
extern const uint8_t is_square_mod_65[];

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	if (len % 8)
		fprintf(stderr, "\n");
}

static const uint8_t *
get_table(int modulus)
{
	switch (modulus) {
	case 11:
		return is_square_mod_11;
	case 63:
		return is_square_mod_63;
	case 64:
		return is_square_mod_64;
	case 65:
		return is_square_mod_65;
	default:
		return NULL;
	}
}

static int
check_tables(int print)
{
	int fill[] = {11, 63, 64, 65};
	const uint8_t *table;
	uint8_t q[65];
	size_t i;
	int j;
	int failed = 0;

	for (i = 0; i < sizeof(fill) / sizeof(fill[0]); i++) {
		memset(q, 0, sizeof(q));

		for (j = 0; j < fill[i]; j++)
			q[(j * j) % fill[i]] = 1;

		if ((table = get_table(fill[i])) == NULL) {
			fprintf(stderr, "failed to get table %d\n", fill[i]);
			failed |= 1;
			continue;
		}

		if (memcmp(table, q, fill[i]) != 0) {
			fprintf(stderr, "table %d does not match:\n", fill[i]);
			fprintf(stderr, "want:\n");
			hexdump(table, fill[i]);
			fprintf(stderr, "got:\n");
			hexdump(q, fill[i]);
			failed |= 1;
			continue;
		}

		if (!print)
			continue;

		printf("const uint8_t is_square_mod_%d[] = {\n\t", fill[i]);
		for (j = 0; j < fill[i]; j++) {
			const char *end = " ";

			if (j % 16 == 15)
				end = "\n\t";
			if (j + 1 == fill[i])
				end = "";

			printf("%d,%s", q[j], end);
		}
		printf("\n};\nCTASSERT(sizeof(is_square_mod_%d) == %d);\n\n",
		    fill[i], fill[i]);
	}

	return failed;
}

static int
validate_tables(void)
{
	int fill[] = {11, 63, 64, 65};
	const uint8_t *table;
	size_t i;
	int j, k;
	int failed = 0;

	for (i = 0; i < sizeof(fill) / sizeof(fill[0]); i++) {
		if ((table = get_table(fill[i])) == NULL) {
			fprintf(stderr, "failed to get table %d\n", fill[i]);
			failed |= 1;
			continue;
		}

		for (j = 0; j < fill[i]; j++) {
			for (k = 0; k < fill[i]; k++) {
				if (j == (k * k) % fill[i])
					break;
			}

			if (table[j] == 0 && k < fill[i]) {
				fprintf(stderr, "%d == %d^2 (mod %d)", j, k,
				    fill[i]);
				failed |= 1;
			}
			if (table[j] == 1 && k == fill[i]) {
				fprintf(stderr, "%d not a square (mod %d)", j,
				    fill[i]);
				failed |= 1;
			}
		}
	}

	return failed;
}

/*
 * Choose a random number n of bit length between LOWER_BITS and UPPER_BITS and
 * check that n == isqrt(n^2). Random numbers n^2 <= testcase < (n + 1)^2 are
 * checked to have isqrt(testcase) == n.
 */
static int
isqrt_test(void)
{
	BN_CTX *ctx;
	BIGNUM *n, *n_sqr, *lower, *upper, *testcase, *isqrt;
	int cmp, i, is_perfect_square;
	int failed = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		errx(1, "BN_CTX_new");

	BN_CTX_start(ctx);

	if ((lower = BN_CTX_get(ctx)) == NULL)
		errx(1, "lower = BN_CTX_get(ctx)");
	if ((upper = BN_CTX_get(ctx)) == NULL)
		errx(1, "upper = BN_CTX_get(ctx)");
	if ((n = BN_CTX_get(ctx)) == NULL)
		errx(1, "n = BN_CTX_get(ctx)");
	if ((n_sqr = BN_CTX_get(ctx)) == NULL)
		errx(1, "n = BN_CTX_get(ctx)");
	if ((isqrt = BN_CTX_get(ctx)) == NULL)
		errx(1, "result = BN_CTX_get(ctx)");
	if ((testcase = BN_CTX_get(ctx)) == NULL)
		errx(1, "testcase = BN_CTX_get(ctx)");

	/* lower = 2^LOWER_BITS, upper = 2^UPPER_BITS. */
	if (!BN_set_bit(lower, LOWER_BITS))
		errx(1, "BN_set_bit(lower, %d)", LOWER_BITS);
	if (!BN_set_bit(upper, UPPER_BITS))
		errx(1, "BN_set_bit(upper, %d)", UPPER_BITS);

	if (!bn_rand_in_range(n, lower, upper))
		errx(1, "bn_rand_in_range n");

	/* n_sqr = n^2 */
	if (!BN_sqr(n_sqr, n, ctx))
		errx(1, "BN_sqr");

	if (!bn_isqrt(isqrt, &is_perfect_square, n_sqr, ctx))
		errx(1, "bn_isqrt n_sqr");

	if ((cmp = BN_cmp(n, isqrt)) != 0 || !is_perfect_square) {
		fprintf(stderr, "n = ");
		BN_print_fp(stderr, n);
		fprintf(stderr, "\nn^2 is_perfect_square: %d, cmp: %d\n",
		    is_perfect_square, cmp);
		failed = 1;
	}

	/* upper = 2 * n + 1 */
	if (!BN_lshift1(upper, n))
		errx(1, "BN_lshift1(upper, n)");
	if (!BN_add_word(upper, 1))
		errx(1, "BN_sub_word(upper, 1)");

	/* upper = (n + 1)^2 = n^2 + upper */
	if (!BN_add(upper, n_sqr, upper))
		errx(1, "BN_add");

	/*
	 * Check that isqrt((n + 1)^2) - 1 == n.
	 */

	if (!bn_isqrt(isqrt, &is_perfect_square, upper, ctx))
		errx(1, "bn_isqrt(upper)");

	if (!BN_sub_word(isqrt, 1))
		errx(1, "BN_add_word(isqrt, 1)");

	if ((cmp = BN_cmp(n, isqrt)) != 0 || !is_perfect_square) {
		fprintf(stderr, "n = ");
		BN_print_fp(stderr, n);
		fprintf(stderr, "\n(n + 1)^2 is_perfect_square: %d, cmp: %d\n",
		    is_perfect_square, cmp);
		failed = 1;
	}

	/*
	 * Test N_TESTS random numbers n^2 <= testcase < (n + 1)^2 and check
	 * that their isqrt is n.
	 */

	for (i = 0; i < N_TESTS; i++) {
		if (!bn_rand_in_range(testcase, n_sqr, upper))
			errx(1, "bn_rand_in_range testcase");

		if (!bn_isqrt(isqrt, &is_perfect_square, testcase, ctx))
			errx(1, "bn_isqrt testcase");

		if ((cmp = BN_cmp(n, isqrt)) != 0 ||
		    (is_perfect_square && BN_cmp(n_sqr, testcase) != 0)) {
			fprintf(stderr, "n = ");
			BN_print_fp(stderr, n);
			fprintf(stderr, "\ntestcase = ");
			BN_print_fp(stderr, testcase);
			fprintf(stderr,
			    "\ntestcase is_perfect_square: %d, cmp: %d\n",
			    is_perfect_square, cmp);
			failed = 1;
		}
	}

	/*
	 * Finally check that isqrt(n^2 - 1) + 1 == n.
	 */

	if (!BN_sub(testcase, n_sqr, BN_value_one()))
		errx(1, "BN_sub(testcase, n_sqr, 1)");

	if (!bn_isqrt(isqrt, &is_perfect_square, testcase, ctx))
		errx(1, "bn_isqrt(n_sqr - 1)");

	if (!BN_add_word(isqrt, 1))
		errx(1, "BN_add_word(isqrt, 1)");

	if ((cmp = BN_cmp(n, isqrt)) != 0 || is_perfect_square) {
		fprintf(stderr, "n = ");
		BN_print_fp(stderr, n);
		fprintf(stderr, "\nn_sqr - 1 is_perfect_square: %d, cmp: %d\n",
		    is_perfect_square, cmp);
		failed = 1;
	}

	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return failed;
}

static void
usage(void)
{
	fprintf(stderr, "usage: bn_isqrt [-C]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	size_t i;
	int ch;
	int failed = 0, print = 0;

	while ((ch = getopt(argc, argv, "C")) != -1) {
		switch (ch) {
		case 'C':
			print = 1;
			break;
		default:
			usage();
			break;
		}
	}

	if (print)
		return check_tables(1);

	for (i = 0; i < N_TESTS; i++)
		failed |= isqrt_test();

	failed |= check_tables(0);
	failed |= validate_tables();

	return failed;
}
