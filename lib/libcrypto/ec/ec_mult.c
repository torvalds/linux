/* $OpenBSD: ec_mult.c,v 1.60 2025/08/26 14:14:52 tb Exp $ */

/*
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/ec.h>

#include "ec_local.h"
#include "err_local.h"

/* Holds the wNAF digits of bn and the corresponding odd multiples of point. */
struct ec_wnaf {
	signed char *digits;
	size_t num_digits;
	EC_POINT **multiples;
	size_t num_multiples;
};

static int
ec_window_bits(const BIGNUM *bn)
{
	int bits = BN_num_bits(bn);

	if (bits >= 2000)
		return 6;
	if (bits >= 800)
		return 5;
	if (bits >= 300)
		return 4;
	if (bits >= 70)
		return 3;
	if (bits >= 20)
		return 2;

	return 1;
}

/*
 * Width-(w+1) non-adjacent form of bn = \sum_j n_j 2^j, with odd n_j,
 * where at most one of any (w+1) consecutive digits is non-zero.
 */

static int
ec_compute_wnaf(const BIGNUM *bn, signed char *digits, size_t num_digits)
{
	int digit, bit, next, sign, wbits, window;
	size_t i;
	int ret = 0;

	if (num_digits != BN_num_bits(bn) + 1) {
		ECerror(ERR_R_INTERNAL_ERROR);
		goto err;
	}

	sign = BN_is_negative(bn) ? -1 : 1;

	wbits = ec_window_bits(bn);

	bit = 1 << wbits;
	next = bit << 1;

	/* Extract the wbits + 1 lowest bits from bn into window. */
	window = 0;
	for (i = 0; i < wbits + 1; i++) {
		if (BN_is_bit_set(bn, i))
			window |= (1 << i);
	}

	/* Instead of bn >>= 1 in each iteration, slide window to the left. */
	for (i = 0; i < num_digits; i++) {
		digit = 0;

		/*
		 * If window is odd, the i-th wNAF digit is window (mods 2^w),
		 * where mods is the signed modulo in (-2^w-1, 2^w-1]. Subtract
		 * the digit from window, so window is 0 or next, and add the
		 * digit to the wNAF digits.
		 */
		if ((window & 1) != 0) {
			digit = window;
			if ((window & bit) != 0)
				digit = window - next;
			window -= digit;
		}

		digits[i] = sign * digit;

		/* Slide the window to the left. */
		window >>= 1;
		window += bit * BN_is_bit_set(bn, i + wbits + 1);
	}

	ret = 1;

 err:
	return ret;
}

static int
ec_compute_odd_multiples(const EC_GROUP *group, const EC_POINT *point,
    EC_POINT **multiples, size_t num_multiples, BN_CTX *ctx)
{
	EC_POINT *doubled = NULL;
	size_t i;
	int ret = 0;

	if (num_multiples < 1)
		goto err;

	if ((multiples[0] = EC_POINT_dup(point, group)) == NULL)
		goto err;

	if ((doubled = EC_POINT_new(group)) == NULL)
		goto err;
	if (!EC_POINT_dbl(group, doubled, point, ctx))
		goto err;
	for (i = 1; i < num_multiples; i++) {
		if ((multiples[i] = EC_POINT_new(group)) == NULL)
			goto err;
		if (!EC_POINT_add(group, multiples[i], multiples[i - 1], doubled,
		    ctx))
			goto err;
	}

	ret = 1;

 err:
	EC_POINT_free(doubled);

	return ret;
}

/*
 * Bring multiples held in wnaf0 and wnaf1 simultaneously into affine form
 * so that the operations in the loop in ec_wnaf_mul() can take fast paths.
 */

static int
ec_normalize_points(const EC_GROUP *group, struct ec_wnaf *wnaf0,
    struct ec_wnaf *wnaf1, BN_CTX *ctx)
{
	EC_POINT **points0 = wnaf0->multiples, **points1 = wnaf1->multiples;
	size_t len0 = wnaf0->num_multiples, len1 = wnaf1->num_multiples;
	EC_POINT **val = NULL;
	size_t len = 0;
	int ret = 0;

	if (len1 > SIZE_MAX - len0)
		goto err;
	len = len0 + len1;

