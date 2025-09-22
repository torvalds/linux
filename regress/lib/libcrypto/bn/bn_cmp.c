/*	$OpenBSD: bn_cmp.c,v 1.2 2023/06/21 07:16:08 jsing Exp $ */
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

#include <stdio.h>

#include <openssl/bn.h>

struct bn_cmp_test {
	const char *a;
	const char *b;
	int cmp;
	int ucmp;
};

struct bn_cmp_test bn_cmp_tests[] = {
	{
		.a = "0",
		.b = "0",
		.cmp = 0,
		.ucmp = 0,
	},
	{
		.a = "-1",
		.b = "0",
		.cmp = -1,
		.ucmp = 1,
	},
	{
		.a = "1ffffffffffffffff",
		.b = "1ffffffffffffffff",
		.cmp = 0,
		.ucmp = 0,
	},
	{
		.a = "1fffffffffffffffe",
		.b = "1ffffffffffffffff",
		.cmp = -1,
		.ucmp = -1,
	},
	{
		.a = "1ffffffffffffffff",
		.b = "1fffffffffffffffe",
		.cmp = 1,
		.ucmp = 1,
	},
	{
		.a = "0",
		.b = "1ffffffffffffffff",
		.cmp = -1,
		.ucmp = -1,
	},
	{
		.a = "1ffffffffffffffff",
		.b = "0",
		.cmp = 1,
		.ucmp = 1,
	},
	{
		.a = "-1ffffffffffffffff",
		.b = "0",
		.cmp = -1,
		.ucmp = 1,
	},
	{
		.a = "1ffffffffffffffff",
		.b = "00000000000000001ffffffffffffffff",
		.cmp = 0,
		.ucmp = 0,
	},
	{
		.a = "-1ffffffffffffffff",
		.b = "-00000000000000001ffffffffffffffff",
		.cmp = 0,
		.ucmp = 0,
	},
	{
		.a = "1ffffffffffffffff",
		.b = "-00000000000000001ffffffffffffffff",
		.cmp = 1,
		.ucmp = 0,
	},
	{
		.a = "-1ffffffffffffffff",
		.b = "00000000000000001ffffffffffffffff",
		.cmp = -1,
		.ucmp = 0,
	},
};

#define N_BN_CMP_TESTS \
    (sizeof(bn_cmp_tests) / sizeof(*bn_cmp_tests))

