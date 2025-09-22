/* $OpenBSD: bn_mont.c,v 1.70 2025/08/30 07:54:27 jsing Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */
/* ====================================================================
 * Copyright (c) 1998-2006 The OpenSSL Project.  All rights reserved.
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

/*
 * Details about Montgomery multiplication algorithms can be found at
 * http://security.ece.orst.edu/publications.html, e.g.
 * http://security.ece.orst.edu/koc/papers/j37acmon.pdf and
 * sections 3.8 and 4.2 in http://security.ece.orst.edu/koc/papers/r01rsasw.pdf
 */

#include <limits.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "bn_internal.h"
#include "bn_local.h"

BN_MONT_CTX *
BN_MONT_CTX_new(void)
{
	BN_MONT_CTX *mctx;

	if ((mctx = calloc(1, sizeof(BN_MONT_CTX))) == NULL)
		return NULL;
	mctx->flags = BN_FLG_MALLOCED;

	BN_init(&mctx->RR);
	BN_init(&mctx->N);

	return mctx;
}
LCRYPTO_ALIAS(BN_MONT_CTX_new);

void
BN_MONT_CTX_free(BN_MONT_CTX *mctx)
{
	if (mctx == NULL)
		return;

	BN_free(&mctx->RR);
	BN_free(&mctx->N);

	if (mctx->flags & BN_FLG_MALLOCED)
		free(mctx);
}
LCRYPTO_ALIAS(BN_MONT_CTX_free);

BN_MONT_CTX *
BN_MONT_CTX_create(const BIGNUM *bn, BN_CTX *bn_ctx)
{
	BN_MONT_CTX *mctx;

	if ((mctx = BN_MONT_CTX_new()) == NULL)
		goto err;
	if (!BN_MONT_CTX_set(mctx, bn, bn_ctx))
		goto err;

	return mctx;

 err:
	BN_MONT_CTX_free(mctx);

	return NULL;
}

BN_MONT_CTX *
BN_MONT_CTX_copy(BN_MONT_CTX *dst, const BN_MONT_CTX *src)
{
	if (dst == src)
		return dst;

	if (!bn_copy(&dst->RR, &src->RR))
		return NULL;
	if (!bn_copy(&dst->N, &src->N))
		return NULL;

	dst->ri = src->ri;
	dst->n0[0] = src->n0[0];
	dst->n0[1] = src->n0[1];

	return dst;
}
LCRYPTO_ALIAS(BN_MONT_CTX_copy);