	if ((val = calloc(len, sizeof(*val))) == NULL) {
		ECerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	memcpy(&val[0], points0, sizeof(*val) * len0);
	memcpy(&val[len0], points1, sizeof(*val) * len1);

	if (!group->meth->points_make_affine(group, len, val, ctx))
		goto err;

	ret = 1;

 err:
	free(val);

	return ret;
}

static void
ec_points_free(EC_POINT **points, size_t num_points)
{
	size_t i;

	if (points == NULL)
		return;

	for (i = 0; i < num_points; i++)
		EC_POINT_free(points[i]);
	free(points);
}

static void
ec_wnaf_free(struct ec_wnaf *wnaf)
{
	if (wnaf == NULL)
		return;

	free(wnaf->digits);
	ec_points_free(wnaf->multiples, wnaf->num_multiples);
	free(wnaf);
}

/*
 * Calculate wNAF splitting of bn and the corresponding odd multiples of point.
 */

static struct ec_wnaf *
ec_wnaf_new(const EC_GROUP *group, const BIGNUM *scalar, const EC_POINT *point,
    BN_CTX *ctx)
{
	struct ec_wnaf *wnaf;

	if ((wnaf = calloc(1, sizeof(*wnaf))) == NULL)
		goto err;

	wnaf->num_digits = BN_num_bits(scalar) + 1;
	if ((wnaf->digits = calloc(wnaf->num_digits,
	    sizeof(*wnaf->digits))) == NULL)
		goto err;

	if (!ec_compute_wnaf(scalar, wnaf->digits, wnaf->num_digits))
		goto err;

	wnaf->num_multiples = 1ULL << (ec_window_bits(scalar) - 1);
	if ((wnaf->multiples = calloc(wnaf->num_multiples,
	    sizeof(*wnaf->multiples))) == NULL)
		goto err;

	if (!ec_compute_odd_multiples(group, point, wnaf->multiples,
	    wnaf->num_multiples, ctx))
		goto err;

	return wnaf;

 err:
	ec_wnaf_free(wnaf);

	return NULL;
}

static signed char
ec_wnaf_digit(struct ec_wnaf *wnaf, size_t idx)
{
	if (idx >= wnaf->num_digits)
		return 0;

	return wnaf->digits[idx];
}

static const EC_POINT *
ec_wnaf_multiple(struct ec_wnaf *wnaf, signed char digit)
{
	if (digit < 0)
		return NULL;
	if (digit >= 2 * wnaf->num_multiples)
		return NULL;

	return wnaf->multiples[digit >> 1];
}

/*
 * Compute r = scalar1 * point1 + scalar2 * point2 in non-constant time.
 */

int
ec_wnaf_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar1,
    const EC_POINT *point1, const BIGNUM *scalar2, const EC_POINT *point2,
    BN_CTX *ctx)
{
	struct ec_wnaf *wnaf[2] = { NULL, NULL };
	size_t i;
	int k;
	int r_is_inverted = 0;
	size_t num_digits;
	int ret = 0;

	if (scalar1 == NULL || scalar2 == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}
	if (group->meth != r->meth || group->meth != point1->meth ||
	    group->meth != point2->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}

	if ((wnaf[0] = ec_wnaf_new(group, scalar1, point1, ctx)) == NULL)
		goto err;
	if ((wnaf[1] = ec_wnaf_new(group, scalar2, point2, ctx)) == NULL)
		goto err;

	if (!ec_normalize_points(group, wnaf[0], wnaf[1], ctx))
		goto err;

	num_digits = wnaf[0]->num_digits;
	if (wnaf[1]->num_digits > num_digits)
		num_digits = wnaf[1]->num_digits;

	/*
	 * Set r to the neutral element. Scan through the wNAF representations
	 * of m and n, starting at the most significant digit. Double r and for
	 * each wNAF digit of scalar1 add the digit times point1, and for each
	 * wNAF digit of scalar2 add the digit times point2, adjusting the signs
	 * as appropriate.
	 */

	if (!EC_POINT_set_to_infinity(group, r))
		goto err;

	for (k = num_digits - 1; k >= 0; k--) {
		if (!EC_POINT_dbl(group, r, r, ctx))
			goto err;

		for (i = 0; i < 2; i++) {
			const EC_POINT *multiple;
			signed char digit;
			int is_neg = 0;

			if ((digit = ec_wnaf_digit(wnaf[i], k)) == 0)
				continue;

			if (digit < 0) {
				is_neg = 1;
				digit = -digit;
			}

			if (is_neg != r_is_inverted) {
				if (!EC_POINT_invert(group, r, ctx))
					goto err;
				r_is_inverted = !r_is_inverted;
			}

			if ((multiple = ec_wnaf_multiple(wnaf[i], digit)) == NULL)
				goto err;

			if (!EC_POINT_add(group, r, r, multiple, ctx))
				goto err;
		}
	}

	if (r_is_inverted) {
		if (!EC_POINT_invert(group, r, ctx))
			goto err;
	}

	ret = 1;

 err:
	ec_wnaf_free(wnaf[0]);
	ec_wnaf_free(wnaf[1]);

	return ret;
}
