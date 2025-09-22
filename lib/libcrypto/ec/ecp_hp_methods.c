/*	$OpenBSD: ecp_hp_methods.c,v 1.5 2025/08/03 15:44:00 jsing Exp $	*/
/*
 * Copyright (c) 2024-2025 Joel Sing <jsing@openbsd.org>
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

#include <string.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/err.h>

#include "bn_internal.h"
#include "crypto_internal.h"
#include "ec_local.h"
#include "ec_internal.h"
#include "err_local.h"

static int
ec_group_set_curve(EC_GROUP *group, const BIGNUM *p, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx)
{
	BIGNUM *t;
	int ret = 0;

	BN_CTX_start(ctx);

	/* XXX - p must be a prime > 3. */

	if (!bn_copy(group->p, p))
		goto err;
	if (!bn_copy(group->a, a))
		goto err;
	if (!bn_copy(group->b, b))
		goto err;

	/* XXX */
	BN_set_negative(group->p, 0);

	/* XXX */
	if (!BN_nnmod(group->a, group->a, group->p, ctx))
		goto err;
	if (!BN_nnmod(group->b, group->b, group->p, ctx))
		goto err;

	if ((t = BN_CTX_get(ctx)) == NULL)
		goto err;
	if (!BN_set_word(t, 3))
		goto err;
	if (!BN_mod_add(t, t, a, group->p, ctx))
		goto err;

	group->a_is_minus3 = BN_is_zero(t);

	if (!ec_field_modulus_from_bn(&group->fm, group->p, ctx))
		goto err;
	if (!ec_field_element_from_bn(&group->fm, group, &group->fe_a, group->a, ctx))
		goto err;
	if (!ec_field_element_from_bn(&group->fm, group, &group->fe_b, group->b, ctx))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_group_get_curve(const EC_GROUP *group, BIGNUM *p, BIGNUM *a,
    BIGNUM *b, BN_CTX *ctx)
{
	if (p != NULL) {
		if (!bn_copy(p, group->p))
			return 0;
	}
	if (a != NULL) {
		if (!bn_copy(a, group->a))
			return 0;
	}
	if (b != NULL) {
		if (!bn_copy(b, group->b))
			return 0;
	}
	return 1;
}

static int
ec_point_is_at_infinity(const EC_GROUP *group, const EC_POINT *point)
{
	/* Check if Z is equal to zero. */
	return ec_field_element_is_zero(&group->fm, &point->fe_z);
}

static int
ec_point_set_to_infinity(const EC_GROUP *group, EC_POINT *point)
{
	/* Infinity is (x = 0, y = 1, z = 0). */

	memset(&point->fe_x, 0, sizeof(point->fe_x));
	memset(&point->fe_y, 0, sizeof(point->fe_y));
	memset(&point->fe_z, 0, sizeof(point->fe_z));

	point->fe_y.w[0] = 1;

	return 1;
}

static int
ec_point_set_affine_coordinates(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx)
{
	if (x == NULL || y == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		return 0;
	}

	if (!bn_copy(point->X, x))
		return 0;
	if (!bn_copy(point->Y, y))
		return 0;
	if (!BN_one(point->Z))
		return 0;

	/* XXX */
	if (!BN_nnmod(point->X, point->X, group->p, ctx))
		return 0;
	if (!BN_nnmod(point->Y, point->Y, group->p, ctx))
		return 0;

	if (!ec_field_element_from_bn(&group->fm, group, &point->fe_x, point->X, ctx))
		return 0;
	if (!ec_field_element_from_bn(&group->fm, group, &point->fe_y, point->Y, ctx))
		return 0;
	if (!ec_field_element_from_bn(&group->fm, group, &point->fe_z, point->Z, ctx))
		return 0;

	return 1;
}

