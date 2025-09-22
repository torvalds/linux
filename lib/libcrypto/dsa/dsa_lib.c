/* $OpenBSD: dsa_lib.c,v 1.49 2025/05/10 05:54:38 tb Exp $ */
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

/* Original version from Steven Schoch <schoch@sheba.arc.nasa.gov> */

#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/dsa.h>

#ifndef OPENSSL_NO_DH
#include <openssl/dh.h>
#endif

#include "dh_local.h"
#include "dsa_local.h"
#include "err_local.h"

static const DSA_METHOD *default_DSA_method = NULL;

void
DSA_set_default_method(const DSA_METHOD *meth)
{
	default_DSA_method = meth;
}
LCRYPTO_ALIAS(DSA_set_default_method);

const DSA_METHOD *
DSA_get_default_method(void)
{
	if (!default_DSA_method)
		default_DSA_method = DSA_OpenSSL();
	return default_DSA_method;
}
LCRYPTO_ALIAS(DSA_get_default_method);

DSA *
DSA_new(void)
{
	return DSA_new_method(NULL);
}
LCRYPTO_ALIAS(DSA_new);

int
DSA_set_method(DSA *dsa, const DSA_METHOD *meth)
{
	/*
	 * NB: The caller is specifically setting a method, so it's not up to us
	 * to deal with which ENGINE it comes from.
	 */
	const DSA_METHOD *mtmp;
	mtmp = dsa->meth;
	if (mtmp->finish)
		mtmp->finish(dsa);
	dsa->meth = meth;
	if (meth->init)
		meth->init(dsa);
	return 1;
}
LCRYPTO_ALIAS(DSA_set_method);

DSA *
DSA_new_method(ENGINE *engine)
{
	DSA *dsa;

	if ((dsa = calloc(1, sizeof(DSA))) == NULL) {
		DSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	dsa->meth = DSA_get_default_method();
	dsa->flags = dsa->meth->flags & ~DSA_FLAG_NON_FIPS_ALLOW;
	dsa->references = 1;

	if (!CRYPTO_new_ex_data(CRYPTO_EX_INDEX_DSA, dsa, &dsa->ex_data))
		goto err;
	if (dsa->meth->init != NULL && !dsa->meth->init(dsa))
		goto err;

	return dsa;

 err:
	DSA_free(dsa);

	return NULL;
}
LCRYPTO_ALIAS(DSA_new_method);

void
DSA_free(DSA *dsa)
{
	if (dsa == NULL)
		return;

	if (CRYPTO_add(&dsa->references, -1, CRYPTO_LOCK_DSA) > 0)
		return;

	if (dsa->meth != NULL && dsa->meth->finish != NULL)
		dsa->meth->finish(dsa);

	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_DSA, dsa, &dsa->ex_data);

	BN_free(dsa->p);
	BN_free(dsa->q);
	BN_free(dsa->g);
	BN_free(dsa->pub_key);
	BN_free(dsa->priv_key);
	BN_free(dsa->kinv);
	BN_free(dsa->r);
	free(dsa);
}
LCRYPTO_ALIAS(DSA_free);

int
DSA_up_ref(DSA *dsa)
{
	return CRYPTO_add(&dsa->references, 1, CRYPTO_LOCK_DSA) > 1;
}
LCRYPTO_ALIAS(DSA_up_ref);

int
DSA_size(const DSA *dsa)
{
	DSA_SIG signature;
	int ret = 0;

	signature.r = dsa->q;
	signature.s = dsa->q;

	if ((ret = i2d_DSA_SIG(&signature, NULL)) < 0)
		ret = 0;

	return ret;
}
LCRYPTO_ALIAS(DSA_size);

int
DSA_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	return CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_DSA, argl, argp,
	    new_func, dup_func, free_func);
}
LCRYPTO_ALIAS(DSA_get_ex_new_index);

int
DSA_set_ex_data(DSA *dsa, int idx, void *arg)
{
	return CRYPTO_set_ex_data(&dsa->ex_data, idx, arg);
}
LCRYPTO_ALIAS(DSA_set_ex_data);

