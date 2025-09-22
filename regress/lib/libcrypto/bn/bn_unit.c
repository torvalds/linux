/*	$OpenBSD: bn_unit.c,v 1.7 2023/06/21 07:15:38 jsing Exp $ */

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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>

static int
test_bn_print_wrapper(char *a, size_t size, const char *descr,
    int (*to_bn)(BIGNUM **, const char *))
{
	int ret;

	ret = to_bn(NULL, a);
	if (ret != 0 && (ret < 0 || (size_t)ret != size - 1)) {
		fprintf(stderr, "unexpected %s() return"
		    "want 0 or %zu, got %d\n", descr, size - 1, ret);
		return 1;
	}

	return 0;
}

static int
test_bn_print_null_derefs(void)
{
	size_t size = INT_MAX / 4 + 4;
	size_t datalimit = (size + 500 * 1024) / 1024;
	char *a;
	char digit;
	int failed = 0;

	if ((a = malloc(size)) == NULL) {
		warn("malloc(%zu) failed (make sure data limit is >= %zu KiB)",
		    size, datalimit);
		return 0;
	}

	/* Fill with a random digit since coverity doesn't like us using '0'. */
	digit = '0' + arc4random_uniform(10);

	memset(a, digit, size - 1);
	a[size - 1] = '\0';

	failed |= test_bn_print_wrapper(a, size, "BN_dec2bn", BN_dec2bn);
	failed |= test_bn_print_wrapper(a, size, "BN_hex2bn", BN_hex2bn);

	free(a);

	return failed;
}

static int
test_bn_num_bits(void)
{
	BIGNUM *bn;
	int i, num_bits;
	int failed = 0;

	if ((bn = BN_new()) == NULL)
		errx(1, "BN_new");

	if ((num_bits = BN_num_bits(bn)) != 0) {
		warnx("BN_num_bits(0): got %d, want 0", num_bits);
		failed |= 1;
	}

	if (!BN_set_word(bn, 1))
		errx(1, "BN_set_word");

	for (i = 0; i <= 5 * BN_BITS2; i++) {
		if ((num_bits = BN_num_bits(bn)) != i + 1) {
			warnx("BN_num_bits(1 << %d): got %d, want %d",
			    i, num_bits, i + 1);
			failed |= 1;
		}
		if (!BN_lshift1(bn, bn))
			errx(1, "BN_lshift1");
	}

	if (BN_hex2bn(&bn, "0000000000000000010000000000000000") != 34)
		errx(1, "BN_hex2bn");

	if ((num_bits = BN_num_bits(bn)) != 65) {
		warnx("BN_num_bits(1 << 64) padded: got %d, want %d",
		    num_bits, 65);
		failed |= 1;
	}

	BN_free(bn);

	return failed;
}

static int
test_bn_num_bits_word(void)
{
	BN_ULONG w = 1;
	int i, num_bits;
	int failed = 0;

	if ((num_bits = BN_num_bits_word(0)) != 0) {
		warnx("BN_num_bits_word(0): want 0, got %d", num_bits);
		failed |= 1;
	}

	for (i = 0; i < BN_BITS2; i++) {
		if ((num_bits = BN_num_bits_word(w << i)) != i + 1) {
			warnx("BN_num_bits_word(0x%llx): want %d, got %d",
			    (unsigned long long)(w << i), i + 1, num_bits);
			failed |= 1;
		}
	}

	return failed;
}

#define BN_FLG_ALL_KNOWN \
    (BN_FLG_STATIC_DATA | BN_FLG_CONSTTIME | BN_FLG_MALLOCED)

static int
bn_check_expected_flags(const BIGNUM *bn, int expected, const char *fn,
    const char *descr)
{
	int flags, got;
	int ret = 1;

	flags = BN_get_flags(bn, BN_FLG_ALL_KNOWN);

	if ((got = flags & expected) != expected) {
		fprintf(stderr, "%s: %s: expected flags: want %x, got %x\n",
		    fn, descr, expected, got);
		ret = 0;
	}

	if ((got = flags & ~expected) != 0) {
		fprintf(stderr, "%s: %s: unexpected flags: want %x, got %x\n",
		    fn, descr, 0, got);
		ret = 0;
	}

	return ret;
}

