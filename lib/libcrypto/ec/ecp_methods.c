/* $OpenBSD: ecp_methods.c,v 1.47 2025/05/24 08:25:58 jsing Exp $ */
/* Includes code written by Lenka Fibikova <fibikova@exp-math.uni-essen.de>
 * for the OpenSSL project.
 * Includes code written by Bodo Moeller for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 * Portions of this software developed by SUN MICROSYSTEMS, INC.,
 * and contributed to the OpenSSL project.
 */

#include <stdlib.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/objects.h>

#include "bn_local.h"
#include "ec_local.h"
#include "err_local.h"

/*
 * Most method functions in this file are designed to work with non-trivial
 * representations of field elements if necessary: while standard modular
 * addition and subtraction are used, the field_mul and field_sqr methods will
 * be used for multiplication, and field_encode and field_decode (if defined)
 * will be used for converting between representations.
 *
 * The functions ec_points_make_affine() and ec_point_get_affine_coordinates()
 * assume that if a non-trivial representation is used, it is a Montgomery
 * representation (i.e. 'encoding' means multiplying by some factor R).
 */

static inline int
ec_field_mul(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    BN_CTX *ctx)
{
	return group->meth->field_mul(group, r, a, b, ctx);
}

static inline int
ec_field_sqr(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a, BN_CTX *ctx)
{
	return group->meth->field_sqr(group, r, a, ctx);
}

static int
ec_decode_scalar(const EC_GROUP *group, BIGNUM *bn, const BIGNUM *x, BN_CTX *ctx)
{
	if (bn == NULL)
		return 1;

	if (group->meth->field_decode != NULL)
		return group->meth->field_decode(group, bn, x, ctx);

	return bn_copy(bn, x);
}

static int
ec_encode_scalar(const EC_GROUP *group, BIGNUM *bn, const BIGNUM *x, BN_CTX *ctx)
{
	if (!BN_nnmod(bn, x, group->p, ctx))
		return 0;

	if (group->meth->field_encode != NULL)
		return group->meth->field_encode(group, bn, bn, ctx);

	return 1;
}

