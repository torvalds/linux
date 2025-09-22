/* $OpenBSD: dh_key.c,v 1.43 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/bn.h>
#include <openssl/dh.h>

#include "bn_local.h"
#include "dh_local.h"
#include "err_local.h"

static int
generate_key(DH *dh)
{
	int ok = 0;
	unsigned l;
	BN_CTX *ctx;
	BN_MONT_CTX *mont = NULL;
	BIGNUM *pub_key = NULL, *priv_key = NULL;

	if (BN_num_bits(dh->p) > OPENSSL_DH_MAX_MODULUS_BITS) {
		DHerror(DH_R_MODULUS_TOO_LARGE);
		return 0;
	}

	ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	if ((priv_key = dh->priv_key) == NULL) {
		if ((priv_key = BN_new()) == NULL)
			goto err;
	}

	if ((pub_key = dh->pub_key) == NULL) {
		if ((pub_key = BN_new()) == NULL)
			goto err;
	}

	if (dh->flags & DH_FLAG_CACHE_MONT_P) {
		mont = BN_MONT_CTX_set_locked(&dh->method_mont_p,
		    CRYPTO_LOCK_DH, dh->p, ctx);
		if (!mont)
			goto err;
	}

	if (dh->priv_key == NULL) {
		if (dh->q) {
			if (!bn_rand_interval(priv_key, 2, dh->q))
				goto err;
		} else {
			/* secret exponent length */
			l = dh->length ? dh->length : BN_num_bits(dh->p) - 1;
			if (!BN_rand(priv_key, l, 0, 0))
				goto err;
		}
	}

	if (!dh->meth->bn_mod_exp(dh, pub_key, dh->g, priv_key, dh->p, ctx,
	    mont))
		goto err;

	dh->pub_key = pub_key;
	dh->priv_key = priv_key;
	ok = 1;
 err:
	if (ok != 1)
		DHerror(ERR_R_BN_LIB);

	if (dh->pub_key == NULL)
		BN_free(pub_key);
	if (dh->priv_key == NULL)
		BN_free(priv_key);
	BN_CTX_free(ctx);

	return ok;
}

static int
compute_key(unsigned char *key, const BIGNUM *pub_key, DH *dh)
{
	BN_CTX *ctx = NULL;
	BN_MONT_CTX *mont = NULL;
	BIGNUM *tmp;
	int ret = -1;
        int check_result;

	if (BN_num_bits(dh->p) > OPENSSL_DH_MAX_MODULUS_BITS) {
		DHerror(DH_R_MODULUS_TOO_LARGE);
		goto err;
	}

	ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;
	BN_CTX_start(ctx);
	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (dh->priv_key == NULL) {
		DHerror(DH_R_NO_PRIVATE_VALUE);
		goto err;
	}

	if (dh->flags & DH_FLAG_CACHE_MONT_P) {
		mont = BN_MONT_CTX_set_locked(&dh->method_mont_p,
		    CRYPTO_LOCK_DH, dh->p, ctx);

		BN_set_flags(dh->priv_key, BN_FLG_CONSTTIME);

		if (!mont)
			goto err;
	}

        if (!DH_check_pub_key(dh, pub_key, &check_result) || check_result) {
		DHerror(DH_R_INVALID_PUBKEY);
		goto err;
	}

	if (!dh->meth->bn_mod_exp(dh, tmp, pub_key, dh->priv_key, dh->p, ctx,
	    mont)) {
		DHerror(ERR_R_BN_LIB);
		goto err;
	}

	ret = BN_bn2bin(tmp, key);
 err:
	if (ctx != NULL) {
		BN_CTX_end(ctx);
		BN_CTX_free(ctx);
	}
	return ret;
}

static int
dh_bn_mod_exp(const DH *dh, BIGNUM *r, const BIGNUM *a, const BIGNUM *p,
    const BIGNUM *m, BN_CTX *ctx, BN_MONT_CTX *m_ctx)
{
	return BN_mod_exp_mont_ct(r, a, p, m, ctx, m_ctx);
}

static int
dh_init(DH *dh)
{
	dh->flags |= DH_FLAG_CACHE_MONT_P;
	return 1;
}

static int
dh_finish(DH *dh)
{
	BN_MONT_CTX_free(dh->method_mont_p);
	return 1;
}

int
DH_generate_key(DH *dh)
{
	return dh->meth->generate_key(dh);
}
LCRYPTO_ALIAS(DH_generate_key);

int
DH_compute_key(unsigned char *key, const BIGNUM *pub_key, DH *dh)
{
	return dh->meth->compute_key(key, pub_key, dh);
}
LCRYPTO_ALIAS(DH_compute_key);

static const DH_METHOD dh_ossl = {
	.name = "OpenSSL DH Method",
	.generate_key = generate_key,
	.compute_key = compute_key,
	.bn_mod_exp = dh_bn_mod_exp,
	.init = dh_init,
	.finish = dh_finish,
};

const DH_METHOD *
DH_OpenSSL(void)
{
	return &dh_ossl;
}
LCRYPTO_ALIAS(DH_OpenSSL);