static int
test_bn_copy_copies_flags(void)
{
	BIGNUM *dst, *src;
	int failed = 0;

	if ((dst = BN_new()) == NULL)
		errx(1, "%s: src = BN_new()", __func__);

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED,
	    __func__, "dst after BN_new"))
		failed |= 1;

	if (BN_copy(dst, BN_value_one()) == NULL)
		errx(1, "%s: bn_copy()", __func__);

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED,
	    __func__, "dst after bn_copy"))
		failed |= 1;

	if ((src = BN_new()) == NULL)
		errx(1, "%s: src = BN_new()", __func__);

	BN_set_flags(src, BN_FLG_CONSTTIME);

	if (!bn_check_expected_flags(src, BN_FLG_MALLOCED | BN_FLG_CONSTTIME,
	    __func__, "src after BN_set_flags"))
		failed |= 1;

	if (!BN_set_word(src, 57))
		errx(1, "%s: BN_set_word(src, 57)", __func__);

	if (BN_copy(dst, src) == NULL)
		errx(1, "%s: BN_copy(dst, src)", __func__);

	if (BN_cmp(src, dst) != 0) {
		fprintf(stderr, "copy not equal to original\n");
		failed |= 1;
	}

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED | BN_FLG_CONSTTIME,
	    __func__, "dst after BN_copy(dst, src)"))
		failed |= 1;

	BN_free(dst);
	BN_free(src);

	return failed;
}

static int
test_bn_copy_consttime_is_sticky(void)
{
	BIGNUM *src, *dst;
	int failed = 0;

	if ((src = BN_new()) == NULL)
		errx(1, "%s: src = BN_new()", __func__);

	if (!bn_check_expected_flags(src, BN_FLG_MALLOCED,
	    __func__, "src after BN_new"))
		failed |= 1;

	if ((dst = BN_new()) == NULL)
		errx(1, "%s: dst = BN_new()", __func__);

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED,
	    __func__, "dst after BN_new"))
		failed |= 1;

	BN_set_flags(dst, BN_FLG_CONSTTIME);

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED | BN_FLG_CONSTTIME,
	    __func__, "src after BN_new"))
		failed |= 1;

	if (BN_copy(dst, BN_value_one()) == NULL)
		errx(1, "%s: bn_copy()", __func__);

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED | BN_FLG_CONSTTIME,
	    __func__, "dst after bn_copy"))
		failed |= 1;

	BN_free(dst);
	BN_free(src);

	return failed;
}

static int
test_bn_dup_consttime_is_sticky(void)
{
	BIGNUM *src, *dst;
	int failed = 0;

	if (!bn_check_expected_flags(BN_value_one(), BN_FLG_STATIC_DATA,
	    __func__, "flags on BN_value_one()"))
		failed |= 1;

	if ((dst = BN_dup(BN_value_one())) == NULL)
		errx(1, "%s: dst = BN_dup(BN_value_one())", __func__);

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED,
	    __func__, "dst after BN_dup(BN_value_one())"))
		failed |= 1;

	BN_free(dst);

	if ((src = BN_new()) == NULL)
		errx(1, "%s: src = BN_new()", __func__);

	BN_set_flags(src, BN_FLG_CONSTTIME);

	if (!bn_check_expected_flags(src, BN_FLG_MALLOCED | BN_FLG_CONSTTIME,
	    __func__, "src after BN_new"))
		failed |= 1;

	if ((dst = BN_dup(src)) == NULL)
		errx(1, "%s: dst = BN_dup(src)", __func__);

	if (!bn_check_expected_flags(dst, BN_FLG_MALLOCED | BN_FLG_CONSTTIME,
	    __func__, "dst after bn_copy"))
		failed |= 1;

	BN_free(dst);
	BN_free(src);

	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_bn_print_null_derefs();
	failed |= test_bn_num_bits();
	failed |= test_bn_num_bits_word();
	failed |= test_bn_copy_copies_flags();
	failed |= test_bn_copy_consttime_is_sticky();
	failed |= test_bn_dup_consttime_is_sticky();

	return failed;
}