int
BN_MONT_CTX_set(BN_MONT_CTX *mont, const BIGNUM *mod, BN_CTX *ctx)
{
	BIGNUM *N, *Ninv, *Rinv, *R;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((N = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Ninv = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((R = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Rinv = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* Save modulus and determine length of R. */
	if (BN_is_zero(mod))
		goto err;
	if (!bn_copy(&mont->N, mod))
		 goto err;
	mont->N.neg = 0;
	mont->ri = ((BN_num_bits(mod) + BN_BITS2 - 1) / BN_BITS2) * BN_BITS2;
	if (mont->ri > INT_MAX / 2)
		goto err;

	/*
	 * Compute Ninv = (R * Rinv - 1)/N mod R, for R = 2^64. This provides
	 * a single or double word result (dependent on BN word size), that is
	 * later used to implement Montgomery reduction.
	 */
	BN_zero(R);
	if (!BN_set_bit(R, 64))
		goto err;

	/* N = N mod R. */
	if (!bn_wexpand(N, 2))
		goto err;
	if (!BN_set_word(N, mod->d[0]))
		goto err;
#if BN_BITS2 == 32
	if (mod->top > 1) {
		N->d[1] = mod->d[1];
		N->top += bn_ct_ne_zero(N->d[1]);
	}
#endif

	/* Rinv = R^-1 mod N */
	if ((BN_mod_inverse_ct(Rinv, R, N, ctx)) == NULL)
		goto err;

	/* Ninv = (R * Rinv - 1) / N */
	if (!BN_lshift(Ninv, Rinv, 64))
		goto err;
	if (BN_is_zero(Ninv)) {
		/* R * Rinv == 0, set to R so that R * Rinv - 1 is mod R. */
		if (!BN_set_bit(Ninv, 64))
			goto err;
	}
	if (!BN_sub_word(Ninv, 1))
		goto err;
	if (!BN_div_ct(Ninv, NULL, Ninv, N, ctx))
		goto err;

	/* Store least significant word(s) of Ninv. */
	mont->n0[0] = mont->n0[1] = 0;
	if (Ninv->top > 0)
		mont->n0[0] = Ninv->d[0];
#if BN_BITS2 == 32
	/* Some BN_BITS2 == 32 platforms (namely parisc) use two words of Ninv. */
	if (Ninv->top > 1)
		mont->n0[1] = Ninv->d[1];
#endif

	/* Compute RR = R * R mod N, for use when converting to Montgomery form. */
	BN_zero(&mont->RR);
	if (!BN_set_bit(&mont->RR, mont->ri * 2))
		goto err;
	if (!BN_mod_ct(&mont->RR, &mont->RR, &mont->N, ctx))
		goto err;

	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}
LCRYPTO_ALIAS(BN_MONT_CTX_set);

BN_MONT_CTX *
BN_MONT_CTX_set_locked(BN_MONT_CTX **pmctx, int lock, const BIGNUM *mod,
    BN_CTX *ctx)
{
	BN_MONT_CTX *mctx = NULL;

	CRYPTO_r_lock(lock);
	mctx = *pmctx;
	CRYPTO_r_unlock(lock);

	if (mctx != NULL)
		goto done;

	if ((mctx = BN_MONT_CTX_create(mod, ctx)) == NULL)
		goto err;

	CRYPTO_w_lock(lock);
	if (*pmctx != NULL) {
		/* Someone else raced us... */
		BN_MONT_CTX_free(mctx);
		mctx = *pmctx;
	} else {
		*pmctx = mctx;
	}
	CRYPTO_w_unlock(lock);

	goto done;
 err:
	BN_MONT_CTX_free(mctx);
	mctx = NULL;
 done:
	return mctx;
}
LCRYPTO_ALIAS(BN_MONT_CTX_set_locked);

/*
 * bn_montgomery_reduce_words() performs Montgomery reduction, reducing the input
 * from its Montgomery form aR to a, returning the result in r. a must be twice
 * the length of the modulus. Note that the input is mutated in the process of
 * performing the reduction.
 */
void
bn_montgomery_reduce_words(BN_ULONG *r, BN_ULONG *a, const BN_ULONG *n,
    BN_ULONG n0, int n_len)
{
	BN_ULONG v, mask;
	BN_ULONG carry = 0;
	int i;

	/* Add multiples of the modulus, so that it becomes divisible by R. */
	for (i = 0; i < n_len; i++) {
		v = bn_mulw_add_words(&a[i], n, n_len, a[i] * n0);
		bn_addw_addw(v, a[i + n_len], carry, &carry, &a[i + n_len]);
	}

	/* Divide by R (this is the equivalent of right shifting by n_len). */
	a = &a[n_len];

	/*
	 * The output is now in the range of [0, 2N). Attempt to reduce once by
	 * subtracting the modulus. If the reduction was necessary then the
	 * result is already in r, otherwise copy the value prior to reduction
	 * from the top half of a.
	 */
	mask = carry - bn_sub_words(r, a, n, n_len);

	for (i = 0; i < n_len; i++) {
		*r = (*r & ~mask) | (*a & mask);
		r++;
		a++;
	}
}

/*
 * bn_montgomery_reduce() performs Montgomery reduction, reducing the input
 * from its Montgomery form aR to a, returning the result in r. Note that the
 * input is mutated in the process of performing the reduction, destroying its
 * original value.
 */
static int
bn_montgomery_reduce(BIGNUM *r, BIGNUM *a, BN_MONT_CTX *mctx)
{
	BIGNUM *n;
	int i, max, n_len;

	n = &mctx->N;
	n_len = mctx->N.top;

	if (n_len == 0) {
		BN_zero(r);
		return 1;
	}

	if (!bn_wexpand(r, n_len))
		return 0;

	/*
	 * Expand a to twice the length of the modulus, zero if necessary.
	 * XXX - make this a requirement of the caller or use a temporary
	 * allocation.
	 */
	if ((max = 2 * n_len) < n_len)
		return 0;
	if (!bn_wexpand(a, max))
		return 0;
	for (i = a->top; i < max; i++)
		a->d[i] = 0;

	bn_montgomery_reduce_words(r->d, a->d, n->d, mctx->n0[0], n_len);

	r->top = n_len;

	bn_correct_top(r);

	BN_set_negative(r, a->neg ^ n->neg);

	return 1;
}

static int
bn_mod_mul_montgomery_simple(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    BN_MONT_CTX *mctx, BN_CTX *ctx)
{
	BIGNUM *tmp;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (a == b) {
		if (!BN_sqr(tmp, a, ctx))
			goto err;
	} else {
		if (!BN_mul(tmp, a, b, ctx))
			goto err;
	}

	/* Reduce from aRR to aR. */
	if (!bn_montgomery_reduce(r, tmp, mctx))
		goto err;

	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}

static inline void
bn_montgomery_multiply_word(const BN_ULONG *ap, BN_ULONG b, const BN_ULONG *np,
    BN_ULONG *tp, BN_ULONG w, BN_ULONG *carry_a, BN_ULONG *carry_n, int n_len)
{
	BN_ULONG x3, x2, x1, x0;

	*carry_a = *carry_n = 0;

	while (n_len & ~3) {
		bn_qwmulw_addqw_addw(ap[3], ap[2], ap[1], ap[0], b,
		    tp[3], tp[2], tp[1], tp[0], *carry_a, carry_a,
		    &x3, &x2, &x1, &x0);
		bn_qwmulw_addqw_addw(np[3], np[2], np[1], np[0], w,
		    x3, x2, x1, x0, *carry_n, carry_n,
		    &tp[3], &tp[2], &tp[1], &tp[0]);
		ap += 4;
		np += 4;
		tp += 4;
		n_len -= 4;
	}
	while (n_len > 0) {
		bn_mulw_addw_addw(ap[0], b, tp[0], *carry_a, carry_a, &x0);
		bn_mulw_addw_addw(np[0], w, x0, *carry_n, carry_n, &tp[0]);
		ap++;
		np++;
		tp++;
		n_len--;
	}
}

/*
 * bn_montgomery_multiply_words() computes r = aR * bR * R^-1 = abR for the
 * given word arrays. The caller must ensure that rp, ap, bp and np are all
 * n_len words in length, while tp must be n_len * 2 + 2 words in length.
 */
void
bn_montgomery_multiply_words(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp,
    const BN_ULONG *np, BN_ULONG *tp, BN_ULONG n0, int n_len)
{
	BN_ULONG a0, b, carry_a, carry_n, carry, mask, w;
	int i;

	carry = 0;

	for (i = 0; i < n_len; i++)
		tp[i] = 0;

	a0 = ap[0];

	for (i = 0; i < n_len; i++) {
		b = bp[i];

		/* Compute new t[0] * n0, as we need it for this iteration. */
		w = (a0 * b + tp[0]) * n0;

		bn_montgomery_multiply_word(ap, b, np, tp, w, &carry_a,
		    &carry_n, n_len);
		bn_addw_addw(carry_a, carry_n, carry, &carry, &tp[n_len]);

		tp++;
	}
	tp[n_len] = carry;

	/*
	 * The output is now in the range of [0, 2N). Attempt to reduce once by
	 * subtracting the modulus. If the reduction was necessary then the
	 * result is already in r, otherwise copy the value prior to reduction
	 * from tp.
	 */
	mask = bn_ct_ne_zero(tp[n_len]) - bn_sub_words(rp, tp, np, n_len);

	for (i = 0; i < n_len; i++) {
		*rp = (*rp & ~mask) | (*tp & mask);
		rp++;
		tp++;
	}
}

/*
 * bn_montgomery_multiply() computes r = aR * bR * R^-1 = abR for the given
 * BIGNUMs. The caller must ensure that the modulus is two or more words in
 * length and that a and b have the same number of words as the modulus.
 */
static int
bn_montgomery_multiply(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    BN_MONT_CTX *mctx, BN_CTX *ctx)
{
	BIGNUM *t;
	int ret = 0;

	BN_CTX_start(ctx);

	if (mctx->N.top <= 1 || a->top != mctx->N.top || b->top != mctx->N.top)
		goto err;
	if (!bn_wexpand(r, mctx->N.top))
		goto err;

	if ((t = BN_CTX_get(ctx)) == NULL)
		goto err;
	if (!bn_wexpand(t, mctx->N.top * 2 + 2))
		goto err;

	bn_montgomery_multiply_words(r->d, a->d, b->d, mctx->N.d, t->d,
	    mctx->n0[0], mctx->N.top);

	r->top = mctx->N.top;
	bn_correct_top(r);

	BN_set_negative(r, a->neg ^ b->neg);

	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}

#ifndef OPENSSL_BN_ASM_MONT
static int
bn_mod_mul_montgomery(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    BN_MONT_CTX *mctx, BN_CTX *ctx)
{
	if (mctx->N.top <= 1 || a->top != mctx->N.top || b->top != mctx->N.top)
		return bn_mod_mul_montgomery_simple(r, a, b, mctx, ctx);

	return bn_montgomery_multiply(r, a, b, mctx, ctx);
}
#else

static int
bn_mod_mul_montgomery(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    BN_MONT_CTX *mctx, BN_CTX *ctx)
{
	if (mctx->N.top <= 1 || a->top != mctx->N.top || b->top != mctx->N.top)
		return bn_mod_mul_montgomery_simple(r, a, b, mctx, ctx);

	/*
	 * Legacy bn_mul_mont() performs stack based allocation, without
	 * size limitation. Allowing a large size results in the stack
	 * being blown.
	 */
	if (mctx->N.top > (8 * 1024 / sizeof(BN_ULONG)))
		return bn_montgomery_multiply(r, a, b, mctx, ctx);

	if (!bn_wexpand(r, mctx->N.top))
		return 0;

	/*
	 * Legacy bn_mul_mont() can indicate that we should "fallback" to
	 * another implementation.
	 */
	if (!bn_mul_mont(r->d, a->d, b->d, mctx->N.d, mctx->n0, mctx->N.top))
		return bn_montgomery_multiply(r, a, b, mctx, ctx);

	r->top = mctx->N.top;
	bn_correct_top(r);

	BN_set_negative(r, a->neg ^ b->neg);

	return (1);
}
#endif

int
BN_mod_mul_montgomery(BIGNUM *r, const BIGNUM *a, const BIGNUM *b,
    BN_MONT_CTX *mctx, BN_CTX *ctx)
{
	/* Compute r = aR * bR * R^-1 mod N = abR mod N */
	return bn_mod_mul_montgomery(r, a, b, mctx, ctx);
}
LCRYPTO_ALIAS(BN_mod_mul_montgomery);

int
BN_to_montgomery(BIGNUM *r, const BIGNUM *a, BN_MONT_CTX *mctx, BN_CTX *ctx)
{
	/* Compute r = a * R * R * R^-1 mod N = aR mod N */
	return bn_mod_mul_montgomery(r, a, &mctx->RR, mctx, ctx);
}
LCRYPTO_ALIAS(BN_to_montgomery);

int
BN_from_montgomery(BIGNUM *r, const BIGNUM *a, BN_MONT_CTX *mctx, BN_CTX *ctx)
{
	BIGNUM *tmp;
	int ret = 0;

	BN_CTX_start(ctx);

	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;
	if (!bn_copy(tmp, a))
		goto err;
	if (!bn_montgomery_reduce(r, tmp, mctx))
		goto err;

	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}
LCRYPTO_ALIAS(BN_from_montgomery);
