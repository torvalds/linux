/*	$OpenBSD: ec_internal.h,v 1.2 2025/08/02 15:44:09 jsing Exp $	*/
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

#include <openssl/bn.h>

#ifndef HEADER_EC_INTERNAL_H
#define HEADER_EC_INTERNAL_H

#define EC_FIELD_ELEMENT_MAX_BITS 521
#define EC_FIELD_ELEMENT_MAX_BYTES \
    (EC_FIELD_ELEMENT_MAX_BITS + 7) / 8
#define EC_FIELD_ELEMENT_MAX_WORDS \
    ((EC_FIELD_ELEMENT_MAX_BYTES + BN_BYTES - 1) / BN_BYTES)

typedef struct {
	BN_ULONG w[EC_FIELD_ELEMENT_MAX_WORDS];
} EC_FIELD_ELEMENT;

typedef struct {
	size_t n;
	EC_FIELD_ELEMENT m;
	EC_FIELD_ELEMENT rr;
	BN_ULONG minv0;
} EC_FIELD_MODULUS;

int ec_field_modulus_from_bn(EC_FIELD_MODULUS *fm, const BIGNUM *bn,
    BN_CTX *ctx);

int ec_field_element_from_bn(const EC_FIELD_MODULUS *fm, const EC_GROUP *group,
    EC_FIELD_ELEMENT *fe, const BIGNUM *bn, BN_CTX *ctx);
int ec_field_element_to_bn(const EC_FIELD_MODULUS *fm, const EC_FIELD_ELEMENT *fe,
    BIGNUM *bn, BN_CTX *ctx);

void ec_field_element_copy(EC_FIELD_ELEMENT *dst, const EC_FIELD_ELEMENT *src);
void ec_field_element_select(const EC_FIELD_MODULUS *fm, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b, int conditional);

int ec_field_element_equal(const EC_FIELD_MODULUS *fm, const EC_FIELD_ELEMENT *a,
    const EC_FIELD_ELEMENT *b);
int ec_field_element_is_zero(const EC_FIELD_MODULUS *fm, const EC_FIELD_ELEMENT *fe);

void ec_field_element_add(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b);
void ec_field_element_sub(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b);
void ec_field_element_mul(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b);
void ec_field_element_sqr(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a);

#endif
