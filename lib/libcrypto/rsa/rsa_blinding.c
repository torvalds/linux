/* $OpenBSD: rsa_blinding.c,v 1.4 2025/05/10 05:54:38 tb Exp $ */
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

#include <pthread.h>
#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/rsa.h>

#include "bn_local.h"
#include "err_local.h"
#include "rsa_local.h"

#define BN_BLINDING_COUNTER	32

struct bn_blinding_st {
	BIGNUM *A;
	BIGNUM *Ai;
	BIGNUM *e;
	BIGNUM *mod;
	pthread_t tid;
	int counter;
	BN_MONT_CTX *m_ctx;
	int (*bn_mod_exp)(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
	    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx);
};

BN_BLINDING *
BN_BLINDING_new(const BIGNUM *e, const BIGNUM *mod, BN_CTX *ctx,
    int (*bn_mod_exp)(BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
	const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx), BN_MONT_CTX *m_ctx)
{
	BN_BLINDING *ret = NULL;

	if ((ret = calloc(1, sizeof(BN_BLINDING))) == NULL) {
		BNerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	if ((ret->A = BN_new()) == NULL)
		goto err;
	if ((ret->Ai = BN_new()) == NULL)
		goto err;
	if ((ret->e = BN_dup(e)) == NULL)
		goto err;
	if ((ret->mod = BN_dup(mod)) == NULL)
		goto err;
	if (BN_get_flags(mod, BN_FLG_CONSTTIME) != 0)
		BN_set_flags(ret->mod, BN_FLG_CONSTTIME);

	/* Update on first use. */
	ret->counter = BN_BLINDING_COUNTER - 1;
	ret->tid = pthread_self();

	if (bn_mod_exp != NULL)
		ret->bn_mod_exp = bn_mod_exp;
	if (m_ctx != NULL)
		ret->m_ctx = m_ctx;

	return ret;

 err:
	BN_BLINDING_free(ret);

	return NULL;
}

void
BN_BLINDING_free(BN_BLINDING *r)
{
	if (r == NULL)
		return;

	BN_free(r->A);
	BN_free(r->Ai);
	BN_free(r->e);
	BN_free(r->mod);
	free(r);
}

static int
BN_BLINDING_setup(BN_BLINDING *b, BN_CTX *ctx)
{
	if (!bn_rand_interval(b->A, 1, b->mod))
		return 0;
	if (BN_mod_inverse_ct(b->Ai, b->A, b->mod, ctx) == NULL)
		return 0;

	if (b->bn_mod_exp != NULL && b->m_ctx != NULL) {
		if (!b->bn_mod_exp(b->A, b->A, b->e, b->mod, ctx, b->m_ctx))
			return 0;
	} else {
		if (!BN_mod_exp_ct(b->A, b->A, b->e, b->mod, ctx))
			return 0;
	}

	return 1;
}

static int
BN_BLINDING_update(BN_BLINDING *b, BN_CTX *ctx)
{
	int ret = 0;

	if (++b->counter >= BN_BLINDING_COUNTER) {
		if (!BN_BLINDING_setup(b, ctx))
			goto err;
		b->counter = 0;
	} else {
		if (!BN_mod_sqr(b->A, b->A, b->mod, ctx))
			goto err;
		if (!BN_mod_sqr(b->Ai, b->Ai, b->mod, ctx))
			goto err;
	}

	ret = 1;

 err:
	return ret;
}

int
BN_BLINDING_convert(BIGNUM *n, BIGNUM *inv, BN_BLINDING *b, BN_CTX *ctx)
{
	int ret = 0;

	if (!BN_BLINDING_update(b, ctx))
		goto err;

	if (inv != NULL) {
		if (!bn_copy(inv, b->Ai))
			goto err;
	}

	ret = BN_mod_mul(n, n, b->A, b->mod, ctx);

 err:
	return ret;
}

int
BN_BLINDING_invert(BIGNUM *n, const BIGNUM *inv, BN_BLINDING *b, BN_CTX *ctx)
{
	if (inv == NULL)
		inv = b->Ai;

	return BN_mod_mul(n, n, inv, b->mod, ctx);
}

int
BN_BLINDING_is_local(BN_BLINDING *b)
{
	return pthread_equal(pthread_self(), b->tid) != 0;
}

static BIGNUM *
rsa_get_public_exp(const BIGNUM *d, const BIGNUM *p, const BIGNUM *q,
    BN_CTX *ctx)
{
	BIGNUM *ret = NULL, *r0, *r1, *r2;

	if (d == NULL || p == NULL || q == NULL)
		return NULL;

	BN_CTX_start(ctx);
	if ((r0 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r2 = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_sub(r1, p, BN_value_one()))
		goto err;
	if (!BN_sub(r2, q, BN_value_one()))
		goto err;
	if (!BN_mul(r0, r1, r2, ctx))
		goto err;

	ret = BN_mod_inverse_ct(NULL, d, r0, ctx);
err:
	BN_CTX_end(ctx);
	return ret;
}

BN_BLINDING *
RSA_setup_blinding(RSA *rsa, BN_CTX *in_ctx)
{
	BIGNUM *e = NULL;
	BIGNUM n;
	BN_CTX *ctx = NULL;
	BN_BLINDING *ret = NULL;

	if ((ctx = in_ctx) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((e = rsa->e) == NULL)
		e = rsa_get_public_exp(rsa->d, rsa->p, rsa->q, ctx);
	if (e == NULL) {
		RSAerror(RSA_R_NO_PUBLIC_EXPONENT);
		goto err;
	}

	BN_init(&n);
	BN_with_flags(&n, rsa->n, BN_FLG_CONSTTIME);

	if ((ret = BN_BLINDING_new(e, &n, ctx, rsa->meth->bn_mod_exp,
	    rsa->_method_mod_n)) == NULL) {
		RSAerror(ERR_R_BN_LIB);
		goto err;
	}

 err:
	BN_CTX_end(ctx);
	if (ctx != in_ctx)
		BN_CTX_free(ctx);
	if (e != rsa->e)
		BN_free(e);

	return ret;
}

void
RSA_blinding_off(RSA *rsa)
{
	BN_BLINDING_free(rsa->blinding);
	rsa->blinding = NULL;
	rsa->flags |= RSA_FLAG_NO_BLINDING;
}
LCRYPTO_ALIAS(RSA_blinding_off);

int
RSA_blinding_on(RSA *rsa, BN_CTX *ctx)
{
	int ret = 0;

	if (rsa->blinding != NULL)
		RSA_blinding_off(rsa);

	rsa->blinding = RSA_setup_blinding(rsa, ctx);
	if (rsa->blinding == NULL)
		goto err;

	rsa->flags &= ~RSA_FLAG_NO_BLINDING;
	ret = 1;
err:
	return (ret);
}
LCRYPTO_ALIAS(RSA_blinding_on);
