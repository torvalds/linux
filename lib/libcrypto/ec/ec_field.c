/*	$OpenBSD: ec_field.c,v 1.3 2025/08/02 16:20:00 jsing Exp $	*/
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

#include <string.h>

#include <openssl/ec.h>

#include "bn_local.h"
#include "bn_internal.h"
#include "ec_local.h"
#include "ec_internal.h"

int
ec_field_modulus_from_bn(EC_FIELD_MODULUS *fm, const BIGNUM *bn, BN_CTX *ctx)
{
	BN_MONT_CTX *mctx = NULL;
	size_t i;
	int ret = 0;

	if (BN_is_negative(bn))
		goto err;
	if (BN_num_bits(bn) > EC_FIELD_ELEMENT_MAX_BITS)
		goto err;

	memset(fm, 0, sizeof(*fm));

	fm->n = (BN_num_bits(bn) + BN_BITS2 - 1) / BN_BITS2;

	for (i = 0; i < bn->top; i++)
		fm->m.w[i] = bn->d[i];

	/* XXX - implement this without BN_MONT_CTX. */
	if ((mctx = BN_MONT_CTX_new()) == NULL)
		goto err;
	if (!BN_MONT_CTX_set(mctx, bn, ctx))
		goto err;

	for (i = 0; i < mctx->RR.top; i++)
		fm->rr.w[i] = mctx->RR.d[i];

	fm->minv0 = mctx->n0[0];

	ret = 1;

 err:
	BN_MONT_CTX_free(mctx);

	return ret;
}

int
ec_field_element_from_bn(const EC_FIELD_MODULUS *fm, const EC_GROUP *group,
    EC_FIELD_ELEMENT *fe, const BIGNUM *bn, BN_CTX *ctx)
{
	BN_ULONG t[EC_FIELD_ELEMENT_MAX_WORDS * 2 + 2];
	BIGNUM *tmp;
	size_t i;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* XXX - enforce 0 <= n < p. */

	if (BN_num_bits(bn) > EC_FIELD_ELEMENT_MAX_BITS)
		goto err;

	/* XXX - do this without BN. */
	if (!BN_nnmod(tmp, bn, group->p, ctx))
		goto err;

	if (BN_num_bits(tmp) > EC_FIELD_ELEMENT_MAX_BITS)
		abort();

	memset(fe->w, 0, sizeof(fe->w));

	for (i = 0; i < tmp->top; i++)
		fe->w[i] = tmp->d[i];

	bn_mod_mul_words(fe->w, fe->w, fm->rr.w, fm->m.w, t, fm->minv0, fm->n);

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}

int
ec_field_element_to_bn(const EC_FIELD_MODULUS *fm, const EC_FIELD_ELEMENT *fe,
    BIGNUM *bn, BN_CTX *ctx)
{
	BN_ULONG t[EC_FIELD_ELEMENT_MAX_WORDS * 2 + 2];
	size_t i;

	if (!bn_wexpand(bn, fm->n))
		return 0;

	memset(t, 0, sizeof(t));
	for (i = 0; i < fm->n; i++)
		t[i] = fe->w[i];

	bn_montgomery_reduce_words(bn->d, t, fm->m.w, fm->minv0, fm->n);

	bn->top = fm->n;
	bn_correct_top(bn);

	return 1;
}

void
ec_field_element_copy(EC_FIELD_ELEMENT *dst, const EC_FIELD_ELEMENT *src)
{
	memcpy(dst, src, sizeof(EC_FIELD_ELEMENT));
}

void
ec_field_element_select(const EC_FIELD_MODULUS *fm, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b, int conditional)
{
	BN_ULONG mask;
	int i;

	mask = bn_ct_eq_zero_mask(conditional);

	for (i = 0; i < fm->n; i++)
		r->w[i] = (a->w[i] & mask) | (b->w[i] & ~mask);
}

int
ec_field_element_equal(const EC_FIELD_MODULUS *fm, const EC_FIELD_ELEMENT *a,
    const EC_FIELD_ELEMENT *b)
{
	BN_ULONG v = 0;
	int i;

	for (i = 0; i < fm->n; i++)
		v |= a->w[i] ^ b->w[i];

	return bn_ct_eq_zero(v);
}

int
ec_field_element_is_zero(const EC_FIELD_MODULUS *fm, const EC_FIELD_ELEMENT *fe)
{
	BN_ULONG v = 0;
	int i;

	for (i = 0; i < fm->n; i++)
		v |= fe->w[i];

	return bn_ct_eq_zero(v);
}

void
ec_field_element_add(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b)
{
	bn_mod_add_words(r->w, a->w, b->w, m->m.w, m->n);
}

void
ec_field_element_sub(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b)
{
	bn_mod_sub_words(r->w, a->w, b->w, m->m.w, m->n);
}

void
ec_field_element_mul(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a, const EC_FIELD_ELEMENT *b)
{
	BN_ULONG t[EC_FIELD_ELEMENT_MAX_WORDS * 2 + 2];

	bn_mod_mul_words(r->w, a->w, b->w, m->m.w, t, m->minv0, m->n);
}

void
ec_field_element_sqr(const EC_FIELD_MODULUS *m, EC_FIELD_ELEMENT *r,
    const EC_FIELD_ELEMENT *a)
{
	BN_ULONG t[EC_FIELD_ELEMENT_MAX_WORDS * 2 + 2];

	bn_mod_sqr_words(r->w, a->w, m->m.w, t, m->minv0, m->n);
}
