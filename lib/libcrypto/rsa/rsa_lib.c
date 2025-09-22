/* $OpenBSD: rsa_lib.c,v 1.51 2025/05/10 05:54:38 tb Exp $ */
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

#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/lhash.h>
#include <openssl/rsa.h>

#include "bn_local.h"
#include "err_local.h"
#include "evp_local.h"
#include "rsa_local.h"

static const RSA_METHOD *default_RSA_meth = NULL;

RSA *
RSA_new(void)
{
	RSA *r = RSA_new_method(NULL);

	return r;
}
LCRYPTO_ALIAS(RSA_new);

void
RSA_set_default_method(const RSA_METHOD *meth)
{
	default_RSA_meth = meth;
}
LCRYPTO_ALIAS(RSA_set_default_method);

const RSA_METHOD *
RSA_get_default_method(void)
{
	if (default_RSA_meth == NULL)
		default_RSA_meth = RSA_PKCS1_SSLeay();

	return default_RSA_meth;
}
LCRYPTO_ALIAS(RSA_get_default_method);

const RSA_METHOD *
RSA_get_method(const RSA *rsa)
{
	return rsa->meth;
}
LCRYPTO_ALIAS(RSA_get_method);

int
RSA_set_method(RSA *rsa, const RSA_METHOD *meth)
{
	/*
	 * NB: The caller is specifically setting a method, so it's not up to us
	 * to deal with which ENGINE it comes from.
	 */
	const RSA_METHOD *mtmp;

	mtmp = rsa->meth;
	if (mtmp->finish)
		mtmp->finish(rsa);
	rsa->meth = meth;
	if (meth->init)
		meth->init(rsa);
	return 1;
}
LCRYPTO_ALIAS(RSA_set_method);

RSA *
RSA_new_method(ENGINE *engine)
{
	RSA *ret;

	if ((ret = calloc(1, sizeof(RSA))) == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	ret->meth = RSA_get_default_method();

	ret->references = 1;
	ret->flags = ret->meth->flags & ~RSA_FLAG_NON_FIPS_ALLOW;

	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_RSA, ret, &ret->ex_data))
		goto err;

	if (ret->meth->init != NULL && !ret->meth->init(ret)) {
		CRYPTO_free_ex_data(CRYPTO_EX_INDEX_RSA, ret, &ret->ex_data);
		goto err;
	}

	return ret;

 err:
	free(ret);

	return NULL;
}
LCRYPTO_ALIAS(RSA_new_method);

void
RSA_free(RSA *r)
{
	int i;

	if (r == NULL)
		return;

	i = CRYPTO_add(&r->references, -1, CRYPTO_LOCK_RSA);
	if (i > 0)
		return;

	if (r->meth->finish)
		r->meth->finish(r);

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_RSA, r, &r->ex_data);

	BN_free(r->n);
	BN_free(r->e);
	BN_free(r->d);
	BN_free(r->p);
	BN_free(r->q);
	BN_free(r->dmp1);
	BN_free(r->dmq1);
	BN_free(r->iqmp);
	BN_BLINDING_free(r->blinding);
	BN_BLINDING_free(r->mt_blinding);
	RSA_PSS_PARAMS_free(r->pss);
	free(r);
}
LCRYPTO_ALIAS(RSA_free);

int
RSA_up_ref(RSA *r)
{
	return CRYPTO_add(&r->references, 1, CRYPTO_LOCK_RSA) > 1;
}
LCRYPTO_ALIAS(RSA_up_ref);

int
RSA_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	return CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_RSA, argl, argp,
	    new_func, dup_func, free_func);
}
LCRYPTO_ALIAS(RSA_get_ex_new_index);

int
RSA_set_ex_data(RSA *r, int idx, void *arg)
{
	return CRYPTO_set_ex_data(&r->ex_data, idx, arg);
}
LCRYPTO_ALIAS(RSA_set_ex_data);

void *
RSA_get_ex_data(const RSA *r, int idx)
{
	return CRYPTO_get_ex_data(&r->ex_data, idx);
}
LCRYPTO_ALIAS(RSA_get_ex_data);

int
RSA_security_bits(const RSA *rsa)
{
	return BN_security_bits(RSA_bits(rsa), -1);
}
LCRYPTO_ALIAS(RSA_security_bits);

void
RSA_get0_key(const RSA *r, const BIGNUM **n, const BIGNUM **e, const BIGNUM **d)
{
	if (n != NULL)
		*n = r->n;
	if (e != NULL)
		*e = r->e;
	if (d != NULL)
		*d = r->d;
}
LCRYPTO_ALIAS(RSA_get0_key);

