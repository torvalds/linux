/* $OpenBSD: ec_convert.c,v 1.15 2025/05/10 05:54:38 tb Exp $ */
/*
 * Originally written by Bodo Moeller for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 1998-2003 The OpenSSL Project.  All rights reserved.
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
 * Binary polynomial ECC support in OpenSSL originally developed by
 * SUN MICROSYSTEMS, INC., and contributed to the OpenSSL project.
 */

#include <string.h>

#include <openssl/asn1.h>

#include "asn1_local.h"
#include "ec_local.h"
#include "err_local.h"

/*
 * Internal handling of the point conversion octet
 * (see X9.62, section 4.4.2, SEC 1 section 2.3.3)
 *
 * Only the last three bits of the leading octet of a point should be set.
 * Bits 3 and 2 encode the conversion form for all points except the point
 * at infinity. In compressed and hybrid form bit 1 indicates if the even
 * or the odd solution of the quadratic equation for y should be used.
 *
 * The public point_conversion_t enum lacks the point at infinity, so we
 * ignore it except at the API boundary.
 */

#define EC_POINT_YBIT			0x01

#define EC_POINT_AT_INFINITY		0x00
#define EC_POINT_COMPRESSED		0x02
#define EC_POINT_UNCOMPRESSED		0x04
#define EC_POINT_HYBRID			0x06
#define EC_POINT_CONVERSION_MASK	0x06

static int
ec_conversion_form_is_valid(uint8_t form)
{
	return (form & EC_POINT_CONVERSION_MASK) == form;
}

static int
ec_check_hybrid_ybit_is_consistent(uint8_t form, int ybit, const BIGNUM *y)
{
	if (form == EC_POINT_HYBRID && ybit != BN_is_odd(y)) {
		ECerror(EC_R_INVALID_ENCODING);
		return 0;
	}

	return 1;
}

/* Nonzero y-bit only makes sense with compressed or hybrid encoding. */
static int
ec_nonzero_ybit_allowed(uint8_t form)
{
	return form == EC_POINT_COMPRESSED || form == EC_POINT_HYBRID;
}

static int
ec_add_leading_octet_cbb(CBB *cbb, uint8_t form, int ybit)
{
	if (ec_nonzero_ybit_allowed(form) && ybit != 0)
		form |= EC_POINT_YBIT;

	return CBB_add_u8(cbb, form);
}

static int
ec_get_leading_octet_cbs(CBS *cbs, uint8_t *out_form, int *out_ybit)
{
	uint8_t octet;

	if (!CBS_get_u8(cbs, &octet)) {
		ECerror(EC_R_BUFFER_TOO_SMALL);
		return 0;
	}

	*out_ybit = octet & EC_POINT_YBIT;
	*out_form = octet & ~EC_POINT_YBIT;

	if (!ec_conversion_form_is_valid(*out_form)) {
		ECerror(EC_R_INVALID_ENCODING);
		return 0;
	}

	if (*out_ybit != 0 && !ec_nonzero_ybit_allowed(*out_form)) {
		ECerror(EC_R_INVALID_ENCODING);
		return 0;
	}

	return 1;
}

static int
ec_encoded_length(const EC_GROUP *group, uint8_t form, size_t *out_len)
{
	switch (form) {
	case EC_POINT_AT_INFINITY:
		*out_len = 1;
		return 1;
	case EC_POINT_COMPRESSED:
		*out_len = 1 + BN_num_bytes(group->p);
		return 1;
	case EC_POINT_UNCOMPRESSED:
	case EC_POINT_HYBRID:
		*out_len = 1 + 2 * BN_num_bytes(group->p);
		return 1;
	default:
		return 0;
	}
}

static int
ec_field_element_is_valid(const EC_GROUP *group, const BIGNUM *bn)
{
	/* Ensure bn is in the range [0, p). */
	return !BN_is_negative(bn) && BN_cmp(group->p, bn) > 0;
}

