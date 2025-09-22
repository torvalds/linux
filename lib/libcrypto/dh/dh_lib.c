/* $OpenBSD: dh_lib.c,v 1.47 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/bn.h>
#include <openssl/dh.h>

#include "dh_local.h"
#include "err_local.h"

static const DH_METHOD *default_DH_method = NULL;

void
DH_set_default_method(const DH_METHOD *meth)
{
	default_DH_method = meth;
}
LCRYPTO_ALIAS(DH_set_default_method);

const DH_METHOD *
DH_get_default_method(void)
{
	if (!default_DH_method)
		default_DH_method = DH_OpenSSL();
	return default_DH_method;
}
LCRYPTO_ALIAS(DH_get_default_method);

int
DH_set_method(DH *dh, const DH_METHOD *meth)
{
	/*
	 * NB: The caller is specifically setting a method, so it's not up to us
	 * to deal with which ENGINE it comes from.
	 */
	const DH_METHOD *mtmp;

	mtmp = dh->meth;
	if (mtmp->finish)
		mtmp->finish(dh);
	dh->meth = meth;
	if (meth->init)
		meth->init(dh);
	return 1;
}
LCRYPTO_ALIAS(DH_set_method);

DH *
DH_new(void)
{
	return DH_new_method(NULL);
}
LCRYPTO_ALIAS(DH_new);

DH *
DH_new_method(ENGINE *engine)
{
	DH *dh;

	if ((dh = calloc(1, sizeof(*dh))) == NULL) {
		DHerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	dh->meth = DH_get_default_method();
	dh->flags = dh->meth->flags & ~DH_FLAG_NON_FIPS_ALLOW;
	dh->references = 1;

	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_DH, dh, &dh->ex_data))
		goto err;
	if (dh->meth->init != NULL && !dh->meth->init(dh))
		goto err;

	return dh;

 err:
	DH_free(dh);

	return NULL;
}
LCRYPTO_ALIAS(DH_new_method);

void
DH_free(DH *dh)
{
	if (dh == NULL)
		return;

	if (CRYPTO_add(&dh->references, -1, CRYPTO_LOCK_DH) > 0)
		return;

	if (dh->meth != NULL && dh->meth->finish != NULL)
		dh->meth->finish(dh);

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_DH, dh, &dh->ex_data);

	BN_free(dh->p);
	BN_free(dh->q);
	BN_free(dh->g);
	BN_free(dh->pub_key);
	BN_free(dh->priv_key);
	free(dh);
}
LCRYPTO_ALIAS(DH_free);

int
DH_up_ref(DH *dh)
{
	return CRYPTO_add(&dh->references, 1, CRYPTO_LOCK_DH) > 1;
}
LCRYPTO_ALIAS(DH_up_ref);

int
DH_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	return CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_DH, argl, argp, new_func,
	    dup_func, free_func);
}
LCRYPTO_ALIAS(DH_get_ex_new_index);

int
DH_set_ex_data(DH *dh, int idx, void *arg)
{
	return CRYPTO_set_ex_data(&dh->ex_data, idx, arg);
}
LCRYPTO_ALIAS(DH_set_ex_data);

void *
DH_get_ex_data(DH *dh, int idx)
{
	return CRYPTO_get_ex_data(&dh->ex_data, idx);
}
LCRYPTO_ALIAS(DH_get_ex_data);

int
DH_size(const DH *dh)
{
	return BN_num_bytes(dh->p);
}
LCRYPTO_ALIAS(DH_size);

int
DH_bits(const DH *dh)
{
	return BN_num_bits(dh->p);
}
LCRYPTO_ALIAS(DH_bits);

int
DH_security_bits(const DH *dh)
{
	int N = -1;

	if (dh->q != NULL)
		N = BN_num_bits(dh->q);
	else if (dh->length > 0)
		N = dh->length;

	return BN_security_bits(BN_num_bits(dh->p), N);
}
LCRYPTO_ALIAS(DH_security_bits);

ENGINE *
DH_get0_engine(DH *dh)
{
	return NULL;
}
LCRYPTO_ALIAS(DH_get0_engine);

void
DH_get0_pqg(const DH *dh, const BIGNUM **p, const BIGNUM **q, const BIGNUM **g)
{
	if (p != NULL)
		*p = dh->p;
	if (q != NULL)
		*q = dh->q;
	if (g != NULL)
		*g = dh->g;
}
LCRYPTO_ALIAS(DH_get0_pqg);

int
DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g)
{
	if ((dh->p == NULL && p == NULL) || (dh->g == NULL && g == NULL))
		return 0;

	if (p != NULL) {
		BN_free(dh->p);
		dh->p = p;
	}
	if (q != NULL) {
		BN_free(dh->q);
		dh->q = q;
		dh->length = BN_num_bits(dh->q);
	}
	if (g != NULL) {
		BN_free(dh->g);
		dh->g = g;
	}

	return 1;
}
LCRYPTO_ALIAS(DH_set0_pqg);

void
DH_get0_key(const DH *dh, const BIGNUM **pub_key, const BIGNUM **priv_key)
{
	if (pub_key != NULL)
		*pub_key = dh->pub_key;
	if (priv_key != NULL)
		*priv_key = dh->priv_key;
}
LCRYPTO_ALIAS(DH_get0_key);

int
DH_set0_key(DH *dh, BIGNUM *pub_key, BIGNUM *priv_key)
{
	if (pub_key != NULL) {
		BN_free(dh->pub_key);
		dh->pub_key = pub_key;
	}
	if (priv_key != NULL) {
		BN_free(dh->priv_key);
		dh->priv_key = priv_key;
	}

	return 1;
}
LCRYPTO_ALIAS(DH_set0_key);

const BIGNUM *
DH_get0_p(const DH *dh)
{
	return dh->p;
}
LCRYPTO_ALIAS(DH_get0_p);

const BIGNUM *
DH_get0_q(const DH *dh)
{
	return dh->q;
}
LCRYPTO_ALIAS(DH_get0_q);

const BIGNUM *
DH_get0_g(const DH *dh)
{
	return dh->g;
}
LCRYPTO_ALIAS(DH_get0_g);

const BIGNUM *
DH_get0_priv_key(const DH *dh)
{
	return dh->priv_key;
}
LCRYPTO_ALIAS(DH_get0_priv_key);

const BIGNUM *
DH_get0_pub_key(const DH *dh)
{
	return dh->pub_key;
}
LCRYPTO_ALIAS(DH_get0_pub_key);

void
DH_clear_flags(DH *dh, int flags)
{
	dh->flags &= ~flags;
}
LCRYPTO_ALIAS(DH_clear_flags);

int
DH_test_flags(const DH *dh, int flags)
{
	return dh->flags & flags;
}
LCRYPTO_ALIAS(DH_test_flags);

void
DH_set_flags(DH *dh, int flags)
{
	dh->flags |= flags;
}
LCRYPTO_ALIAS(DH_set_flags);

long
DH_get_length(const DH *dh)
{
	return dh->length;
}
LCRYPTO_ALIAS(DH_get_length);

int
DH_set_length(DH *dh, long length)
{
	if (length < 0 || length > INT_MAX)
		return 0;

	dh->length = length;
	return 1;
}
LCRYPTO_ALIAS(DH_set_length);