static int
ec_group_set_curve(EC_GROUP *group,
    const BIGNUM *p, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx)
{
	BIGNUM *a_plus_3;
	int ret = 0;

	/* p must be a prime > 3 */
	if (BN_num_bits(p) <= 2 || !BN_is_odd(p)) {
		ECerror(EC_R_INVALID_FIELD);
		return 0;
	}

	BN_CTX_start(ctx);

	if ((a_plus_3 = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!bn_copy(group->p, p))
		goto err;
	BN_set_negative(group->p, 0);

	if (!ec_encode_scalar(group, group->a, a, ctx))
		goto err;
	if (!ec_encode_scalar(group, group->b, b, ctx))
		goto err;

	if (!BN_set_word(a_plus_3, 3))
		goto err;
	if (!BN_mod_add(a_plus_3, a_plus_3, a, group->p, ctx))
		goto err;

	group->a_is_minus3 = BN_is_zero(a_plus_3);

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_group_get_curve(const EC_GROUP *group, BIGNUM *p, BIGNUM *a, BIGNUM *b,
    BN_CTX *ctx)
{
	if (p != NULL) {
		if (!bn_copy(p, group->p))
			return 0;
	}
	if (!ec_decode_scalar(group, a, group->a, ctx))
		return 0;
	if (!ec_decode_scalar(group, b, group->b, ctx))
		return 0;

	return 1;
}

static int
ec_point_set_to_infinity(const EC_GROUP *group, EC_POINT *point)
{
	BN_zero(point->Z);
	point->Z_is_one = 0;

	return 1;
}

static int
ec_point_is_at_infinity(const EC_GROUP *group, const EC_POINT *point)
{
	return BN_is_zero(point->Z);
}

static int
ec_point_is_on_curve(const EC_GROUP *group, const EC_POINT *point, BN_CTX *ctx)
{
	BIGNUM *rh, *tmp, *Z4, *Z6;
	int ret = -1;

	if (EC_POINT_is_at_infinity(group, point))
		return 1;

	BN_CTX_start(ctx);

	if ((rh = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Z4 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Z6 = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * The curve is defined by a Weierstrass equation y^2 = x^3 + a*x + b.
	 * The point is given in Jacobian projective coordinates where (X, Y, Z)
	 * represents (x, y) = (X/Z^2, Y/Z^3). Substituting this and multiplying
	 * by Z^6 transforms the above into Y^2 = X^3 + a*X*Z^4 + b*Z^6.
	 */

	/* rh := X^2 */
	if (!ec_field_sqr(group, rh, point->X, ctx))
		goto err;

	if (!point->Z_is_one) {
		if (!ec_field_sqr(group, tmp, point->Z, ctx))
			goto err;
		if (!ec_field_sqr(group, Z4, tmp, ctx))
			goto err;
		if (!ec_field_mul(group, Z6, Z4, tmp, ctx))
			goto err;

		/* rh := (rh + a*Z^4)*X */
		if (group->a_is_minus3) {
			if (!BN_mod_lshift1_quick(tmp, Z4, group->p))
				goto err;
			if (!BN_mod_add_quick(tmp, tmp, Z4, group->p))
				goto err;
			if (!BN_mod_sub_quick(rh, rh, tmp, group->p))
				goto err;
			if (!ec_field_mul(group, rh, rh, point->X, ctx))
				goto err;
		} else {
			if (!ec_field_mul(group, tmp, Z4, group->a, ctx))
				goto err;
			if (!BN_mod_add_quick(rh, rh, tmp, group->p))
				goto err;
			if (!ec_field_mul(group, rh, rh, point->X, ctx))
				goto err;
		}

		/* rh := rh + b*Z^6 */
		if (!ec_field_mul(group, tmp, group->b, Z6, ctx))
			goto err;
		if (!BN_mod_add_quick(rh, rh, tmp, group->p))
			goto err;
	} else {
		/* point->Z_is_one */

		/* rh := (rh + a)*X */
		if (!BN_mod_add_quick(rh, rh, group->a, group->p))
			goto err;
		if (!ec_field_mul(group, rh, rh, point->X, ctx))
			goto err;
		/* rh := rh + b */
		if (!BN_mod_add_quick(rh, rh, group->b, group->p))
			goto err;
	}

	/* 'lh' := Y^2 */
	if (!ec_field_sqr(group, tmp, point->Y, ctx))
		goto err;

	ret = (0 == BN_ucmp(tmp, rh));

 err:
	BN_CTX_end(ctx);

	return ret;
}

/*
 * Returns -1 on error, 0 if the points are equal, 1 if the points are distinct.
 */

static int
ec_point_cmp(const EC_GROUP *group, const EC_POINT *a, const EC_POINT *b,
    BN_CTX *ctx)
{
	BIGNUM *tmp1, *tmp2, *Za23, *Zb23;
	const BIGNUM *tmp1_, *tmp2_;
	int ret = -1;

	if (EC_POINT_is_at_infinity(group, a) && EC_POINT_is_at_infinity(group, b))
		return 0;
	if (EC_POINT_is_at_infinity(group, a) || EC_POINT_is_at_infinity(group, b))
		return 1;

	if (a->Z_is_one && b->Z_is_one)
		return BN_cmp(a->X, b->X) != 0 || BN_cmp(a->Y, b->Y) != 0;

	BN_CTX_start(ctx);

	if ((tmp1 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((tmp2 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((Za23 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((Zb23 = BN_CTX_get(ctx)) == NULL)
		goto end;

	/*
	 * Decide whether (X_a/Z_a^2, Y_a/Z_a^3) = (X_b/Z_b^2, Y_b/Z_b^3), or
	 * equivalently, (X_a*Z_b^2, Y_a*Z_b^3) = (X_b*Z_a^2, Y_b*Z_a^3).
	 */

	if (!b->Z_is_one) {
		if (!ec_field_sqr(group, Zb23, b->Z, ctx))
			goto end;
		if (!ec_field_mul(group, tmp1, a->X, Zb23, ctx))
			goto end;
		tmp1_ = tmp1;
	} else
		tmp1_ = a->X;
	if (!a->Z_is_one) {
		if (!ec_field_sqr(group, Za23, a->Z, ctx))
			goto end;
		if (!ec_field_mul(group, tmp2, b->X, Za23, ctx))
			goto end;
		tmp2_ = tmp2;
	} else
		tmp2_ = b->X;

	/* compare  X_a*Z_b^2  with  X_b*Z_a^2 */
	if (BN_cmp(tmp1_, tmp2_) != 0) {
		ret = 1;	/* points differ */
		goto end;
	}
	if (!b->Z_is_one) {
		if (!ec_field_mul(group, Zb23, Zb23, b->Z, ctx))
			goto end;
		if (!ec_field_mul(group, tmp1, a->Y, Zb23, ctx))
			goto end;
		/* tmp1_ = tmp1 */
	} else
		tmp1_ = a->Y;
	if (!a->Z_is_one) {
		if (!ec_field_mul(group, Za23, Za23, a->Z, ctx))
			goto end;
		if (!ec_field_mul(group, tmp2, b->Y, Za23, ctx))
			goto end;
		/* tmp2_ = tmp2 */
	} else
		tmp2_ = b->Y;

	/* compare  Y_a*Z_b^3  with  Y_b*Z_a^3 */
	if (BN_cmp(tmp1_, tmp2_) != 0) {
		ret = 1;	/* points differ */
		goto end;
	}
	/* points are equal */
	ret = 0;

 end:
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_point_set_affine_coordinates(const EC_GROUP *group, EC_POINT *point,
    const BIGNUM *x, const BIGNUM *y, BN_CTX *ctx)
{
	int ret = 0;

	if (x == NULL || y == NULL) {
		ECerror(ERR_R_PASSED_NULL_PARAMETER);
		goto err;
	}

	if (!ec_encode_scalar(group, point->X, x, ctx))
		goto err;
	if (!ec_encode_scalar(group, point->Y, y, ctx))
		goto err;
	if (!ec_encode_scalar(group, point->Z, BN_value_one(), ctx))
		goto err;
	point->Z_is_one = 1;

	ret = 1;

 err:
	return ret;
}

static int
ec_point_get_affine_coordinates(const EC_GROUP *group, const EC_POINT *point,
    BIGNUM *x, BIGNUM *y, BN_CTX *ctx)
{
	BIGNUM *z, *Z, *Z_1, *Z_2, *Z_3;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((z = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Z = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Z_1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Z_2 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Z_3 = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * Convert from Jacobian projective coordinates (X, Y, Z) into
	 * (X/Z^2, Y/Z^3).
	 */

	if (!ec_decode_scalar(group, z, point->Z, ctx))
		goto err;

	if (BN_is_one(z)) {
		if (!ec_decode_scalar(group, x, point->X, ctx))
			goto err;
		if (!ec_decode_scalar(group, y, point->Y, ctx))
			goto err;
		goto done;
	}

	if (BN_mod_inverse_ct(Z_1, z, group->p, ctx) == NULL) {
		ECerror(ERR_R_BN_LIB);
		goto err;
	}
	if (group->meth->field_encode == NULL) {
		/* field_sqr works on standard representation */
		if (!ec_field_sqr(group, Z_2, Z_1, ctx))
			goto err;
	} else {
		if (!BN_mod_sqr(Z_2, Z_1, group->p, ctx))
			goto err;
	}

	if (x != NULL) {
		/*
		 * in the Montgomery case, field_mul will cancel out
		 * Montgomery factor in X:
		 */
		if (!ec_field_mul(group, x, point->X, Z_2, ctx))
			goto err;
	}
	if (y != NULL) {
		if (group->meth->field_encode == NULL) {
			/* field_mul works on standard representation */
			if (!ec_field_mul(group, Z_3, Z_2, Z_1, ctx))
				goto err;
		} else {
			if (!BN_mod_mul(Z_3, Z_2, Z_1, group->p, ctx))
				goto err;
		}

		/*
		 * in the Montgomery case, field_mul will cancel out
		 * Montgomery factor in Y:
		 */
		if (!ec_field_mul(group, y, point->Y, Z_3, ctx))
			goto err;
	}

 done:
	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_points_make_affine(const EC_GROUP *group, size_t num, EC_POINT **points,
    BN_CTX *ctx)
{
	BIGNUM **prod_Z = NULL;
	BIGNUM *one, *tmp, *tmp_Z;
	size_t i;
	int ret = 0;

	if (num == 0)
		return 1;

	BN_CTX_start(ctx);

	if ((one = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((tmp_Z = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!ec_encode_scalar(group, one, BN_value_one(), ctx))
		goto err;

	if ((prod_Z = calloc(num, sizeof *prod_Z)) == NULL)
		goto err;
	for (i = 0; i < num; i++) {
		if ((prod_Z[i] = BN_CTX_get(ctx)) == NULL)
			goto err;
	}

	/*
	 * Set prod_Z[i] to the product of points[0]->Z, ..., points[i]->Z,
	 * skipping any zero-valued inputs (pretend that they're 1).
	 */

	if (!BN_is_zero(points[0]->Z)) {
		if (!bn_copy(prod_Z[0], points[0]->Z))
			goto err;
	} else {
		if (!bn_copy(prod_Z[0], one))
			goto err;
	}

	for (i = 1; i < num; i++) {
		if (!BN_is_zero(points[i]->Z)) {
			if (!ec_field_mul(group, prod_Z[i],
			    prod_Z[i - 1], points[i]->Z, ctx))
				goto err;
		} else {
			if (!bn_copy(prod_Z[i], prod_Z[i - 1]))
				goto err;
		}
	}

	/*
	 * Now use a single explicit inversion to replace every non-zero
	 * points[i]->Z by its inverse.
	 */
	if (!BN_mod_inverse_nonct(tmp, prod_Z[num - 1], group->p, ctx)) {
		ECerror(ERR_R_BN_LIB);
		goto err;
	}

	if (group->meth->field_encode != NULL) {
		/*
		 * In the Montgomery case we just turned R*H (representing H)
		 * into 1/(R*H), but we need R*(1/H) (representing 1/H); i.e.,
		 * we need to multiply by the Montgomery factor twice.
		 */
		if (!group->meth->field_encode(group, tmp, tmp, ctx))
			goto err;
		if (!group->meth->field_encode(group, tmp, tmp, ctx))
			goto err;
	}

	for (i = num - 1; i > 0; i--) {
		/*
		 * Loop invariant: tmp is the product of the inverses of
		 * points[0]->Z, ..., points[i]->Z (zero-valued inputs skipped).
		 */
		if (BN_is_zero(points[i]->Z))
			continue;

		/* Set tmp_Z to the inverse of points[i]->Z. */
		if (!ec_field_mul(group, tmp_Z, prod_Z[i - 1], tmp, ctx))
			goto err;
		/* Adjust tmp to satisfy loop invariant. */
		if (!ec_field_mul(group, tmp, tmp, points[i]->Z, ctx))
			goto err;
		/* Replace points[i]->Z by its inverse. */
		if (!bn_copy(points[i]->Z, tmp_Z))
			goto err;
	}

	if (!BN_is_zero(points[0]->Z)) {
		/* Replace points[0]->Z by its inverse. */
		if (!bn_copy(points[0]->Z, tmp))
			goto err;
	}

	/* Finally, fix up the X and Y coordinates for all points. */
	for (i = 0; i < num; i++) {
		EC_POINT *p = points[i];

		if (BN_is_zero(p->Z))
			continue;

		/* turn  (X, Y, 1/Z)  into  (X/Z^2, Y/Z^3, 1) */

		if (!ec_field_sqr(group, tmp, p->Z, ctx))
			goto err;
		if (!ec_field_mul(group, p->X, p->X, tmp, ctx))
			goto err;

		if (!ec_field_mul(group, tmp, tmp, p->Z, ctx))
			goto err;
		if (!ec_field_mul(group, p->Y, p->Y, tmp, ctx))
			goto err;

		if (!bn_copy(p->Z, one))
			goto err;
		p->Z_is_one = 1;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);
	free(prod_Z);

	return ret;
}

static int
ec_add(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a, const EC_POINT *b,
    BN_CTX *ctx)
{
	BIGNUM *n0, *n1, *n2, *n3, *n4, *n5, *n6;
	int ret = 0;

	if (a == b)
		return EC_POINT_dbl(group, r, a, ctx);
	if (EC_POINT_is_at_infinity(group, a))
		return EC_POINT_copy(r, b);
	if (EC_POINT_is_at_infinity(group, b))
		return EC_POINT_copy(r, a);

	BN_CTX_start(ctx);

	if ((n0 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((n1 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((n2 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((n3 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((n4 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((n5 = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((n6 = BN_CTX_get(ctx)) == NULL)
		goto end;

	/*
	 * Note that in this function we must not read components of 'a' or
	 * 'b' once we have written the corresponding components of 'r'. ('r'
	 * might be one of 'a' or 'b'.)
	 */

	/* n1, n2 */
	if (b->Z_is_one) {
		if (!bn_copy(n1, a->X))
			goto end;
		if (!bn_copy(n2, a->Y))
			goto end;
		/* n1 = X_a */
		/* n2 = Y_a */
	} else {
		if (!ec_field_sqr(group, n0, b->Z, ctx))
			goto end;
		if (!ec_field_mul(group, n1, a->X, n0, ctx))
			goto end;
		/* n1 = X_a * Z_b^2 */

		if (!ec_field_mul(group, n0, n0, b->Z, ctx))
			goto end;
		if (!ec_field_mul(group, n2, a->Y, n0, ctx))
			goto end;
		/* n2 = Y_a * Z_b^3 */
	}

	/* n3, n4 */
	if (a->Z_is_one) {
		if (!bn_copy(n3, b->X))
			goto end;
		if (!bn_copy(n4, b->Y))
			goto end;
		/* n3 = X_b */
		/* n4 = Y_b */
	} else {
		if (!ec_field_sqr(group, n0, a->Z, ctx))
			goto end;
		if (!ec_field_mul(group, n3, b->X, n0, ctx))
			goto end;
		/* n3 = X_b * Z_a^2 */

		if (!ec_field_mul(group, n0, n0, a->Z, ctx))
			goto end;
		if (!ec_field_mul(group, n4, b->Y, n0, ctx))
			goto end;
		/* n4 = Y_b * Z_a^3 */
	}

	/* n5, n6 */
	if (!BN_mod_sub_quick(n5, n1, n3, group->p))
		goto end;
	if (!BN_mod_sub_quick(n6, n2, n4, group->p))
		goto end;
	/* n5 = n1 - n3 */
	/* n6 = n2 - n4 */

	if (BN_is_zero(n5)) {
		if (BN_is_zero(n6)) {
			/* a is the same point as b */
			BN_CTX_end(ctx);
			ret = EC_POINT_dbl(group, r, a, ctx);
			ctx = NULL;
			goto end;
		} else {
			/* a is the inverse of b */
			BN_zero(r->Z);
			r->Z_is_one = 0;
			ret = 1;
			goto end;
		}
	}
	/* 'n7', 'n8' */
	if (!BN_mod_add_quick(n1, n1, n3, group->p))
		goto end;
	if (!BN_mod_add_quick(n2, n2, n4, group->p))
		goto end;
	/* 'n7' = n1 + n3 */
	/* 'n8' = n2 + n4 */

	/* Z_r */
	if (a->Z_is_one && b->Z_is_one) {
		if (!bn_copy(r->Z, n5))
			goto end;
	} else {
		if (a->Z_is_one) {
			if (!bn_copy(n0, b->Z))
				goto end;
		} else if (b->Z_is_one) {
			if (!bn_copy(n0, a->Z))
				goto end;
		} else {
			if (!ec_field_mul(group, n0, a->Z, b->Z, ctx))
				goto end;
		}
		if (!ec_field_mul(group, r->Z, n0, n5, ctx))
			goto end;
	}
	r->Z_is_one = 0;
	/* Z_r = Z_a * Z_b * n5 */

	/* X_r */
	if (!ec_field_sqr(group, n0, n6, ctx))
		goto end;
	if (!ec_field_sqr(group, n4, n5, ctx))
		goto end;
	if (!ec_field_mul(group, n3, n1, n4, ctx))
		goto end;
	if (!BN_mod_sub_quick(r->X, n0, n3, group->p))
		goto end;
	/* X_r = n6^2 - n5^2 * 'n7' */

	/* 'n9' */
	if (!BN_mod_lshift1_quick(n0, r->X, group->p))
		goto end;
	if (!BN_mod_sub_quick(n0, n3, n0, group->p))
		goto end;
	/* n9 = n5^2 * 'n7' - 2 * X_r */

	/* Y_r */
	if (!ec_field_mul(group, n0, n0, n6, ctx))
		goto end;
	if (!ec_field_mul(group, n5, n4, n5, ctx))
		goto end;	/* now n5 is n5^3 */
	if (!ec_field_mul(group, n1, n2, n5, ctx))
		goto end;
	if (!BN_mod_sub_quick(n0, n0, n1, group->p))
		goto end;
	if (BN_is_odd(n0))
		if (!BN_add(n0, n0, group->p))
			goto end;
	/* now  0 <= n0 < 2*p,  and n0 is even */
	if (!BN_rshift1(r->Y, n0))
		goto end;
	/* Y_r = (n6 * 'n9' - 'n8' * 'n5^3') / 2 */

	ret = 1;

 end:
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_dbl(const EC_GROUP *group, EC_POINT *r, const EC_POINT *a, BN_CTX *ctx)
{
	BIGNUM *n0, *n1, *n2, *n3;
	int ret = 0;

	if (EC_POINT_is_at_infinity(group, a))
		return EC_POINT_set_to_infinity(group, r);

	BN_CTX_start(ctx);

	if ((n0 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((n1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((n2 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((n3 = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * Note that in this function we must not read components of 'a' once
	 * we have written the corresponding components of 'r'. ('r' might
	 * the same as 'a'.)
	 */

	/* n1 */
	if (a->Z_is_one) {
		if (!ec_field_sqr(group, n0, a->X, ctx))
			goto err;
		if (!BN_mod_lshift1_quick(n1, n0, group->p))
			goto err;
		if (!BN_mod_add_quick(n0, n0, n1, group->p))
			goto err;
		if (!BN_mod_add_quick(n1, n0, group->a, group->p))
			goto err;
		/* n1 = 3 * X_a^2 + a_curve */
	} else if (group->a_is_minus3) {
		if (!ec_field_sqr(group, n1, a->Z, ctx))
			goto err;
		if (!BN_mod_add_quick(n0, a->X, n1, group->p))
			goto err;
		if (!BN_mod_sub_quick(n2, a->X, n1, group->p))
			goto err;
		if (!ec_field_mul(group, n1, n0, n2, ctx))
			goto err;
		if (!BN_mod_lshift1_quick(n0, n1, group->p))
			goto err;
		if (!BN_mod_add_quick(n1, n0, n1, group->p))
			goto err;
		/*
		 * n1 = 3 * (X_a + Z_a^2) * (X_a - Z_a^2) = 3 * X_a^2 - 3 *
		 * Z_a^4
		 */
	} else {
		if (!ec_field_sqr(group, n0, a->X, ctx))
			goto err;
		if (!BN_mod_lshift1_quick(n1, n0, group->p))
			goto err;
		if (!BN_mod_add_quick(n0, n0, n1, group->p))
			goto err;
		if (!ec_field_sqr(group, n1, a->Z, ctx))
			goto err;
		if (!ec_field_sqr(group, n1, n1, ctx))
			goto err;
		if (!ec_field_mul(group, n1, n1, group->a, ctx))
			goto err;
		if (!BN_mod_add_quick(n1, n1, n0, group->p))
			goto err;
		/* n1 = 3 * X_a^2 + a_curve * Z_a^4 */
	}

	/* Z_r */
	if (a->Z_is_one) {
		if (!bn_copy(n0, a->Y))
			goto err;
	} else {
		if (!ec_field_mul(group, n0, a->Y, a->Z, ctx))
			goto err;
	}
	if (!BN_mod_lshift1_quick(r->Z, n0, group->p))
		goto err;
	r->Z_is_one = 0;
	/* Z_r = 2 * Y_a * Z_a */

	/* n2 */
	if (!ec_field_sqr(group, n3, a->Y, ctx))
		goto err;
	if (!ec_field_mul(group, n2, a->X, n3, ctx))
		goto err;
	if (!BN_mod_lshift_quick(n2, n2, 2, group->p))
		goto err;
	/* n2 = 4 * X_a * Y_a^2 */

	/* X_r */
	if (!BN_mod_lshift1_quick(n0, n2, group->p))
		goto err;
	if (!ec_field_sqr(group, r->X, n1, ctx))
		goto err;
	if (!BN_mod_sub_quick(r->X, r->X, n0, group->p))
		goto err;
	/* X_r = n1^2 - 2 * n2 */

	/* n3 */
	if (!ec_field_sqr(group, n0, n3, ctx))
		goto err;
	if (!BN_mod_lshift_quick(n3, n0, 3, group->p))
		goto err;
	/* n3 = 8 * Y_a^4 */

	/* Y_r */
	if (!BN_mod_sub_quick(n0, n2, r->X, group->p))
		goto err;
	if (!ec_field_mul(group, n0, n1, n0, ctx))
		goto err;
	if (!BN_mod_sub_quick(r->Y, n0, n3, group->p))
		goto err;
	/* Y_r = n1 * (n2 - X_r) - n3 */

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_invert(const EC_GROUP *group, EC_POINT *point, BN_CTX *ctx)
{
	if (EC_POINT_is_at_infinity(group, point) || BN_is_zero(point->Y))
		/* point is its own inverse */
		return 1;

	return BN_usub(point->Y, group->p, point->Y);
}

/*
 * Apply randomization of EC point Jacobian projective coordinates:
 *
 *	(X, Y, Z) = (lambda^2 * X, lambda^3 * Y, lambda * Z)
 *
 * where lambda is in the interval [1, p).
 */
static int
ec_blind_coordinates(const EC_GROUP *group, EC_POINT *p, BN_CTX *ctx)
{
	BIGNUM *lambda = NULL;
	BIGNUM *tmp = NULL;
	int ret = 0;

	BN_CTX_start(ctx);
	if ((lambda = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* Generate lambda in [1, p). */
	if (!bn_rand_interval(lambda, 1, group->p))
		goto err;

	if (group->meth->field_encode != NULL &&
	    !group->meth->field_encode(group, lambda, lambda, ctx))
		goto err;

	/* Z = lambda * Z */
	if (!ec_field_mul(group, p->Z, lambda, p->Z, ctx))
		goto err;

	/* tmp = lambda^2 */
	if (!ec_field_sqr(group, tmp, lambda, ctx))
		goto err;

	/* X = lambda^2 * X */
	if (!ec_field_mul(group, p->X, tmp, p->X, ctx))
		goto err;

	/* tmp = lambda^3 */
	if (!ec_field_mul(group, tmp, tmp, lambda, ctx))
		goto err;

	/* Y = lambda^3 * Y */
	if (!ec_field_mul(group, p->Y, tmp, p->Y, ctx))
		goto err;

	/* Disable optimized arithmetics after replacing Z by lambda * Z. */
	p->Z_is_one = 0;

	ret = 1;

 err:
	BN_CTX_end(ctx);
	return ret;
}

#define EC_POINT_BN_set_flags(P, flags) do {				\
	BN_set_flags((P)->X, (flags));					\
	BN_set_flags((P)->Y, (flags));					\
	BN_set_flags((P)->Z, (flags));					\
} while(0)

#define EC_POINT_CSWAP(c, a, b, w, t) do {				\
	if (!BN_swap_ct(c, (a)->X, (b)->X, w)	||			\
	    !BN_swap_ct(c, (a)->Y, (b)->Y, w)	||			\
	    !BN_swap_ct(c, (a)->Z, (b)->Z, w))				\
		goto err;						\
	t = ((a)->Z_is_one ^ (b)->Z_is_one) & (c);			\
	(a)->Z_is_one ^= (t);						\
	(b)->Z_is_one ^= (t);						\
} while(0)

/*
 * This function computes (in constant time) a point multiplication over the
 * EC group.
 *
 * At a high level, it is Montgomery ladder with conditional swaps.
 *
 * It performs either a fixed point multiplication
 *          (scalar * generator)
 * when point is NULL, or a variable point multiplication
 *          (scalar * point)
 * when point is not NULL.
 *
 * scalar should be in the range [0,n) otherwise all constant time bets are off.
 *
 * NB: This says nothing about EC_POINT_add and EC_POINT_dbl,
 * which of course are not constant time themselves.
 *
 * The product is stored in r.
 *
 * Returns 1 on success, 0 otherwise.
 */
static int
ec_mul_ct(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar,
    const EC_POINT *point, BN_CTX *ctx)
{
	int i, cardinality_bits, group_top, kbit, pbit, Z_is_one;
	EC_POINT *s = NULL;
	BIGNUM *k = NULL;
	BIGNUM *lambda = NULL;
	BIGNUM *cardinality = NULL;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((s = EC_POINT_dup(point, group)) == NULL)
		goto err;

	EC_POINT_BN_set_flags(s, BN_FLG_CONSTTIME);

	if ((cardinality = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((lambda = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((k = BN_CTX_get(ctx)) == NULL)
		goto err;
	if (!BN_mul(cardinality, group->order, group->cofactor, ctx))
		goto err;

	/*
	 * Group cardinalities are often on a word boundary.
	 * So when we pad the scalar, some timing diff might
	 * pop if it needs to be expanded due to carries.
	 * So expand ahead of time.
	 */
	cardinality_bits = BN_num_bits(cardinality);
	group_top = cardinality->top;
	if (!bn_wexpand(k, group_top + 2) ||
	    !bn_wexpand(lambda, group_top + 2))
		goto err;

	if (!bn_copy(k, scalar))
		goto err;

	BN_set_flags(k, BN_FLG_CONSTTIME);

	if (BN_num_bits(k) > cardinality_bits || BN_is_negative(k)) {
		/*
		 * This is an unusual input, and we don't guarantee
		 * constant-timeness
		 */
		if (!BN_nnmod(k, k, cardinality, ctx))
			goto err;
	}

	if (!BN_add(lambda, k, cardinality))
		goto err;
	BN_set_flags(lambda, BN_FLG_CONSTTIME);
	if (!BN_add(k, lambda, cardinality))
		goto err;
	/*
	 * lambda := scalar + cardinality
	 * k := scalar + 2*cardinality
	 */
	kbit = BN_is_bit_set(lambda, cardinality_bits);
	if (!BN_swap_ct(kbit, k, lambda, group_top + 2))
		goto err;

	group_top = group->p->top;
	if (!bn_wexpand(s->X, group_top) ||
	    !bn_wexpand(s->Y, group_top) ||
	    !bn_wexpand(s->Z, group_top) ||
	    !bn_wexpand(r->X, group_top) ||
	    !bn_wexpand(r->Y, group_top) ||
	    !bn_wexpand(r->Z, group_top))
		goto err;

	/*
	 * Apply coordinate blinding for EC_POINT if the underlying EC_METHOD
	 * implements it.
	 */
	if (!ec_blind_coordinates(group, s, ctx))
		goto err;

	/* top bit is a 1, in a fixed pos */
	if (!EC_POINT_copy(r, s))
		goto err;

	EC_POINT_BN_set_flags(r, BN_FLG_CONSTTIME);

	if (!EC_POINT_dbl(group, s, s, ctx))
		goto err;

	pbit = 0;

	/*
	 * The ladder step, with branches, is
	 *
	 * k[i] == 0: S = add(R, S), R = dbl(R)
	 * k[i] == 1: R = add(S, R), S = dbl(S)
	 *
	 * Swapping R, S conditionally on k[i] leaves you with state
	 *
	 * k[i] == 0: T, U = R, S
	 * k[i] == 1: T, U = S, R
	 *
	 * Then perform the ECC ops.
	 *
	 * U = add(T, U)
	 * T = dbl(T)
	 *
	 * Which leaves you with state
	 *
	 * k[i] == 0: U = add(R, S), T = dbl(R)
	 * k[i] == 1: U = add(S, R), T = dbl(S)
	 *
	 * Swapping T, U conditionally on k[i] leaves you with state
	 *
	 * k[i] == 0: R, S = T, U
	 * k[i] == 1: R, S = U, T
	 *
	 * Which leaves you with state
	 *
	 * k[i] == 0: S = add(R, S), R = dbl(R)
	 * k[i] == 1: R = add(S, R), S = dbl(S)
	 *
	 * So we get the same logic, but instead of a branch it's a
	 * conditional swap, followed by ECC ops, then another conditional swap.
	 *
	 * Optimization: The end of iteration i and start of i-1 looks like
	 *
	 * ...
	 * CSWAP(k[i], R, S)
	 * ECC
	 * CSWAP(k[i], R, S)
	 * (next iteration)
	 * CSWAP(k[i-1], R, S)
	 * ECC
	 * CSWAP(k[i-1], R, S)
	 * ...
	 *
	 * So instead of two contiguous swaps, you can merge the condition
	 * bits and do a single swap.
	 *
	 * k[i]   k[i-1]    Outcome
	 * 0      0         No Swap
	 * 0      1         Swap
	 * 1      0         Swap
	 * 1      1         No Swap
	 *
	 * This is XOR. pbit tracks the previous bit of k.
	 */

	for (i = cardinality_bits - 1; i >= 0; i--) {
		kbit = BN_is_bit_set(k, i) ^ pbit;
		EC_POINT_CSWAP(kbit, r, s, group_top, Z_is_one);
		if (!EC_POINT_add(group, s, r, s, ctx))
			goto err;
		if (!EC_POINT_dbl(group, r, r, ctx))
			goto err;
		/*
		 * pbit logic merges this cswap with that of the
		 * next iteration
		 */
		pbit ^= kbit;
	}
	/* one final cswap to move the right value into r */
	EC_POINT_CSWAP(pbit, r, s, group_top, Z_is_one);

	ret = 1;

 err:
	EC_POINT_free(s);
	BN_CTX_end(ctx);

	return ret;
}

#undef EC_POINT_BN_set_flags
#undef EC_POINT_CSWAP

static int
ec_mul_single_ct(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar,
    const EC_POINT *point, BN_CTX *ctx)
{
	return ec_mul_ct(group, r, scalar, point, ctx);
}

static int
ec_mul_double_nonct(const EC_GROUP *group, EC_POINT *r, const BIGNUM *scalar1,
    const EC_POINT *point1, const BIGNUM *scalar2, const EC_POINT *point2,
    BN_CTX *ctx)
{
	return ec_wnaf_mul(group, r, scalar1, point1, scalar2, point2, ctx);
}

static int
ec_simple_field_mul(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx)
{
	return BN_mod_mul(r, a, b, group->p, ctx);
}

static int
ec_simple_field_sqr(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a, BN_CTX *ctx)
{
	return BN_mod_sqr(r, a, group->p, ctx);
}

static int
ec_mont_group_set_curve(EC_GROUP *group, const BIGNUM *p, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx)
{
	BN_MONT_CTX_free(group->mont_ctx);
	if ((group->mont_ctx = BN_MONT_CTX_create(p, ctx)) == NULL)
		goto err;

	if (!ec_group_set_curve(group, p, a, b, ctx))
		goto err;

	return 1;

 err:
	BN_MONT_CTX_free(group->mont_ctx);
	group->mont_ctx = NULL;

	return 0;
}

static int
ec_mont_field_mul(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
    const BIGNUM *b, BN_CTX *ctx)
{
	if (group->mont_ctx == NULL) {
		ECerror(EC_R_NOT_INITIALIZED);
		return 0;
	}
	return BN_mod_mul_montgomery(r, a, b, group->mont_ctx, ctx);
}

static int
ec_mont_field_sqr(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
    BN_CTX *ctx)
{
	if (group->mont_ctx == NULL) {
		ECerror(EC_R_NOT_INITIALIZED);
		return 0;
	}
	return BN_mod_mul_montgomery(r, a, a, group->mont_ctx, ctx);
}

static int
ec_mont_field_encode(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
    BN_CTX *ctx)
{
	if (group->mont_ctx == NULL) {
		ECerror(EC_R_NOT_INITIALIZED);
		return 0;
	}
	return BN_to_montgomery(r, a, group->mont_ctx, ctx);
}

static int
ec_mont_field_decode(const EC_GROUP *group, BIGNUM *r, const BIGNUM *a,
    BN_CTX *ctx)
{
	if (group->mont_ctx == NULL) {
		ECerror(EC_R_NOT_INITIALIZED);
		return 0;
	}
	return BN_from_montgomery(r, a, group->mont_ctx, ctx);
}

static const EC_METHOD ec_GFp_simple_method = {
	.group_set_curve = ec_group_set_curve,
	.group_get_curve = ec_group_get_curve,
	.point_set_to_infinity = ec_point_set_to_infinity,
	.point_is_at_infinity = ec_point_is_at_infinity,
	.point_is_on_curve = ec_point_is_on_curve,
	.point_cmp = ec_point_cmp,
	.point_set_affine_coordinates = ec_point_set_affine_coordinates,
	.point_get_affine_coordinates = ec_point_get_affine_coordinates,
	.points_make_affine = ec_points_make_affine,
	.add = ec_add,
	.dbl = ec_dbl,
	.invert = ec_invert,
	.mul_single_ct = ec_mul_single_ct,
	.mul_double_nonct = ec_mul_double_nonct,
	.field_mul = ec_simple_field_mul,
	.field_sqr = ec_simple_field_sqr,
};

const EC_METHOD *
EC_GFp_simple_method(void)
{
	return &ec_GFp_simple_method;
}

static const EC_METHOD ec_GFp_mont_method = {
	.group_set_curve = ec_mont_group_set_curve,
	.group_get_curve = ec_group_get_curve,
	.point_set_to_infinity = ec_point_set_to_infinity,
	.point_is_at_infinity = ec_point_is_at_infinity,
	.point_is_on_curve = ec_point_is_on_curve,
	.point_cmp = ec_point_cmp,
	.point_set_affine_coordinates = ec_point_set_affine_coordinates,
	.point_get_affine_coordinates = ec_point_get_affine_coordinates,
	.points_make_affine = ec_points_make_affine,
	.add = ec_add,
	.dbl = ec_dbl,
	.invert = ec_invert,
	.mul_single_ct = ec_mul_single_ct,
	.mul_double_nonct = ec_mul_double_nonct,
	.field_mul = ec_mont_field_mul,
	.field_sqr = ec_mont_field_sqr,
	.field_encode = ec_mont_field_encode,
	.field_decode = ec_mont_field_decode,
};

const EC_METHOD *
EC_GFp_mont_method(void)
{
	return &ec_GFp_mont_method;
}
