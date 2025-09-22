/* $OpenBSD: bn_word.c,v 1.2 2024/08/23 12:56:26 anton Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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
#include <string.h>

#include <openssl/bn.h>

struct bn_word_test {
	const char *in_hex;
	BN_ULONG in_word;
	BN_ULONG mod_word;
	BN_ULONG out_word;
	const char *out_hex;
	int out_is_negative;
};

static int
check_bn_word_test(const char *op_name, const BIGNUM *bn,
    const struct bn_word_test *bwt)
{
	char *out_hex = NULL;
	BN_ULONG out_word;
	int failed = 1;

	if ((out_word = BN_get_word(bn)) != bwt->out_word) {
		fprintf(stderr, "FAIL %s: Got word %lx, want %lx\n",
		    op_name, (unsigned long)out_word,
		    (unsigned long)bwt->out_word);
		goto failure;
	}

	if (BN_is_negative(bn) != bwt->out_is_negative) {
		fprintf(stderr, "FAIL %s: Got is negative %d, want %d\n",
		    op_name, BN_is_negative(bn), bwt->out_is_negative);
		goto failure;
	}

	if ((out_hex = BN_bn2hex(bn)) == NULL)
		errx(1, "BN_bn2hex() failed");

	if (strcmp(out_hex, bwt->out_hex) != 0) {
		fprintf(stderr, "FAIL %s: Got hex %s, want %s\n",
		    op_name, out_hex, bwt->out_hex);
		goto failure;
	}

	if (BN_is_zero(bn) && BN_is_negative(bn) != 0) {
		fprintf(stderr, "FAIL %s: Got negative zero\n", op_name);
		goto failure;
	}

	failed = 0;

 failure:
	free(out_hex);

	return failed;
}

static int
test_bn_word(int (*bn_word_op)(BIGNUM *, BN_ULONG), const char *op_name,
    const struct bn_word_test *bwts, size_t num_tests)
{
	const struct bn_word_test *bwt;
	BIGNUM *bn;
	size_t i;
	int failed = 0;

	if ((bn = BN_new()) == NULL)
		errx(1, "BN_new() failed");

	for (i = 0; i < num_tests; i++) {
		bwt = &bwts[i];

		if (!BN_hex2bn(&bn, bwt->in_hex)) {
			fprintf(stderr, "FAIL: BN_hex2bn(\"%s\") failed\n",
			    bwt->in_hex);
			failed = 1;
			continue;
		}

		if (!bn_word_op(bn, bwt->in_word)) {
			fprintf(stderr, "FAIL: %s(%lx) failed\n", op_name,
			     (unsigned long)bwt->in_word);
			failed = 1;
			continue;
		}

		failed |= check_bn_word_test(op_name, bn, bwt);
	}

	BN_free(bn);

	return failed;
}

static const struct bn_word_test bn_add_word_tests[] = {
	{
		.in_hex = "1",
		.in_word = 0,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "0",
		.in_word = 1,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "1",
		.in_word = 1,
		.out_word = 2,
		.out_hex = "02",
	},
	{
		.in_hex = "-1",
		.in_word = 2,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "-1",
		.in_word = 1,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "-3",
		.in_word = 2,
		.out_word = 1,
		.out_hex = "-01",
		.out_is_negative = 1,
	},
	{
		.in_hex = "1",
		.in_word = 0xfffffffeUL,
		.out_word = 0xffffffffUL,
		.out_hex = "FFFFFFFF",
	},
	{
		.in_hex = "FFFFFFFFFFFFFFFF",
		.in_word = 1,
		.out_word = BN_MASK2,
		.out_hex = "010000000000000000",
	},
};

#define N_BN_ADD_WORD_TESTS \
    (sizeof(bn_add_word_tests) / sizeof(bn_add_word_tests[0]))

static int
test_bn_add_word(void)
{
	return test_bn_word(BN_add_word, "BN_add_word", bn_add_word_tests,
	    N_BN_ADD_WORD_TESTS);
}

static const struct bn_word_test bn_sub_word_tests[] = {
	{
		.in_hex = "1",
		.in_word = 0,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "0",
		.in_word = 1,
		.out_word = 1,
		.out_hex = "-01",
		.out_is_negative = 1,
	},
	{
		.in_hex = "1",
		.in_word = 1,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "2",
		.in_word = 1,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "-1",
		.in_word = 2,
		.out_word = 3,
		.out_hex = "-03",
		.out_is_negative = 1,
	},
	{
		.in_hex = "1",
		.in_word = 1,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "3",
		.in_word = 2,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "-3",
		.in_word = 2,
		.out_word = 5,
		.out_hex = "-05",
		.out_is_negative = 1,
	},
	{
		.in_hex = "-1",
		.in_word = 0xfffffffeUL,
		.out_word = 0xffffffffUL,
		.out_hex = "-FFFFFFFF",
		.out_is_negative = 1,
	},
	{
		.in_hex = "010000000000000000",
		.in_word = 1,
		.out_word = BN_MASK2,
		.out_hex = "FFFFFFFFFFFFFFFF",
	},
};

#define N_BN_SUB_WORD_TESTS \
    (sizeof(bn_sub_word_tests) / sizeof(bn_sub_word_tests[0]))

static int
test_bn_sub_word(void)
{
	return test_bn_word(BN_sub_word, "BN_sub_word", bn_sub_word_tests,
	    N_BN_SUB_WORD_TESTS);
}

static const struct bn_word_test bn_mul_word_tests[] = {
	{
		.in_hex = "1",
		.in_word = 0,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "0",
		.in_word = 1,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "1",
		.in_word = 1,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "-1",
		.in_word = 0,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "-1",
		.in_word = 1,
		.out_word = 1,
		.out_hex = "-01",
		.out_is_negative = 1,
	},
	{
		.in_hex = "-3",
		.in_word = 2,
		.out_word = 6,
		.out_hex = "-06",
		.out_is_negative = 1,
	},
	{
		.in_hex = "1",
		.in_word = 0xfffffffeUL,
		.out_word = 0xfffffffeUL,
		.out_hex = "FFFFFFFE",
	},
	{
		.in_hex = "010000000000000000",
		.in_word = 2,
		.out_word = BN_MASK2,
		.out_hex = "020000000000000000",
	},
};

#define N_BN_MUL_WORD_TESTS \
    (sizeof(bn_mul_word_tests) / sizeof(bn_mul_word_tests[0]))

static int
test_bn_mul_word(void)
{
	return test_bn_word(BN_mul_word, "BN_mul_word", bn_mul_word_tests,
	    N_BN_MUL_WORD_TESTS);
}

static const struct bn_word_test bn_div_word_tests[] = {
	{
		.in_hex = "1",
		.in_word = 0,
		.mod_word = BN_MASK2,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "0",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "4",
		.in_word = 2,
		.mod_word = 0,
		.out_word = 2,
		.out_hex = "02",
	},
	{
		.in_hex = "7",
		.in_word = 3,
		.mod_word = 1,
		.out_word = 2,
		.out_hex = "02",
	},
	{
		.in_hex = "1",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "-2",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 2,
		.out_hex = "-02",
		.out_is_negative = 1,
	},
	{
		.in_hex = "-1",
		.in_word = 2,
		.mod_word = 1,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "-3",
		.in_word = 2,
		.mod_word = 1,
		.out_word = 1,
		.out_hex = "-01",
		.out_is_negative = 1,
	},
	{
		.in_hex = "1",
		.in_word = 0xffffffffUL,
		.mod_word = 1,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "FFFFFFFF",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 0xffffffffUL,
		.out_hex = "FFFFFFFF",
	},
	{
		.in_hex = "FFFFFFFE",
		.in_word = 0xffffffffUL,
		.mod_word = 0xfffffffeUL,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "FFFFFFFFFFFFFFFF",
		.in_word = 1,
		.mod_word = 0,
		.out_word = BN_MASK2,
		.out_hex = "FFFFFFFFFFFFFFFF",
	},
	{
		.in_hex = "FFFFFFFF",
		.in_word = 0xff,
		.mod_word = 0,
		.out_word = 0x1010101UL,
		.out_hex = "01010101",
	},
	{
		.in_hex = "FFFFFFFF",
		.in_word = 0x10,
		.mod_word = 0xf,
		.out_word = 0xfffffffUL,
		.out_hex = "0FFFFFFF",
	},
};

#define N_BN_DIV_WORD_TESTS \
    (sizeof(bn_div_word_tests) / sizeof(bn_div_word_tests[0]))

static int
test_bn_div_word(void)
{
	const char *op_name = "BN_div_word";
	const struct bn_word_test *bwt;
	BN_ULONG mod_word;
	BIGNUM *bn;
	size_t i;
	int failed = 0;

	if ((bn = BN_new()) == NULL)
		errx(1, "BN_new() failed");

	for (i = 0; i < N_BN_DIV_WORD_TESTS; i++) {
		bwt = &bn_div_word_tests[i];

		if (!BN_hex2bn(&bn, bwt->in_hex)) {
			fprintf(stderr, "FAIL: BN_hex2bn(\"%s\") failed\n",
			    bwt->in_hex);
			failed = 1;
			continue;
		}

		if ((mod_word = BN_div_word(bn, bwt->in_word)) != bwt->mod_word) {
			fprintf(stderr, "FAIL %s: Got mod word %lx, want %lx\n",
			    op_name, (unsigned long)mod_word,
			    (unsigned long)bwt->mod_word);
			failed = 1;
			continue;
		}

		failed |= check_bn_word_test(op_name, bn, bwt);
	}

	BN_free(bn);

	return failed;
}

static const struct bn_word_test bn_mod_word_tests[] = {
	{
		.in_hex = "1",
		.in_word = 0,
		.mod_word = BN_MASK2,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "0",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 0,
		.out_hex = "0",
	},
	{
		.in_hex = "4",
		.in_word = 2,
		.mod_word = 0,
		.out_word = 4,
		.out_hex = "04",
	},
	{
		.in_hex = "7",
		.in_word = 3,
		.mod_word = 1,
		.out_word = 7,
		.out_hex = "07",
	},
	{
		.in_hex = "1",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "-2",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 2,
		.out_hex = "-02",
		.out_is_negative = 1,
	},
	{
		.in_hex = "-1",
		.in_word = 2,
		.mod_word = 1,
		.out_word = 1,
		.out_hex = "-01",
		.out_is_negative = 1,
	},
	{
		.in_hex = "-3",
		.in_word = 2,
		.mod_word = 1,
		.out_word = 3,
		.out_hex = "-03",
		.out_is_negative = 1,
	},
	{
		.in_hex = "1",
		.in_word = 0xffffffffUL,
		.mod_word = 1,
		.out_word = 1,
		.out_hex = "01",
	},
	{
		.in_hex = "FFFFFFFF",
		.in_word = 1,
		.mod_word = 0,
		.out_word = 0xffffffffUL,
		.out_hex = "FFFFFFFF",
	},
	{
		.in_hex = "FFFFFFFE",
		.in_word = 0xffffffffUL,
		.mod_word = 0xfffffffeUL,
		.out_word = 0xfffffffeUL,
		.out_hex = "FFFFFFFE",
	},
	{
		.in_hex = "FFFFFFFFFFFFFFFF",
		.in_word = 1,
		.mod_word = 0,
		.out_word = BN_MASK2,
		.out_hex = "FFFFFFFFFFFFFFFF",
	},
	{
		.in_hex = "FFFFFFFF",
		.in_word = 0xff,
		.mod_word = 0,
		.out_word = 0xffffffff,
		.out_hex = "FFFFFFFF",
	},
	{
		.in_hex = "FFFFFFFF",
		.in_word = 0x10,
		.mod_word = 0xf,
		.out_word = 0xffffffffUL,
		.out_hex = "FFFFFFFF",
	},
};

#define N_BN_MOD_WORD_TESTS \
    (sizeof(bn_mod_word_tests) / sizeof(bn_mod_word_tests[0]))

static int
test_bn_mod_word(void)
{
	const char *op_name = "BN_mod_word";
	const struct bn_word_test *bwt;
	BN_ULONG mod_word;
	BIGNUM *bn;
	size_t i;
	int failed = 0;

	if ((bn = BN_new()) == NULL)
		errx(1, "BN_new() failed");

	for (i = 0; i < N_BN_MOD_WORD_TESTS; i++) {
		bwt = &bn_mod_word_tests[i];

		if (!BN_hex2bn(&bn, bwt->in_hex)) {
			fprintf(stderr, "FAIL: BN_hex2bn(\"%s\") failed\n",
			    bwt->in_hex);
			failed = 1;
			continue;
		}

		if ((mod_word = BN_mod_word(bn, bwt->in_word)) != bwt->mod_word) {
			fprintf(stderr, "FAIL %s: Got mod word %lx, want %lx\n",
			    op_name, (unsigned long)mod_word,
			    (unsigned long)bwt->mod_word);
			failed = 1;
			continue;
		}

		failed |= check_bn_word_test(op_name, bn, bwt);
	}

	BN_free(bn);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_bn_add_word();
	failed |= test_bn_sub_word();
	failed |= test_bn_mul_word();
	failed |= test_bn_div_word();
	failed |= test_bn_mod_word();

	return failed;
}