int
RSA_set0_key(RSA *r, BIGNUM *n, BIGNUM *e, BIGNUM *d)
{
	if ((r->n == NULL && n == NULL) || (r->e == NULL && e == NULL))
		return 0;

	if (n != NULL) {
		BN_free(r->n);
		r->n = n;
	}
	if (e != NULL) {
		BN_free(r->e);
		r->e = e;
	}
	if (d != NULL) {
		BN_free(r->d);
		r->d = d;
	}

	return 1;
}
LCRYPTO_ALIAS(RSA_set0_key);

void
RSA_get0_crt_params(const RSA *r, const BIGNUM **dmp1, const BIGNUM **dmq1,
    const BIGNUM **iqmp)
{
	if (dmp1 != NULL)
		*dmp1 = r->dmp1;
	if (dmq1 != NULL)
		*dmq1 = r->dmq1;
	if (iqmp != NULL)
		*iqmp = r->iqmp;
}
LCRYPTO_ALIAS(RSA_get0_crt_params);

int
RSA_set0_crt_params(RSA *r, BIGNUM *dmp1, BIGNUM *dmq1, BIGNUM *iqmp)
{
	if ((r->dmp1 == NULL && dmp1 == NULL) ||
	    (r->dmq1 == NULL && dmq1 == NULL) ||
	    (r->iqmp == NULL && iqmp == NULL))
		return 0;

	if (dmp1 != NULL) {
		BN_free(r->dmp1);
		r->dmp1 = dmp1;
	}
	if (dmq1 != NULL) {
		BN_free(r->dmq1);
		r->dmq1 = dmq1;
	}
	if (iqmp != NULL) {
		BN_free(r->iqmp);
		r->iqmp = iqmp;
	}

	return 1;
}
LCRYPTO_ALIAS(RSA_set0_crt_params);

void
RSA_get0_factors(const RSA *r, const BIGNUM **p, const BIGNUM **q)
{
	if (p != NULL)
		*p = r->p;
	if (q != NULL)
		*q = r->q;
}
LCRYPTO_ALIAS(RSA_get0_factors);

int
RSA_set0_factors(RSA *r, BIGNUM *p, BIGNUM *q)
{
	if ((r->p == NULL && p == NULL) || (r->q == NULL && q == NULL))
		return 0;

	if (p != NULL) {
		BN_free(r->p);
		r->p = p;
	}
	if (q != NULL) {
		BN_free(r->q);
		r->q = q;
	}

	return 1;
}
LCRYPTO_ALIAS(RSA_set0_factors);

const BIGNUM *
RSA_get0_n(const RSA *r)
{
	return r->n;
}
LCRYPTO_ALIAS(RSA_get0_n);

const BIGNUM *
RSA_get0_e(const RSA *r)
{
	return r->e;
}
LCRYPTO_ALIAS(RSA_get0_e);

const BIGNUM *
RSA_get0_d(const RSA *r)
{
	return r->d;
}
LCRYPTO_ALIAS(RSA_get0_d);

const BIGNUM *
RSA_get0_p(const RSA *r)
{
	return r->p;
}
LCRYPTO_ALIAS(RSA_get0_p);

const BIGNUM *
RSA_get0_q(const RSA *r)
{
	return r->q;
}
LCRYPTO_ALIAS(RSA_get0_q);

const BIGNUM *
RSA_get0_dmp1(const RSA *r)
{
	return r->dmp1;
}
LCRYPTO_ALIAS(RSA_get0_dmp1);

const BIGNUM *
RSA_get0_dmq1(const RSA *r)
{
	return r->dmq1;
}
LCRYPTO_ALIAS(RSA_get0_dmq1);

const BIGNUM *
RSA_get0_iqmp(const RSA *r)
{
	return r->iqmp;
}
LCRYPTO_ALIAS(RSA_get0_iqmp);

const RSA_PSS_PARAMS *
RSA_get0_pss_params(const RSA *r)
{
	return r->pss;
}
LCRYPTO_ALIAS(RSA_get0_pss_params);

void
RSA_clear_flags(RSA *r, int flags)
{
	r->flags &= ~flags;
}
LCRYPTO_ALIAS(RSA_clear_flags);

int
RSA_test_flags(const RSA *r, int flags)
{
	return r->flags & flags;
}
LCRYPTO_ALIAS(RSA_test_flags);

void
RSA_set_flags(RSA *r, int flags)
{
	r->flags |= flags;
}
LCRYPTO_ALIAS(RSA_set_flags);

int
RSA_pkey_ctx_ctrl(EVP_PKEY_CTX *ctx, int optype, int cmd, int p1, void *p2)
{
	/* Return an error if the key type is not RSA or RSA-PSS. */
	if (ctx != NULL && ctx->pmeth != NULL &&
	    ctx->pmeth->pkey_id != EVP_PKEY_RSA &&
	    ctx->pmeth->pkey_id != EVP_PKEY_RSA_PSS)
		return -1;

	return EVP_PKEY_CTX_ctrl(ctx, -1, optype, cmd, p1, p2);
}
LCRYPTO_ALIAS(RSA_pkey_ctx_ctrl);