void *
DSA_get_ex_data(DSA *dsa, int idx)
{
	return CRYPTO_get_ex_data(&dsa->ex_data, idx);
}
LCRYPTO_ALIAS(DSA_get_ex_data);

int
DSA_security_bits(const DSA *dsa)
{
	if (dsa->p == NULL || dsa->q == NULL)
		return -1;

	return BN_security_bits(BN_num_bits(dsa->p), BN_num_bits(dsa->q));
}
LCRYPTO_ALIAS(DSA_security_bits);

#ifndef OPENSSL_NO_DH
DH *
DSA_dup_DH(const DSA *dsa)
{
	/*
	 * DSA has p, q, g, optional pub_key, optional priv_key.
	 * DH has p, optional length, g, optional pub_key, optional priv_key,
	 * optional q.
	 */
	DH *dh = NULL;

	if (dsa == NULL)
		goto err;

	if ((dh = DH_new()) == NULL)
		goto err;

	if (dsa->p != NULL) {
		if ((dh->p = BN_dup(dsa->p)) == NULL)
			goto err;
	}
	if (dsa->q != NULL) {
		dh->length = BN_num_bits(dsa->q);
		if ((dh->q = BN_dup(dsa->q)) == NULL)
			goto err;
	}
	if (dsa->g != NULL) {
		if ((dh->g = BN_dup(dsa->g)) == NULL)
			goto err;
	}
	if (dsa->pub_key != NULL) {
		if ((dh->pub_key = BN_dup(dsa->pub_key)) == NULL)
			goto err;
	}
	if (dsa->priv_key != NULL) {
		if ((dh->priv_key = BN_dup(dsa->priv_key)) == NULL)
			goto err;
	}

	return dh;

 err:
	DH_free(dh);
	return NULL;
}
LCRYPTO_ALIAS(DSA_dup_DH);
#endif

void
DSA_get0_pqg(const DSA *dsa, const BIGNUM **p, const BIGNUM **q, const BIGNUM **g)
{
	if (p != NULL)
		*p = dsa->p;
	if (q != NULL)
		*q = dsa->q;
	if (g != NULL)
		*g = dsa->g;
}
LCRYPTO_ALIAS(DSA_get0_pqg);

int
DSA_set0_pqg(DSA *dsa, BIGNUM *p, BIGNUM *q, BIGNUM *g)
{
	if ((dsa->p == NULL && p == NULL) || (dsa->q == NULL && q == NULL) ||
	    (dsa->g == NULL && g == NULL))
		return 0;

	if (p != NULL) {
		BN_free(dsa->p);
		dsa->p = p;
	}
	if (q != NULL) {
		BN_free(dsa->q);
		dsa->q = q;
	}
	if (g != NULL) {
		BN_free(dsa->g);
		dsa->g = g;
	}

	return 1;
}
LCRYPTO_ALIAS(DSA_set0_pqg);

void
DSA_get0_key(const DSA *dsa, const BIGNUM **pub_key, const BIGNUM **priv_key)
{
	if (pub_key != NULL)
		*pub_key = dsa->pub_key;
	if (priv_key != NULL)
		*priv_key = dsa->priv_key;
}
LCRYPTO_ALIAS(DSA_get0_key);

int
DSA_set0_key(DSA *dsa, BIGNUM *pub_key, BIGNUM *priv_key)
{
	if (dsa->pub_key == NULL && pub_key == NULL)
		return 0;

	if (pub_key != NULL) {
		BN_free(dsa->pub_key);
		dsa->pub_key = pub_key;
	}
	if (priv_key != NULL) {
		BN_free(dsa->priv_key);
		dsa->priv_key = priv_key;
	}

	return 1;
}
LCRYPTO_ALIAS(DSA_set0_key);

const BIGNUM *
DSA_get0_p(const DSA *dsa)
{
	return dsa->p;
}
LCRYPTO_ALIAS(DSA_get0_p);

const BIGNUM *
DSA_get0_q(const DSA *dsa)
{
	return dsa->q;
}
LCRYPTO_ALIAS(DSA_get0_q);

