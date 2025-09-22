/*	$OpenBSD: bn_bits.c,v 1.2 2024/04/15 14:36:16 jsing Exp $ */
/*
 * Copyright (c) 2024 Joel Sing <jsing@openbsd.org>
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
#include <string.h>

#include <openssl/bn.h>

static int
test_bn_set_bit(void)
{
	BIGNUM *bn = NULL;
	char *out_str = NULL;
	size_t i;
	int failed = 1;

	if ((bn = BN_new()) == NULL)
		goto failure;

	for (i = 0; i < 128; i++) {
		if (i % 2 == 0) {
			if (!BN_set_bit(bn, i)) {
				fprintf(stderr, "FAIL: failed to set bit\n");
				goto failure;
			}
		}
		if (BN_is_bit_set(bn, i) != (i % 2 == 0)) {
			fprintf(stderr, "FAIL: BN_is_bit_set() = %d, want %d\n",
			    BN_is_bit_set(bn, i), (i % 2 == 0));
			goto failure;
		}
	}

	if ((out_str = BN_bn2hex(bn)) == NULL)
		goto failure;
	if (strcmp(out_str, "55555555555555555555555555555555") != 0) {
		fprintf(stderr, "FAIL: got 0x%s, want 0x%s\n", out_str,
		    "55555555555555555555555555555555");
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(bn);
	free(out_str);

	return failed;
}

static int
test_bn_clear_bit(void)
{
	BIGNUM *bn = NULL;
	char *out_str = NULL;
	size_t i;
	int failed = 1;

	if ((bn = BN_new()) == NULL)
		goto failure;

	for (i = 0; i < 128; i++) {
		if (!BN_set_bit(bn, i)) {
			fprintf(stderr, "FAIL: failed to set bit\n");
			goto failure;
		}
		if (i % 2 == 0) {
			if (!BN_clear_bit(bn, i)) {
				fprintf(stderr, "FAIL: failed to clear bit\n");
				goto failure;
			}
		}
		if (BN_is_bit_set(bn, i) != (i % 2 == 1)) {
			fprintf(stderr, "FAIL: BN_is_bit_set() = %d, want %d\n",
			    BN_is_bit_set(bn, i), (i % 2 == 1));
			goto failure;
		}
	}

	if ((out_str = BN_bn2hex(bn)) == NULL)
		goto failure;
	if (strcmp(out_str, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") != 0) {
		fprintf(stderr, "FAIL: got 0x%s, want 0x%s\n", out_str,
		    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
		goto failure;
	}

	/* Ensure that clearing results in non-negative zero. */
	if (!BN_one(bn))
		goto failure;
	BN_set_negative(bn, 1);
	if (!BN_clear_bit(bn, 0)) {
		fprintf(stderr, "FAIL: failed to clear bit\n");
		goto failure;
	}
	if (!BN_is_zero(bn)) {
		fprintf(stderr, "FAIL: clear bit did not result in zero\n");
		goto failure;
	}
	if (BN_is_negative(bn)) {
		fprintf(stderr, "FAIL: clear bit resulted in -0\n");
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(bn);
	free(out_str);

	return failed;
}

static int
test_bn_mask_bits(void)
{
	BIGNUM *bn = NULL;
	char *out_str = NULL;
	size_t i;
	int failed = 1;

	if ((bn = BN_new()) == NULL)
		goto failure;

	if (BN_mask_bits(bn, 0)) {
		fprintf(stderr, "FAIL: mask bits should have failed\n");
		goto failure;
	}

	for (i = 0; i < 128; i++) {
		if (!BN_set_bit(bn, i)) {
			fprintf(stderr, "FAIL: failed to set bit\n");
			goto failure;
		}
	}

	if ((out_str = BN_bn2hex(bn)) == NULL)
		goto failure;
	if (strcmp(out_str, "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF") != 0) {
		fprintf(stderr, "FAIL: got 0x%s, want 0x%s\n", out_str,
		    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
		goto failure;
	}

	if (!BN_mask_bits(bn, 127)) {
		fprintf(stderr, "FAIL: failed to mask bits\n");
		goto failure;
	}

	free(out_str);
	if ((out_str = BN_bn2hex(bn)) == NULL)
		goto failure;
	if (strcmp(out_str, "7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF") != 0) {
		fprintf(stderr, "FAIL: got 0x%s, want 0x%s\n", out_str,
		    "7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
		goto failure;
	}

	if (!BN_mask_bits(bn, 65)) {
		fprintf(stderr, "FAIL: failed to mask bits\n");
		goto failure;
	}

	free(out_str);
	if ((out_str = BN_bn2hex(bn)) == NULL)
		goto failure;
	if (strcmp(out_str, "01FFFFFFFFFFFFFFFF") != 0) {
		fprintf(stderr, "FAIL: got 0x%s, want 0x%s\n", out_str,
		    "01FFFFFFFFFFFFFFFF");
		goto failure;
	}

	/* Ensure that masking results in non-negative zero. */
	if (!BN_one(bn))
		goto failure;
	BN_set_negative(bn, 1);
	if (!BN_mask_bits(bn, 0)) {
		fprintf(stderr, "FAIL: failed to mask bits\n");
		goto failure;
	}
	if (!BN_is_zero(bn)) {
		fprintf(stderr, "FAIL: mask bits did not result in zero\n");
		goto failure;
	}
	if (BN_is_negative(bn)) {
		fprintf(stderr, "FAIL: mask bits resulted in -0\n");
		goto failure;
	}

	failed = 0;

 failure:
	BN_free(bn);
	free(out_str);

	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_bn_set_bit();
	failed |= test_bn_clear_bit();
	failed |= test_bn_mask_bits();

	return failed;
}