static int
ec_add_field_element_cbb(CBB *cbb, const EC_GROUP *group, const BIGNUM *bn)
{
	uint8_t *buf = NULL;
	int buf_len = BN_num_bytes(group->p);

	if (!ec_field_element_is_valid(group, bn)) {
		ECerror(EC_R_BIGNUM_OUT_OF_RANGE);
		return 0;
	}
	if (!CBB_add_space(cbb, &buf, buf_len)) {
		ECerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	if (BN_bn2binpad(bn, buf, buf_len) != buf_len) {
		ECerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	return 1;
}

static int
ec_get_field_element_cbs(CBS *cbs, const EC_GROUP *group, BIGNUM *bn)
{
	CBS field_element;

	if (!CBS_get_bytes(cbs, &field_element, BN_num_bytes(group->p))) {
		ECerror(EC_R_INVALID_ENCODING);
		return 0;
	}
	if (!BN_bin2bn(CBS_data(&field_element), CBS_len(&field_element), bn)) {
		ECerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	if (!ec_field_element_is_valid(group, bn)) {
		ECerror(EC_R_BIGNUM_OUT_OF_RANGE);
		return 0;
	}

	return 1;
}

static size_t
ec_point2oct(const EC_GROUP *group, const EC_POINT *point, uint8_t form,
    unsigned char *buf, size_t len, BN_CTX *ctx)
{
	CBB cbb;
	BIGNUM *x, *y;
	size_t encoded_length;
	size_t ret = 0;

	if (EC_POINT_is_at_infinity(group, point))
		form = EC_POINT_AT_INFINITY;

	if (!ec_encoded_length(group, form, &encoded_length)) {
		ECerror(EC_R_INVALID_FORM);
		return 0;
	}

	if (buf == NULL)
		return encoded_length;

	if (len < encoded_length) {
		ECerror(EC_R_BUFFER_TOO_SMALL);
		return 0;
	}

	BN_CTX_start(ctx);
	if (!CBB_init_fixed(&cbb, buf, len))
		goto err;

	if (form == EC_POINT_AT_INFINITY) {
		if (!EC_POINT_is_at_infinity(group, point))
			goto err;
		if (!ec_add_leading_octet_cbb(&cbb, form, 0))
			goto err;

		goto done;
	}

	if ((x = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((y = BN_CTX_get(ctx)) == NULL)
		goto err;
	if (!EC_POINT_get_affine_coordinates(group, point, x, y, ctx))
		goto err;

	if (!ec_add_leading_octet_cbb(&cbb, form, BN_is_odd(y)))
		goto err;

	if (form == EC_POINT_COMPRESSED) {
		if (!ec_add_field_element_cbb(&cbb, group, x))
			goto err;
	} else {
		if (!ec_add_field_element_cbb(&cbb, group, x))
			goto err;
		if (!ec_add_field_element_cbb(&cbb, group, y))
			goto err;
	}

 done:
	if (!CBB_finish(&cbb, NULL, &ret))
		goto err;

	if (ret != encoded_length) {
		ret = 0;
		goto err;
	}

 err:
	CBB_cleanup(&cbb);
	BN_CTX_end(ctx);

	return ret;
}

static int
ec_oct2point(const EC_GROUP *group, EC_POINT *point,
    const unsigned char *buf, size_t len, BN_CTX *ctx)
{
	CBS cbs;
	uint8_t form;
	int ybit;
	BIGNUM *x, *y;
	int ret = 0;

	BN_CTX_start(ctx);
	CBS_init(&cbs, buf, len);

	if (!ec_get_leading_octet_cbs(&cbs, &form, &ybit))
		goto err;

	if (form == EC_POINT_AT_INFINITY) {
		if (!EC_POINT_set_to_infinity(group, point))
			goto err;

		goto done;
	}

	if ((x = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((y = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (form == EC_POINT_COMPRESSED) {
		if (!ec_get_field_element_cbs(&cbs, group, x))
			goto err;
		if (!EC_POINT_set_compressed_coordinates(group, point, x, ybit, ctx))
			goto err;
	} else {
		if (!ec_get_field_element_cbs(&cbs, group, x))
			goto err;
		if (!ec_get_field_element_cbs(&cbs, group, y))
			goto err;
		if (!ec_check_hybrid_ybit_is_consistent(form, ybit, y))
			goto err;
		if (!EC_POINT_set_affine_coordinates(group, point, x, y, ctx))
			goto err;
	}

 done:
	if (CBS_len(&cbs) > 0) {
		ECerror(EC_R_INVALID_ENCODING);
		goto err;
	}

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

int
ec_point_to_octets(const EC_GROUP *group, const EC_POINT *point, int form,
    unsigned char **out_buf, size_t *out_len, BN_CTX *ctx)
{
	unsigned char *buf = NULL;
	size_t len = 0;
	int ret = 0;

	*out_len = 0;

	if (out_buf == NULL || *out_buf != NULL)
		goto err;

	if ((len = EC_POINT_point2oct(group, point, form, NULL, 0, ctx)) == 0)
		goto err;
	if ((buf = calloc(1, len)) == NULL)
		goto err;
	if (EC_POINT_point2oct(group, point, form, buf, len, ctx) != len)
		goto err;

	*out_buf = buf;
	buf = NULL;
	*out_len = len;
	len = 0;

	ret = 1;

 err:
	freezero(buf, len);

	return ret;
}

int
ec_point_from_octets(const EC_GROUP *group, const unsigned char *buf, size_t buf_len,
    EC_POINT **out_point, uint8_t *out_form, BN_CTX *ctx)
{
	EC_POINT *point;
	int ret = 0;

	if ((point = *out_point) == NULL)
		point = EC_POINT_new(group);
	if (point == NULL)
		goto err;

	if (!EC_POINT_oct2point(group, point, buf, buf_len, ctx))
		goto err;

	if (out_form != NULL)
		*out_form = buf[0] & ~EC_POINT_YBIT;

	*out_point = point;
	point = NULL;

	ret = 1;

 err:
	if (*out_point != point)
		EC_POINT_free(point);

	return ret;
}

static int
ec_normalize_form(const EC_GROUP *group, const EC_POINT *point, int form,
    uint8_t *out_form)
{
	/*
	 * Established behavior is to reject a request for the form 0 for the
	 * point at infinity even if it is valid.
	 */
	if (form <= 0 || form > UINT8_MAX)
		return 0;
	if (!ec_conversion_form_is_valid(form))
		return 0;

	*out_form = form;
	if (EC_POINT_is_at_infinity(group, point))
		*out_form = EC_POINT_AT_INFINITY;

	return 1;
}

size_t
EC_POINT_point2oct(const EC_GROUP *group, const EC_POINT *point,
    point_conversion_form_t conv_form, unsigned char *buf, size_t len,
    BN_CTX *ctx_in)
{
	BN_CTX *ctx = NULL;
	uint8_t form;
	size_t ret = 0;

	if (!ec_normalize_form(group, point, conv_form, &form)) {
		ECerror(EC_R_INVALID_FORM);
		goto err;
	}

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = ec_point2oct(group, point, form, buf, len, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_point2oct);

int
EC_POINT_oct2point(const EC_GROUP *group, EC_POINT *point,
    const unsigned char *buf, size_t len, BN_CTX *ctx_in)
{
	BN_CTX *ctx;
	int ret = 0;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if (group->meth != point->meth) {
		ECerror(EC_R_INCOMPATIBLE_OBJECTS);
		goto err;
	}
	ret = ec_oct2point(group, point, buf, len, ctx);

 err:
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}
LCRYPTO_ALIAS(EC_POINT_oct2point);

BIGNUM *
EC_POINT_point2bn(const EC_GROUP *group, const EC_POINT *point,
    point_conversion_form_t form, BIGNUM *in_bn, BN_CTX *ctx)
{
	BIGNUM *bn = NULL;
	unsigned char *buf = NULL;
	size_t buf_len = 0;

	if (!ec_point_to_octets(group, point, form, &buf, &buf_len, ctx))
		goto err;
	if ((bn = BN_bin2bn(buf, buf_len, in_bn)) == NULL)
		goto err;

 err:
	freezero(buf, buf_len);

	return bn;
}
LCRYPTO_ALIAS(EC_POINT_point2bn);

EC_POINT *
EC_POINT_bn2point(const EC_GROUP *group,
    const BIGNUM *bn, EC_POINT *point, BN_CTX *ctx)
{
	unsigned char *buf = NULL;
	size_t buf_len = 0;

	/* Of course BN_bn2bin() is in no way symmetric to BN_bin2bn()... */
	if ((buf_len = BN_num_bytes(bn)) == 0)
		goto err;
	if ((buf = calloc(1, buf_len)) == NULL)
		goto err;
	if (!BN_bn2bin(bn, buf))
		goto err;
	if (!ec_point_from_octets(group, buf, buf_len, &point, NULL, ctx))
		goto err;

 err:
	freezero(buf, buf_len);

	return point;
}
LCRYPTO_ALIAS(EC_POINT_bn2point);

char *
EC_POINT_point2hex(const EC_GROUP *group, const EC_POINT *point,
    point_conversion_form_t form, BN_CTX *ctx)
{
	BIGNUM *bn;
	char *hex = NULL;

	if ((bn = EC_POINT_point2bn(group, point, form, NULL, ctx)) == NULL)
		goto err;
	if ((hex = BN_bn2hex(bn)) == NULL)
		goto err;

 err:
	BN_free(bn);

	return hex;
}
LCRYPTO_ALIAS(EC_POINT_point2hex);

EC_POINT *
EC_POINT_hex2point(const EC_GROUP *group, const char *hex,
    EC_POINT *in_point, BN_CTX *ctx)
{
	EC_POINT *point = NULL;
	BIGNUM *bn = NULL;

	if (BN_hex2bn(&bn, hex) == 0)
		goto err;
	if ((point = EC_POINT_bn2point(group, bn, in_point, ctx)) == NULL)
		goto err;

 err:
	BN_free(bn);

	return point;
}
LCRYPTO_ALIAS(EC_POINT_hex2point);