static int
test_bn_cmp(void)
{
	struct bn_cmp_test *bct;
	BIGNUM *a = NULL, *b = NULL;
	size_t i;
	int ret;
	int failed = 1;

	if ((a = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN\n");
		goto failure;
	}
	if ((b = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN\n");
		goto failure;
	}

	for (i = 0; i < N_BN_CMP_TESTS; i++) {
		bct = &bn_cmp_tests[i];

		if (!BN_hex2bn(&a, bct->a)) {
			fprintf(stderr, "FAIL: failed to set a from hex\n");
			goto failure;
		}
		if (!BN_hex2bn(&b, bct->b)) {
			fprintf(stderr, "FAIL: failed to set b from hex\n");
			goto failure;
		}

		if ((ret = BN_cmp(a, b)) != bct->cmp) {
			fprintf(stderr, "FAIL: BN_cmp(%s, %s) = %d, want %d\n",
			    bct->a, bct->b, ret, bct->cmp);
			goto failure;
		}
		if ((ret = BN_ucmp(a, b)) != bct->ucmp) {
			fprintf(stderr, "FAIL: BN_ucmp(%s, %s) = %d, want %d\n",
			    bct->a, bct->b, ret, bct->ucmp);
			goto failure;
		}
	}

	failed = 0;

 failure:
	BN_free(a);
	BN_free(b);

	return failed;
}

static int
test_bn_cmp_null(void)
{
	BIGNUM *a = NULL;
	int ret;
	int failed = 1;

	if ((a = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN\n");
		goto failure;
	}

	/*
	 * Comparison to NULL.
	 */
	if ((ret = BN_cmp(NULL, NULL)) != 0) {
		fprintf(stderr, "FAIL: BN_cmp(NULL, NULL) == %d, want 0\n", ret);
		goto failure;
	}

	if ((ret = BN_cmp(a, NULL)) != -1) {
		fprintf(stderr, "FAIL: BN_cmp(0, NULL) == %d, want -1\n", ret);
		goto failure;
	}
	if ((ret = BN_cmp(NULL, a)) != 1) {
		fprintf(stderr, "FAIL: BN_cmp(NULL, 0) == %d, want 1\n", ret);
		goto failure;
	}

	if (!BN_set_word(a, 1)) {
		fprintf(stderr, "FAIL: failed to set BN to 1\n");
		goto failure;
	}
	if ((ret = BN_cmp(a, NULL)) != -1) {
		fprintf(stderr, "FAIL: BN_cmp(1, NULL) == %d, want -1\n", ret);
		goto failure;
	}
	if ((ret = BN_cmp(NULL, a)) != 1) {
		fprintf(stderr, "FAIL: BN_cmp(NULL, 1) == %d, want 1\n", ret);
		goto failure;
	}

	BN_set_negative(a, 1);
	if ((ret = BN_cmp(a, NULL)) != -1) {
		fprintf(stderr, "FAIL: BN_cmp(-1, NULL) == %d, want -1\n", ret);
		goto failure;
	}
	if ((ret = BN_cmp(NULL, a)) != 1) {
		fprintf(stderr, "FAIL: BN_cmp(NULL, -1) == %d, want 1\n", ret);
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(a);

	return failed;
}

struct bn_cmp_word_test {
	int a;
	int b;
	int cmp;
	int ucmp;
};

struct bn_cmp_word_test bn_cmp_word_tests[] = {
	{
		.a = -1,
		.b = -1,
		.cmp = 0,
		.ucmp = 0,
	},
	{
		.a = 0,
		.b = 0,
		.cmp = 0,
		.ucmp = 0,
	},
	{
		.a = 1,
		.b = 1,
		.cmp = 0,
		.ucmp = 0,
	},
	{
		.a = 0,
		.b = 1,
		.cmp = -1,
		.ucmp = -1,
	},
	{
		.a = 1,
		.b = 0,
		.cmp = 1,
		.ucmp = 1,
	},
	{
		.a = -1,
		.b = 0,
		.cmp = -1,
		.ucmp = 1,
	},
	{
		.a = 0,
		.b = -1,
		.cmp = 1,
		.ucmp = -1,
	},
	{
		.a = -1,
		.b = 1,
		.cmp = -1,
		.ucmp = 0,
	},
	{
		.a = 1,
		.b = -1,
		.cmp = 1,
		.ucmp = 0,
	},
};

#define N_BN_CMP_WORD_TESTS \
    (sizeof(bn_cmp_word_tests) / sizeof(*bn_cmp_word_tests))

static int
test_bn_cmp_word(void)
{
	struct bn_cmp_word_test *bcwt;
	BIGNUM *a = NULL, *b = NULL;
	BN_ULONG v;
	size_t i;
	int ret;
	int failed = 1;

	if ((a = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN\n");
		goto failure;
	}
	if ((b = BN_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create BN\n");
		goto failure;
	}

	for (i = 0; i < N_BN_CMP_WORD_TESTS; i++) {
		bcwt = &bn_cmp_word_tests[i];

		if (bcwt->a >= 0) {
			v = bcwt->a;
		} else {
			v = 0 - bcwt->a;
		}
		if (!BN_set_word(a, v)) {
			fprintf(stderr, "FAIL: failed to set a\n");
			goto failure;
		}
		BN_set_negative(a, (bcwt->a < 0));

		if (bcwt->b >= 0) {
			v = bcwt->b;
		} else {
			v = 0 - bcwt->b;
		}
		if (!BN_set_word(b, v)) {
			fprintf(stderr, "FAIL: failed to set b\n");
			goto failure;
		}
		BN_set_negative(b, (bcwt->b < 0));

		if ((ret = BN_cmp(a, b)) != bcwt->cmp) {
			fprintf(stderr, "FAIL: BN_cmp(%d, %d) = %d, want %d\n",
			    bcwt->a, bcwt->b, ret, bcwt->cmp);
			goto failure;
		}
		if ((ret = BN_ucmp(a, b)) != bcwt->ucmp) {
			fprintf(stderr, "FAIL: BN_ucmp(%d, %d) = %d, want %d\n",
			    bcwt->a, bcwt->b, ret, bcwt->ucmp);
			goto failure;
		}
	}

	failed = 0;

 failure:
	BN_free(a);
	BN_free(b);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_bn_cmp();
	failed |= test_bn_cmp_null();
	failed |= test_bn_cmp_word();

	return failed;
}