const BIGNUM *
DSA_get0_g(const DSA *dsa)
{
	return dsa->g;
}
LCRYPTO_ALIAS(DSA_get0_g);

const BIGNUM *
DSA_get0_pub_key(const DSA *dsa)
{
	return dsa->pub_key;
}
LCRYPTO_ALIAS(DSA_get0_pub_key);

const BIGNUM *
DSA_get0_priv_key(const DSA *dsa)
{
	return dsa->priv_key;
}
LCRYPTO_ALIAS(DSA_get0_priv_key);

void
DSA_clear_flags(DSA *dsa, int flags)
{
	dsa->flags &= ~flags;
}
LCRYPTO_ALIAS(DSA_clear_flags);

int
DSA_test_flags(const DSA *dsa, int flags)
{
	return dsa->flags & flags;
}
LCRYPTO_ALIAS(DSA_test_flags);

void
DSA_set_flags(DSA *dsa, int flags)
{
	dsa->flags |= flags;
}
LCRYPTO_ALIAS(DSA_set_flags);

ENGINE *
DSA_get0_engine(DSA *dsa)
{
	return NULL;
}
LCRYPTO_ALIAS(DSA_get0_engine);

int
DSA_bits(const DSA *dsa)
{
	return BN_num_bits(dsa->p);
}
LCRYPTO_ALIAS(DSA_bits);

int
dsa_check_key(const DSA *dsa)
{
	int p_bits, q_bits;

	if (dsa->p == NULL || dsa->q == NULL || dsa->g == NULL) {
		DSAerror(DSA_R_MISSING_PARAMETERS);
		return 0;
	}

	/* Checking that p and q are primes is expensive. Check they are odd. */
	if (!BN_is_odd(dsa->p) || !BN_is_odd(dsa->q)) {
		DSAerror(DSA_R_INVALID_PARAMETERS);
		return 0;
	}

	/* FIPS 186-4: 1 < g < p. */
	if (BN_cmp(dsa->g, BN_value_one()) <= 0 ||
	    BN_cmp(dsa->g, dsa->p) >= 0) {
		DSAerror(DSA_R_INVALID_PARAMETERS);
		return 0;
	}

	/* We know p and g are positive. The next two checks imply q > 0. */
	if (BN_is_negative(dsa->q)) {
		DSAerror(DSA_R_BAD_Q_VALUE);
		return 0;
	}

	/* FIPS 186-4 only allows three sizes for q. */
	q_bits = BN_num_bits(dsa->q);
	if (q_bits != 160 && q_bits != 224 && q_bits != 256) {
		DSAerror(DSA_R_BAD_Q_VALUE);
		return 0;
	}

	/*
	 * XXX - FIPS 186-4 only allows 1024, 2048, and 3072 bits for p.
	 * Cap the size to reduce DoS risks. Poor defaults make keys with
	 * incorrect p sizes >= 512 bits common, so only enforce a weak
	 * lower bound.
	 */
	p_bits = BN_num_bits(dsa->p);
	if (p_bits > OPENSSL_DSA_MAX_MODULUS_BITS) {
		DSAerror(DSA_R_MODULUS_TOO_LARGE);
		return 0;
	}
	if (p_bits < 512) {
		DSAerror(DSA_R_INVALID_PARAMETERS);
		return 0;
	}

	/* The public key must be in the multiplicative group (mod p). */
	if (dsa->pub_key != NULL) {
		if (BN_cmp(dsa->pub_key, BN_value_one()) <= 0 ||
		    BN_cmp(dsa->pub_key, dsa->p) >= 0) {
			DSAerror(DSA_R_INVALID_PARAMETERS);
			return 0;
		}
	}

	/* The private key must be nonzero and in GF(q). */
	if (dsa->priv_key != NULL) {
		if (BN_cmp(dsa->priv_key, BN_value_one()) < 0 ||
		    BN_cmp(dsa->priv_key, dsa->q) >= 0) {
			DSAerror(DSA_R_INVALID_PARAMETERS);
			return 0;
		}
	}

	return 1;
}