static int
ec_point_get_affine_coordinates(const EC_GROUP *group, const EC_POINT *point,
    BIGNUM *x, BIGNUM *y, BN_CTX *ctx)
{
	BIGNUM *zinv;
	int ret = 0;

	/*
	 * Convert homogeneous projective coordinates (XZ, YZ, Z) to affine
	 * coordinates (x = X/Z, y = Y/Z).
	 */
	if (!ec_field_element_to_bn(&group->fm, &point->fe_x, point->X, ctx))
		return 0;
	if (!ec_field_element_to_bn(&group->fm, &point->fe_y, point->Y, ctx))
		return 0;
	if (!ec_field_element_to_bn(&group->fm, &point->fe_z, point->Z, ctx))
		return 0;

	BN_CTX_start(ctx);

	if ((zinv = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (BN_mod_inverse_ct(zinv, point->Z, group->p, ctx) == NULL)
		goto err;

	if (x != NULL) {
		if (!BN_mod_mul(x, point->X, zinv, group->p, ctx))
			goto err;
	}
	if (y != NULL) {
		if (!BN_mod_mul(y, point->Y, zinv, group->p, ctx))
			goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_point_add_a1(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    const EC_POINT *b, BN_CTX *ctx)
{
	EC_FIELD_ELEMENT X1, Y1, Z1, X2, Y2, Z2, X3, Y3, Z3;
	EC_FIELD_ELEMENT b3, t0, t1, t2, t3, t4, t5;
	EC_FIELD_ELEMENT ga, gb;

	/*
	 * Complete, projective point addition for arbitrary prime order short
	 * Weierstrass curves with arbitrary a - see
	 * https://eprint.iacr.org/2015/1060, algorithm 1 and appendix A.1.
	 */

	ec_field_element_copy(&ga, &group->fe_a);
	ec_field_element_copy(&gb, &group->fe_b);

	ec_field_element_copy(&X1, &a->fe_x);
	ec_field_element_copy(&Y1, &a->fe_y);
	ec_field_element_copy(&Z1, &a->fe_z);

	ec_field_element_copy(&X2, &b->fe_x);
	ec_field_element_copy(&Y2, &b->fe_y);
	ec_field_element_copy(&Z2, &b->fe_z);

	/* b3 := 3 * b ; */
	ec_field_element_add(&group->fm, &b3, &gb, &gb);
	ec_field_element_add(&group->fm, &b3, &b3, &gb);

	/* t0 := X1 * X2 ; t1 := Y1 * Y2 ; t2 := Z1 * Z2 ; */
	ec_field_element_mul(&group->fm, &t0, &X1, &X2);
	ec_field_element_mul(&group->fm, &t1, &Y1, &Y2);
	ec_field_element_mul(&group->fm, &t2, &Z1, &Z2);

	/* t3 := X1 + Y1 ; t4 := X2 + Y2 ; t3 := t3 * t4 ; */
	ec_field_element_add(&group->fm, &t3, &X1, &Y1);
	ec_field_element_add(&group->fm, &t4, &X2, &Y2);
	ec_field_element_mul(&group->fm, &t3, &t3, &t4);

	/* t4 := t0 + t1 ; t3 := t3 - t4 ; t4 := X1 + Z1 ; */
	ec_field_element_add(&group->fm, &t4, &t0, &t1);
	ec_field_element_sub(&group->fm, &t3, &t3, &t4);
	ec_field_element_add(&group->fm, &t4, &X1, &Z1);

	/* t5 := X2 + Z2 ; t4 := t4 * t5 ; t5 := t0 + t2 ; */
	ec_field_element_add(&group->fm, &t5, &X2, &Z2);
	ec_field_element_mul(&group->fm, &t4, &t4, &t5);
	ec_field_element_add(&group->fm, &t5, &t0, &t2);

	/* t4 := t4 - t5 ; t5 := Y1 + Z1 ; X3 := Y2 + Z2 ; */
	ec_field_element_sub(&group->fm, &t4, &t4, &t5);
	ec_field_element_add(&group->fm, &t5, &Y1, &Z1);
	ec_field_element_add(&group->fm, &X3, &Y2, &Z2);

	/* t5 := t5 * X3 ; X3 := t1 + t2 ; t5 := t5 - X3 ; */
	ec_field_element_mul(&group->fm, &t5, &t5, &X3);
	ec_field_element_add(&group->fm, &X3, &t1, &t2);
	ec_field_element_sub(&group->fm, &t5, &t5, &X3);

	/* Z3 := a * t4 ; X3 := b3 * t2 ; Z3 := X3 + Z3 ; */
	ec_field_element_mul(&group->fm, &Z3, &ga, &t4);
	ec_field_element_mul(&group->fm, &X3, &b3, &t2);
	ec_field_element_add(&group->fm, &Z3, &X3, &Z3);

	/* X3 := t1 - Z3 ; Z3 := t1 + Z3 ; Y3 := X3 * Z3 ; */
	ec_field_element_sub(&group->fm, &X3, &t1, &Z3);
	ec_field_element_add(&group->fm, &Z3, &t1, &Z3);
	ec_field_element_mul(&group->fm, &Y3, &X3, &Z3);

	/* t1 := t0 + t0 ; t1 := t1 + t0 ; t2 := a * t2 ; */
	ec_field_element_add(&group->fm, &t1, &t0, &t0);
	ec_field_element_add(&group->fm, &t1, &t1, &t0);
	ec_field_element_mul(&group->fm, &t2, &ga, &t2);

	/* t4 := b3 * t4 ; t1 := t1 + t2 ; t2 := t0 - t2 ; */
	ec_field_element_mul(&group->fm, &t4, &b3, &t4);
	ec_field_element_add(&group->fm, &t1, &t1, &t2);
	ec_field_element_sub(&group->fm, &t2, &t0, &t2);

	/* t2 := a * t2 ; t4 := t4 + t2 ; t0 := t1 * t4 ; */
	ec_field_element_mul(&group->fm, &t2, &ga, &t2);
	ec_field_element_add(&group->fm, &t4, &t4, &t2);
	ec_field_element_mul(&group->fm, &t0, &t1, &t4);

	/* Y3 := Y3 + t0 ; t0 := t5 * t4 ; X3 := t3 * X3 ; */
	ec_field_element_add(&group->fm, &Y3, &Y3, &t0);
	ec_field_element_mul(&group->fm, &t0, &t5, &t4);
	ec_field_element_mul(&group->fm, &X3, &t3, &X3);

	/* X3 := X3 - t0 ; t0 := t3 * t1 ; Z3 := t5 * Z3 ; */
	ec_field_element_sub(&group->fm, &X3, &X3, &t0);
	ec_field_element_mul(&group->fm, &t0, &t3, &t1);
	ec_field_element_mul(&group->fm, &Z3, &t5, &Z3);

	/* Z3 := Z3 + t0 ; */
	ec_field_element_add(&group->fm, &Z3, &Z3, &t0);

	ec_field_element_copy(&r->fe_x, &X3);
	ec_field_element_copy(&r->fe_y, &Y3);
	ec_field_element_copy(&r->fe_z, &Z3);

	return 1;
}

static int
ec_point_add_a2(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    const EC_POINT *b, BN_CTX *ctx)
{
	EC_FIELD_ELEMENT X1, Y1, Z1, X2, Y2, Z2, X3, Y3, Z3;
	EC_FIELD_ELEMENT t0, t1, t2, t3, t4;
	EC_FIELD_ELEMENT gb;

	/*
	 * Complete, projective point addition for arbitrary prime order short
	 * Weierstrass curves with a = -3 - see https://eprint.iacr.org/2015/1060,
	 * algorithm 4 and appendix A.2.
	 */

	ec_field_element_copy(&gb, &group->fe_b);

	ec_field_element_copy(&X1, &a->fe_x);
	ec_field_element_copy(&Y1, &a->fe_y);
	ec_field_element_copy(&Z1, &a->fe_z);

	ec_field_element_copy(&X2, &b->fe_x);
	ec_field_element_copy(&Y2, &b->fe_y);
	ec_field_element_copy(&Z2, &b->fe_z);

	/* t0 := X1 * X2 ; t1 := Y1 * Y2 ; t2 := Z1 * Z2 ; */
	ec_field_element_mul(&group->fm, &t0, &X1, &X2);
	ec_field_element_mul(&group->fm, &t1, &Y1, &Y2);
	ec_field_element_mul(&group->fm, &t2, &Z1, &Z2);

	/* t3 := X1 + Y1 ; t4 := X2 + Y2 ; t3 := t3 * t4 ; */
	ec_field_element_add(&group->fm, &t3, &X1, &Y1);
	ec_field_element_add(&group->fm, &t4, &X2, &Y2);
	ec_field_element_mul(&group->fm, &t3, &t3, &t4);

	/* t4 := t0 + t1 ; t3 := t3 - t4 ; t4 := Y1 + Z1 ; */
	ec_field_element_add(&group->fm, &t4, &t0, &t1);
	ec_field_element_sub(&group->fm, &t3, &t3, &t4);
	ec_field_element_add(&group->fm, &t4, &Y1, &Z1);

	/* X3 := Y2 + Z2 ; t4 := t4 * X3 ; X3 := t1 + t2 ; */
	ec_field_element_add(&group->fm, &X3, &Y2, &Z2);
	ec_field_element_mul(&group->fm, &t4, &t4, &X3);
	ec_field_element_add(&group->fm, &X3, &t1, &t2);

	/* t4 := t4 - X3 ; X3 := X1 + Z1 ; Y3 := X2 + Z2 ; */
	ec_field_element_sub(&group->fm, &t4, &t4, &X3);
	ec_field_element_add(&group->fm, &X3, &X1, &Z1);
	ec_field_element_add(&group->fm, &Y3, &X2, &Z2);

	/* X3 := X3 * Y3 ; Y3 := t0 + t2 ; Y3 := X3 - Y3 ; */
	ec_field_element_mul(&group->fm, &X3, &X3, &Y3);
	ec_field_element_add(&group->fm, &Y3, &t0, &t2);
	ec_field_element_sub(&group->fm, &Y3, &X3, &Y3);

	/* Z3 := b * t2 ; X3 := Y3 - Z3 ; Z3 := X3 + X3 ; */
	ec_field_element_mul(&group->fm, &Z3, &gb, &t2);
	ec_field_element_sub(&group->fm, &X3, &Y3, &Z3);
	ec_field_element_add(&group->fm, &Z3, &X3, &X3);

	/* X3 := X3 + Z3 ; Z3 := t1 - X3 ; X3 := t1 + X3 ; */
	ec_field_element_add(&group->fm, &X3, &X3, &Z3);
	ec_field_element_sub(&group->fm, &Z3, &t1, &X3);
	ec_field_element_add(&group->fm, &X3, &t1, &X3);

	/* Y3 := b * Y3 ; t1 := t2 + t2 ; t2 := t1 + t2 ; */
	ec_field_element_mul(&group->fm, &Y3, &gb, &Y3);
	ec_field_element_add(&group->fm, &t1, &t2, &t2);
	ec_field_element_add(&group->fm, &t2, &t1, &t2);

	/* Y3 := Y3 - t2 ; Y3 := Y3 - t0 ; t1 := Y3 + Y3 ; */
	ec_field_element_sub(&group->fm, &Y3, &Y3, &t2);
	ec_field_element_sub(&group->fm, &Y3, &Y3, &t0);
	ec_field_element_add(&group->fm, &t1, &Y3, &Y3);

	/* Y3 := t1 + Y3 ; t1 := t0 + t0 ; t0 := t1 + t0 ; */
	ec_field_element_add(&group->fm, &Y3, &t1, &Y3);
	ec_field_element_add(&group->fm, &t1, &t0, &t0);
	ec_field_element_add(&group->fm, &t0, &t1, &t0);

	/* t0 := t0 - t2 ; t1 := t4 * Y3 ; t2 := t0 * Y3 ; */
	ec_field_element_sub(&group->fm, &t0, &t0, &t2);
	ec_field_element_mul(&group->fm, &t1, &t4, &Y3);
	ec_field_element_mul(&group->fm, &t2, &t0, &Y3);

	/* Y3 := X3 * Z3 ; Y3 := Y3 + t2 ; X3 := t3 * X3 ; */
	ec_field_element_mul(&group->fm, &Y3, &X3, &Z3);
	ec_field_element_add(&group->fm, &Y3, &Y3, &t2);
	ec_field_element_mul(&group->fm, &X3, &t3, &X3);

	/* X3 := X3 - t1 ; Z3 := t4 * Z3 ; t1 := t3 * t0 ; */
	ec_field_element_sub(&group->fm, &X3, &X3, &t1);
	ec_field_element_mul(&group->fm, &Z3, &t4, &Z3);
	ec_field_element_mul(&group->fm, &t1, &t3, &t0);

	/* Z3 := Z3 + t1 ; */
	ec_field_element_add(&group->fm, &Z3, &Z3, &t1);

	ec_field_element_copy(&r->fe_x, &X3);
	ec_field_element_copy(&r->fe_y, &Y3);
	ec_field_element_copy(&r->fe_z, &Z3);

	return 1;
}

static int
ec_point_add(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    const EC_POINT *b, BN_CTX *ctx)
{
	if (group->a_is_minus3)
		return ec_point_add_a2(group, r, a, b, ctx);

	return ec_point_add_a1(group, r, a, b, ctx);
}

static int
ec_point_dbl_a1(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a, BN_CTX *ctx)
{
	EC_FIELD_ELEMENT X1, Y1, Z1, X3, Y3, Z3;
	EC_FIELD_ELEMENT b3, t0, t1, t2, t3;
	EC_FIELD_ELEMENT ga, gb;

	/*
	 * Exception-free point doubling for arbitrary prime order short
	 * Weierstrass curves with arbitrary a - see
	 * https://eprint.iacr.org/2015/1060, algorithm 3 and appendix A.1.
	 */

	ec_field_element_copy(&ga, &group->fe_a);
	ec_field_element_copy(&gb, &group->fe_b);

	ec_field_element_copy(&X1, &a->fe_x);
	ec_field_element_copy(&Y1, &a->fe_y);
	ec_field_element_copy(&Z1, &a->fe_z);

	/* b3 := 3 * b ; */
	ec_field_element_add(&group->fm, &b3, &gb, &gb);
	ec_field_element_add(&group->fm, &b3, &b3, &gb);

	/* t0 := X^2; t1 := Y^2; t2 := Z^2 ; */
	ec_field_element_sqr(&group->fm, &t0, &X1);
	ec_field_element_sqr(&group->fm, &t1, &Y1);
	ec_field_element_sqr(&group->fm, &t2, &Z1);

	/* t3 := X * Y ; t3 := t3 + t3 ; Z3 := X * Z ; */
	ec_field_element_mul(&group->fm, &t3, &X1, &Y1);
	ec_field_element_add(&group->fm, &t3, &t3, &t3);
	ec_field_element_mul(&group->fm, &Z3, &X1, &Z1);

	/* Z3 := Z3 + Z3 ; X3 := a * Z3 ; Y3 := b3 * t2 ; */
	ec_field_element_add(&group->fm, &Z3, &Z3, &Z3);
	ec_field_element_mul(&group->fm, &X3, &ga, &Z3);
	ec_field_element_mul(&group->fm, &Y3, &b3, &t2);

	/* Y3 := X3 + Y3 ; X3 := t1 - Y3 ; Y3 := t1 + Y3 ; */
	ec_field_element_add(&group->fm, &Y3, &X3, &Y3);
	ec_field_element_sub(&group->fm, &X3, &t1, &Y3);
	ec_field_element_add(&group->fm, &Y3, &t1, &Y3);

	/* Y3 := X3 * Y3 ; X3 := t3 * X3 ; Z3 := b3 * Z3 ; */
	ec_field_element_mul(&group->fm, &Y3, &X3, &Y3);
	ec_field_element_mul(&group->fm, &X3, &t3, &X3);
	ec_field_element_mul(&group->fm, &Z3, &b3, &Z3);

	/* t2 := a * t2 ; t3 := t0 - t2 ; t3 := a * t3 ; */
	ec_field_element_mul(&group->fm, &t2, &ga, &t2);
	ec_field_element_sub(&group->fm, &t3, &t0, &t2);
	ec_field_element_mul(&group->fm, &t3, &ga, &t3);

	/* t3 := t3 + Z3 ; Z3 := t0 + t0 ; t0 := Z3 + t0 ; */
	ec_field_element_add(&group->fm, &t3, &t3, &Z3);
	ec_field_element_add(&group->fm, &Z3, &t0, &t0);
	ec_field_element_add(&group->fm, &t0, &Z3, &t0);

	/* t0 := t0 + t2 ; t0 := t0 * t3 ; Y3 := Y3 + t0 ; */
	ec_field_element_add(&group->fm, &t0, &t0, &t2);
	ec_field_element_mul(&group->fm, &t0, &t0, &t3);
	ec_field_element_add(&group->fm, &Y3, &Y3, &t0);

	/* t2 := Y * Z ; t2 := t2 + t2 ; t0 := t2 * t3 ; */
	ec_field_element_mul(&group->fm, &t2, &Y1, &Z1);
	ec_field_element_add(&group->fm, &t2, &t2, &t2);
	ec_field_element_mul(&group->fm, &t0, &t2, &t3);

	/* X3 := X3 - t0 ; Z3 := t2 * t1 ; Z3 := Z3 + Z3 ; */
	ec_field_element_sub(&group->fm, &X3, &X3, &t0);
	ec_field_element_mul(&group->fm, &Z3, &t2, &t1);
	ec_field_element_add(&group->fm, &Z3, &Z3, &Z3);

	/* Z3 := Z3 + Z3 ; */
	ec_field_element_add(&group->fm, &Z3, &Z3, &Z3);

	ec_field_element_copy(&r->fe_x, &X3);
	ec_field_element_copy(&r->fe_y, &Y3);
	ec_field_element_copy(&r->fe_z, &Z3);

	return 1;
}

static int
ec_point_dbl_a2(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a, BN_CTX *ctx)
{
	EC_FIELD_ELEMENT X1, Y1, Z1, X3, Y3, Z3;
	EC_FIELD_ELEMENT t0, t1, t2, t3;
	EC_FIELD_ELEMENT ga, gb;

	/*
	 * Exception-free point doubling for arbitrary prime order short
	 * Weierstrass curves with a = -3 - see https://eprint.iacr.org/2015/1060,
	 * algorithm 6 and appendix A.2.
	 */

	ec_field_element_copy(&ga, &group->fe_a);
	ec_field_element_copy(&gb, &group->fe_b);

	ec_field_element_copy(&X1, &a->fe_x);
	ec_field_element_copy(&Y1, &a->fe_y);
	ec_field_element_copy(&Z1, &a->fe_z);

	/* t0 := X^2; t1 := Y^2; t2 := Z^2 ; */
	ec_field_element_sqr(&group->fm, &t0, &X1);
	ec_field_element_sqr(&group->fm, &t1, &Y1);
	ec_field_element_sqr(&group->fm, &t2, &Z1);

	/* t3 := X * Y ; t3 := t3 + t3 ; Z3 := X * Z ; */
	ec_field_element_mul(&group->fm, &t3, &X1, &Y1);
	ec_field_element_add(&group->fm, &t3, &t3, &t3);
	ec_field_element_mul(&group->fm, &Z3, &X1, &Z1);

	/* Z3 := Z3 + Z3 ; Y3 := b * t2 ; Y3 := Y3 - Z3 ; */
	ec_field_element_add(&group->fm, &Z3, &Z3, &Z3);
	ec_field_element_mul(&group->fm, &Y3, &gb, &t2);
	ec_field_element_sub(&group->fm, &Y3, &Y3, &Z3);

	/* X3 := Y3 + Y3 ; Y3 := X3 + Y3 ; X3 := t1 - Y3 ; */
	ec_field_element_add(&group->fm, &X3, &Y3, &Y3);
	ec_field_element_add(&group->fm, &Y3, &X3, &Y3);
	ec_field_element_sub(&group->fm, &X3, &t1, &Y3);

	/* Y3 := t1 + Y3 ; Y3 := X3 * Y3 ; X3 := X3 * t3 ; */
	ec_field_element_add(&group->fm, &Y3, &t1, &Y3);
	ec_field_element_mul(&group->fm, &Y3, &X3, &Y3);
	ec_field_element_mul(&group->fm, &X3, &X3, &t3);

	/* t3 := t2 + t2 ; t2 := t2 + t3 ; Z3 := b * Z3 ; */
	ec_field_element_add(&group->fm, &t3, &t2, &t2);
	ec_field_element_add(&group->fm, &t2, &t2, &t3);
	ec_field_element_mul(&group->fm, &Z3, &gb, &Z3);

	/* Z3 := Z3 - t2 ; Z3 := Z3 - t0 ; t3 := Z3 + Z3 ; */
	ec_field_element_sub(&group->fm, &Z3, &Z3, &t2);
	ec_field_element_sub(&group->fm, &Z3, &Z3, &t0);
	ec_field_element_add(&group->fm, &t3, &Z3, &Z3);

	/* Z3 := Z3 + t3 ; t3 := t0 + t0 ; t0 := t3 + t0 ; */
	ec_field_element_add(&group->fm, &Z3, &Z3, &t3);
	ec_field_element_add(&group->fm, &t3, &t0, &t0);
	ec_field_element_add(&group->fm, &t0, &t3, &t0);

	/* t0 := t0 - t2 ; t0 := t0 * Z3 ; Y3 := Y3 + t0 ; */
	ec_field_element_sub(&group->fm, &t0, &t0, &t2);
	ec_field_element_mul(&group->fm, &t0, &t0, &Z3);
	ec_field_element_add(&group->fm, &Y3, &Y3, &t0);

	/* t0 := Y * Z ; t0 := t0 + t0 ; Z3 := t0 * Z3 ; */
	ec_field_element_mul(&group->fm, &t0, &Y1, &Z1);
	ec_field_element_add(&group->fm, &t0, &t0, &t0);
	ec_field_element_mul(&group->fm, &Z3, &t0, &Z3);

	/* X3 := X3 - Z3 ; Z3 := t0 * t1 ; Z3 := Z3 + Z3 ; */
	ec_field_element_sub(&group->fm, &X3, &X3, &Z3);
	ec_field_element_mul(&group->fm, &Z3, &t0, &t1);
	ec_field_element_add(&group->fm, &Z3, &Z3, &Z3);

	/* Z3 := Z3 + Z3 ; */
	ec_field_element_add(&group->fm, &Z3, &Z3, &Z3);

	ec_field_element_copy(&r->fe_x, &X3);
	ec_field_element_copy(&r->fe_y, &Y3);
	ec_field_element_copy(&r->fe_z, &Z3);

	return 1;
}

static int
ec_point_dbl(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a, BN_CTX *ctx)
{
	if (group->a_is_minus3)
		return ec_point_dbl_a2(group, r, a, ctx);

	return ec_point_dbl_a1(group, r, a, ctx);
}

static int
ec_point_invert(const EC_GROUP *group, EC_POINT *point, BN_CTX *ctx)
{
	EC_FIELD_ELEMENT y;
	BN_ULONG mask;
	int i;

	/*
	 * Invert the point by setting Y = p - Y, if Y is non-zero and the point
	 * is not at infinity.
	 */

	mask = ~(0 - (ec_point_is_at_infinity(group, point) |
	    ec_field_element_is_zero(&group->fm, &point->fe_y)));

	/* XXX - masked/conditional subtraction? */
	ec_field_element_sub(&group->fm, &y, &group->fm.m, &point->fe_y);

	for (i = 0; i < group->fm.n; i++)
		point->fe_y.w[i] = (point->fe_y.w[i] & ~mask) | (y.w[i] & mask);

	return 1;
}

static int
ec_point_is_on_curve(const EC_GROUP *group, const EC_POINT *point, BN_CTX *ctx)
{
	EC_FIELD_ELEMENT sum, axz2, bz3, x3, y2z, z2;

	/*
	 * Curve is defined by a Weierstrass equation y^2 = x^3 + a*x + b.
	 * The given point is in homogeneous projective coordinates
	 * (x = X/Z, y = Y/Z). Substitute and multiply by Z^3 in order to
	 * evaluate as zy^2 = x^3 + axz^2 + bz^3.
	 */

	ec_field_element_sqr(&group->fm, &z2, &point->fe_z);

	ec_field_element_sqr(&group->fm, &y2z, &point->fe_y);
	ec_field_element_mul(&group->fm, &y2z, &y2z, &point->fe_z);

	ec_field_element_sqr(&group->fm, &x3, &point->fe_x);
	ec_field_element_mul(&group->fm, &x3, &x3, &point->fe_x);

	ec_field_element_mul(&group->fm, &axz2, &group->fe_a, &point->fe_x);
	ec_field_element_mul(&group->fm, &axz2, &axz2, &z2);

	ec_field_element_mul(&group->fm, &bz3, &group->fe_b, &point->fe_z);
	ec_field_element_mul(&group->fm, &bz3, &bz3, &z2);

	ec_field_element_add(&group->fm, &sum, &x3, &axz2);
	ec_field_element_add(&group->fm, &sum, &sum, &bz3);

	return ec_field_element_equal(&group->fm, &y2z, &sum) |
	    ec_point_is_at_infinity(group, point);
}

static int
ec_point_cmp(const EC_GROUP *group, const EC_POINT *a, const EC_POINT *b, BN_CTX *ctx)
{
	EC_FIELD_ELEMENT ax, ay, bx, by;

	/*
	 * Compare two points that have homogeneous projection coordinates, that
	 * is (X_a/Z_a, Y_a/Z_a) == (X_b/Z_b, Y_b/Z_b). Return -1 on error, 0 on
	 * equality and 1 on inequality.
	 *
	 * If a and b are both at infinity, Z_a and Z_b will both be zero,
	 * resulting in all values becoming zero, resulting in equality. If a is
	 * at infinity and b is not, then Y_a will be one and Z_b will be
	 * non-zero, hence Y_a * Z_b will be non-zero. Z_a will be zero, hence
	 * Y_b * Z_a will be zero, resulting in inequality. The same applies if
	 * b is at infinity and a is not.
	 */

	ec_field_element_mul(&group->fm, &ax, &a->fe_x, &b->fe_z);
	ec_field_element_mul(&group->fm, &ay, &a->fe_y, &b->fe_z);
	ec_field_element_mul(&group->fm, &bx, &b->fe_x, &a->fe_z);
	ec_field_element_mul(&group->fm, &by, &b->fe_y, &a->fe_z);

	return 1 - (ec_field_element_equal(&group->fm, &ax, &bx) &
	    ec_field_element_equal(&group->fm, &ay, &by));
}

#if 0
static int
ec_points_make_affine(const EC_GROUP *group, size_t num, EC_POINT *points[],
    BN_CTX *ctx)
{
	size_t i;

	/* XXX */
	for (i = 0; i < num; i++) {
		if (!EC_POINT_make_affine(group, points[0], ctx))
			return 0;
	}

	return 1;
}
#else

static int
ec_points_make_affine(const EC_GROUP *group, size_t num, EC_POINT *points[],
    BN_CTX *ctx)
{
	BIGNUM **prod_Z = NULL;
	BIGNUM *tmp, *tmp_Z;
	size_t i;
	int ret = 0;

	if (num == 0)
		return 1;

	BN_CTX_start(ctx);

	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((tmp_Z = BN_CTX_get(ctx)) == NULL)
		goto err;

	if ((prod_Z = calloc(num, sizeof *prod_Z)) == NULL)
		goto err;
	for (i = 0; i < num; i++) {
		if ((prod_Z[i] = BN_CTX_get(ctx)) == NULL)
			goto err;
	}

	if (!BN_is_zero(points[0]->Z)) {
		if (!bn_copy(prod_Z[0], points[0]->Z))
			goto err;
	} else {
		if (!BN_one(prod_Z[0]))
			goto err;
	}

	for (i = 1; i < num; i++) {
		if (!BN_is_zero(points[i]->Z)) {
			if (!BN_mod_mul(prod_Z[i], prod_Z[i - 1], points[i]->Z,
			    group->p, ctx))
				goto err;
		} else {
			if (!bn_copy(prod_Z[i], prod_Z[i - 1]))
				goto err;
		}
	}

	if (!BN_mod_inverse_nonct(tmp, prod_Z[num - 1], group->p, ctx)) {
		ECerror(ERR_R_BN_LIB);
		goto err;
	}

	for (i = num - 1; i > 0; i--) {
		if (BN_is_zero(points[i]->Z))
			continue;

		if (!BN_mod_mul(tmp_Z, prod_Z[i - 1], tmp, group->p, ctx))
			goto err;
		if (!BN_mod_mul(tmp, tmp, points[i]->Z, group->p, ctx))
			goto err;
		if (!bn_copy(points[i]->Z, tmp_Z))
			goto err;
	}

	for (i = 0; i < num; i++) {
		EC_POINT *p = points[i];

		if (BN_is_zero(p->Z))
			continue;

		if (!BN_mod_mul(p->X, p->X, p->Z, group->p, ctx))
			goto err;
		if (!BN_mod_mul(p->Y, p->Y, p->Z, group->p, ctx))
			goto err;

		if (!BN_one(p->Z))
			goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);
	free(prod_Z);

	return ret;
}
#endif

static void
ec_point_select(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a,
    const EC_POINT *b, int conditional)
{
	ec_field_element_select(&group->fm, &r->fe_x, &a->fe_x, &b->fe_x, conditional);
	ec_field_element_select(&group->fm, &r->fe_y, &a->fe_y, &b->fe_y, conditional);
	ec_field_element_select(&group->fm, &r->fe_z, &a->fe_z, &b->fe_z, conditional);
}

static int
ec_mul(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar, const EC_POINT *point,
    BN_CTX *ctx)
{
	BIGNUM *cardinality;
	EC_POINT *multiples[15];
	EC_POINT *rr = NULL, *t = NULL;
	uint8_t *scalar_bytes = NULL;
	int scalar_len = 0;
	uint8_t j, wv;
	int conditional, i;
	int ret = 0;

	memset(multiples, 0, sizeof(multiples));

	BN_CTX_start(ctx);

	/* XXX - consider blinding. */

	if ((cardinality = BN_CTX_get(ctx)) == NULL)
		goto err;
	if (!BN_mul(cardinality, group->order, group->cofactor, ctx))
		goto err;

	/* XXX - handle scalar > cardinality and/or negative. */

	/* Convert scalar into big endian bytes. */
	scalar_len = BN_num_bytes(cardinality);
	if ((scalar_bytes = calloc(1, scalar_len)) == NULL)
		goto err;
	if (!BN_bn2binpad(scalar, scalar_bytes, scalar_len))
		goto err;

	/* Compute multiples of point. */
	if ((multiples[0] = EC_POINT_dup(point, group)) == NULL)
		goto err;
	for (i = 1; i < 15; i += 2) {
		if ((multiples[i] = EC_POINT_new(group)) == NULL)
			goto err;
		if (!EC_POINT_dbl(group, multiples[i], multiples[i / 2], ctx))
			goto err;
		if ((multiples[i + 1] = EC_POINT_new(group)) == NULL)
			goto err;
		if (!EC_POINT_add(group, multiples[i + 1], multiples[i], point, ctx))
			goto err;
	}

	if ((rr = EC_POINT_new(group)) == NULL)
		goto err;
	if ((t = EC_POINT_new(group)) == NULL)
		goto err;

	if (!EC_POINT_set_to_infinity(group, rr))
		goto err;

	for (i = 0; i < scalar_len; i++) {
		if (i != 0) {
			if (!EC_POINT_dbl(group, rr, rr, ctx))
				goto err;
			if (!EC_POINT_dbl(group, rr, rr, ctx))
				goto err;
			if (!EC_POINT_dbl(group, rr, rr, ctx))
				goto err;
			if (!EC_POINT_dbl(group, rr, rr, ctx))
				goto err;
		}

		if (!EC_POINT_set_to_infinity(group, t))
			goto err;

		wv = scalar_bytes[i] >> 4;
		for (j = 1; j < 16; j++) {
			conditional = crypto_ct_eq_u8(j, wv);
			ec_point_select(group, t, t, multiples[j - 1], conditional);
		}
		if (!EC_POINT_add(group, rr, rr, t, ctx))
			goto err;

		if (!EC_POINT_dbl(group, rr, rr, ctx))
			goto err;
		if (!EC_POINT_dbl(group, rr, rr, ctx))
			goto err;
		if (!EC_POINT_dbl(group, rr, rr, ctx))
			goto err;
		if (!EC_POINT_dbl(group, rr, rr, ctx))
			goto err;

		if (!EC_POINT_set_to_infinity(group, t))
			goto err;

		wv = scalar_bytes[i] & 0xf;
		for (j = 1; j < 16; j++) {
			conditional = crypto_ct_eq_u8(j, wv);
			ec_point_select(group, t, t, multiples[j - 1], conditional);
		}
		if (!EC_POINT_add(group, rr, rr, t, ctx))
			goto err;
	}

	if (!EC_POINT_copy(r, rr))
		goto err;

	ret = 1;

 err:
	for (i = 0; i < 15; i++)
		EC_POINT_free(multiples[i]);

	EC_POINT_free(rr);
	EC_POINT_free(t);

	freezero(scalar_bytes, scalar_len);

	BN_CTX_end(ctx);

	return ret;
}

static int
ec_mul_single_ct(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar,
    const EC_POINT *point, BN_CTX *ctx)
{
	return ec_mul(group, r, scalar, point, ctx);
}

static int
ec_mul_double_nonct(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar1,
    const EC_POINT *point1, const BIGNUM *scalar2, const EC_POINT *point2,
    BN_CTX *ctx)
{
	return ec_wnaf_mul(group, r, scalar1, point1, scalar2, point2, ctx);
}

static const EC_METHOD ec_GFp_homogeneous_projective_method = {
	.group_set_curve = ec_group_set_curve,
	.group_get_curve = ec_group_get_curve,
	.point_set_to_infinity = ec_point_set_to_infinity,
	.point_is_at_infinity = ec_point_is_at_infinity,
	.point_set_affine_coordinates = ec_point_set_affine_coordinates,
	.point_get_affine_coordinates = ec_point_get_affine_coordinates,
	.add = ec_point_add,
	.dbl = ec_point_dbl,
	.invert = ec_point_invert,
	.point_is_on_curve = ec_point_is_on_curve,
	.point_cmp = ec_point_cmp,
	.points_make_affine = ec_points_make_affine,
	.mul_single_ct = ec_mul_single_ct,
	.mul_double_nonct = ec_mul_double_nonct,
};

const EC_METHOD *
EC_GFp_homogeneous_projective_method(void)
{
	return &ec_GFp_homogeneous_projective_method;
}
