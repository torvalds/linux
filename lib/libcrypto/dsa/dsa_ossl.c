/* $OpenBSD: dsa_ossl.c,v 1.57 2025/05/10 05:54:38 tb Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/dsa.h>
#include <openssl/sha.h>

#include "bn_local.h"
#include "dsa_local.h"
#include "err_local.h"

/*
 * Since DSA parameters are entirely arbitrary and checking them to be
 * consistent is very expensive, we cannot do so on every sign operation.
 * Instead, cap the number of retries so we do not loop indefinitely if
 * the generator of the multiplicative group happens to be nilpotent.
 * The probability of needing a retry with valid parameters is negligible,
 * so trying 32 times is amply enough.
 */
#define DSA_MAX_SIGN_ITERATIONS		32

static DSA_SIG *
dsa_do_sign(const unsigned char *dgst, int dlen, DSA *dsa)
{
	BIGNUM *b = NULL, *bm = NULL, *bxr = NULL, *binv = NULL, *m = NULL;
	BIGNUM *kinv = NULL, *r = NULL, *s = NULL;
	BN_CTX *ctx = NULL;
	int reason = ERR_R_BN_LIB;
	DSA_SIG *ret = NULL;
	int attempts = 0;
	int noredo = 0;

	if (!dsa_check_key(dsa)) {
		reason = DSA_R_INVALID_PARAMETERS;
		goto err;
	}

	if ((s = BN_new()) == NULL)
		goto err;

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((b = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((binv = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((bm = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((bxr = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((m = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * If the digest length is greater than N (the bit length of q), the
	 * leftmost N bits of the digest shall be used, see FIPS 186-3, 4.2.
	 * In this case the digest length is given in bytes.
	 */
	if (dlen > BN_num_bytes(dsa->q))
		dlen = BN_num_bytes(dsa->q);
	if (BN_bin2bn(dgst, dlen, m) == NULL)
		goto err;

 redo:
	if (dsa->kinv == NULL || dsa->r == NULL) {
		if (!DSA_sign_setup(dsa, ctx, &kinv, &r))
			goto err;
	} else {
		kinv = dsa->kinv;
		dsa->kinv = NULL;
		r = dsa->r;
		dsa->r = NULL;
		noredo = 1;
	}

	/*
	 * Compute:
	 *
	 *  s = inv(k)(m + xr) mod q
	 *
	 * In order to reduce the possibility of a side-channel attack, the
	 * following is calculated using a blinding value:
	 *
	 *  s = inv(b)(bm + bxr)inv(k) mod q
	 *
	 * Where b is a random value in the range [1, q).
	 */
	if (!bn_rand_interval(b, 1, dsa->q))
		goto err;
	if (BN_mod_inverse_ct(binv, b, dsa->q, ctx) == NULL)
		goto err;

	if (!BN_mod_mul(bxr, b, dsa->priv_key, dsa->q, ctx))	/* bx */
		goto err;
	if (!BN_mod_mul(bxr, bxr, r, dsa->q, ctx))	/* bxr */
		goto err;
	if (!BN_mod_mul(bm, b, m, dsa->q, ctx))		/* bm */
		goto err;
	if (!BN_mod_add(s, bxr, bm, dsa->q, ctx))	/* s = bm + bxr */
		goto err;
	if (!BN_mod_mul(s, s, kinv, dsa->q, ctx))	/* s = b(m + xr)k^-1 */
		goto err;
	if (!BN_mod_mul(s, s, binv, dsa->q, ctx))	/* s = (m + xr)k^-1 */
		goto err;

	/*
	 * Redo if r or s is zero as required by FIPS 186-3: this is very
	 * unlikely.
	 */
	if (BN_is_zero(r) || BN_is_zero(s)) {
		if (noredo) {
			reason = DSA_R_NEED_NEW_SETUP_VALUES;
			goto err;
		}
		if (++attempts > DSA_MAX_SIGN_ITERATIONS) {
			reason = DSA_R_INVALID_PARAMETERS;
			goto err;
		}
		goto redo;
	}

	if ((ret = DSA_SIG_new()) == NULL) {
		reason = ERR_R_MALLOC_FAILURE;
		goto err;
	}
	ret->r = r;
	ret->s = s;

 err:
	if (!ret) {
		DSAerror(reason);
		BN_free(r);
		BN_free(s);
	}
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	BN_free(kinv);

	return ret;
}

static int
dsa_sign_setup(DSA *dsa, BN_CTX *ctx_in, BIGNUM **kinvp, BIGNUM **rp)
{
	BIGNUM *k = NULL, *l = NULL, *m = NULL, *kinv = NULL, *r = NULL;
	BN_CTX *ctx = NULL;
	int q_bits;
	int ret = 0;

	if (!dsa_check_key(dsa))
		goto err;

	if ((r = BN_new()) == NULL)
		goto err;

	if ((ctx = ctx_in) == NULL)
		ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((k = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((l = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((m = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* Preallocate space */
	q_bits = BN_num_bits(dsa->q);
	if (!BN_set_bit(k, q_bits) ||
	    !BN_set_bit(l, q_bits) ||
	    !BN_set_bit(m, q_bits))
		goto err;

	if (!bn_rand_interval(k, 1, dsa->q))
		goto err;

	BN_set_flags(k, BN_FLG_CONSTTIME);

	if (dsa->flags & DSA_FLAG_CACHE_MONT_P) {
		if (!BN_MONT_CTX_set_locked(&dsa->method_mont_p,
		    CRYPTO_LOCK_DSA, dsa->p, ctx))
			goto err;
	}

	/* Compute r = (g^k mod p) mod q */

	/*
	 * We do not want timing information to leak the length of k,
	 * so we compute G^k using an equivalent exponent of fixed
	 * bit-length.
	 *
	 * We unconditionally perform both of these additions to prevent a
	 * small timing information leakage.  We then choose the sum that is
	 * one bit longer than the modulus.
	 *
	 * TODO: revisit the bn_copy aiming for a memory access agnostic
	 * conditional copy.
	 */

	if (!BN_add(l, k, dsa->q) ||
	    !BN_add(m, l, dsa->q) ||
	    !bn_copy(k, BN_num_bits(l) > q_bits ? l : m))
		goto err;

	if (!BN_mod_exp_mont_ct(r, dsa->g, k, dsa->p, ctx, dsa->method_mont_p))
		goto err;

	if (!BN_mod_ct(r, r, dsa->q, ctx))
		goto err;

	/* Compute  part of 's = inv(k) (m + xr) mod q' */
	if ((kinv = BN_mod_inverse_ct(NULL, k, dsa->q, ctx)) == NULL)
		goto err;

	BN_free(*kinvp);
	*kinvp = kinv;
	kinv = NULL;

	BN_free(*rp);
	*rp = r;

	ret = 1;

 err:
	if (!ret) {
		DSAerror(ERR_R_BN_LIB);
		BN_free(r);
	}
	BN_CTX_end(ctx);
	if (ctx != ctx_in)
		BN_CTX_free(ctx);

	return ret;
}

static int
dsa_do_verify(const unsigned char *dgst, int dgst_len, DSA_SIG *sig, DSA *dsa)
{
	BIGNUM *u1 = NULL, *u2 = NULL, *t1 = NULL;
	BN_CTX *ctx = NULL;
	BN_MONT_CTX *mont = NULL;
	int qbits;
	int ret = -1;

	if (!dsa_check_key(dsa))
		goto err;

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((u1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((u2 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((t1 = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (BN_is_zero(sig->r) || BN_is_negative(sig->r) ||
	    BN_ucmp(sig->r, dsa->q) >= 0) {
		ret = 0;
		goto err;
	}
	if (BN_is_zero(sig->s) || BN_is_negative(sig->s) ||
	    BN_ucmp(sig->s, dsa->q) >= 0) {
		ret = 0;
		goto err;
	}

	/* Calculate w = inv(s) mod q, saving w in u2. */
	if ((BN_mod_inverse_ct(u2, sig->s, dsa->q, ctx)) == NULL)
		goto err;

	/*
	 * If the digest length is greater than the size of q use the
	 * BN_num_bits(dsa->q) leftmost bits of the digest, see FIPS 186-4, 4.2.
	 */
	qbits = BN_num_bits(dsa->q);
	if (dgst_len > (qbits >> 3))
		dgst_len = (qbits >> 3);

	/* Save m in u1. */
	if (BN_bin2bn(dgst, dgst_len, u1) == NULL)
		goto err;

	/* u1 = m * w mod q */
	if (!BN_mod_mul(u1, u1, u2, dsa->q, ctx))
		goto err;

	/* u2 = r * w mod q */
	if (!BN_mod_mul(u2, sig->r, u2, dsa->q, ctx))
		goto err;

	if (dsa->flags & DSA_FLAG_CACHE_MONT_P) {
		mont = BN_MONT_CTX_set_locked(&dsa->method_mont_p,
		    CRYPTO_LOCK_DSA, dsa->p, ctx);
		if (!mont)
			goto err;
	}

	if (!BN_mod_exp2_mont(t1, dsa->g, u1, dsa->pub_key, u2, dsa->p,
	    ctx, mont))
		goto err;

	/* let u1 = u1 mod q */
	if (!BN_mod_ct(u1, t1, dsa->q, ctx))
		goto err;

	/* v is in u1 - if the signature is correct, it will be equal to r. */
	ret = BN_ucmp(u1, sig->r) == 0;

 err:
	if (ret < 0)
		DSAerror(ERR_R_BN_LIB);
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return ret;
}

static int
dsa_init(DSA *dsa)
{
	dsa->flags |= DSA_FLAG_CACHE_MONT_P;
	return 1;
}

static int
dsa_finish(DSA *dsa)
{
	BN_MONT_CTX_free(dsa->method_mont_p);
	return 1;
}

static const DSA_METHOD openssl_dsa_meth = {
	.name = "OpenSSL DSA method",
	.dsa_do_sign = dsa_do_sign,
	.dsa_sign_setup = dsa_sign_setup,
	.dsa_do_verify = dsa_do_verify,
	.init = dsa_init,
	.finish = dsa_finish,
};

const DSA_METHOD *
DSA_OpenSSL(void)
{
	return &openssl_dsa_meth;
}
LCRYPTO_ALIAS(DSA_OpenSSL);

DSA_SIG *
DSA_SIG_new(void)
{
	return calloc(1, sizeof(DSA_SIG));
}
LCRYPTO_ALIAS(DSA_SIG_new);

void
DSA_SIG_free(DSA_SIG *sig)
{
	if (sig == NULL)
		return;

	BN_free(sig->r);
	BN_free(sig->s);
	free(sig);
}
LCRYPTO_ALIAS(DSA_SIG_free);

int
DSA_sign_setup(DSA *dsa, BN_CTX *ctx_in, BIGNUM **kinvp, BIGNUM **rp)
{
	return dsa->meth->dsa_sign_setup(dsa, ctx_in, kinvp, rp);
}
LCRYPTO_ALIAS(DSA_sign_setup);

DSA_SIG *
DSA_do_sign(const unsigned char *dgst, int dlen, DSA *dsa)
{
	return dsa->meth->dsa_do_sign(dgst, dlen, dsa);
}
LCRYPTO_ALIAS(DSA_do_sign);

int
DSA_do_verify(const unsigned char *dgst, int dgst_len, DSA_SIG *sig, DSA *dsa)
{
	return dsa->meth->dsa_do_verify(dgst, dgst_len, sig, dsa);
}
LCRYPTO_ALIAS(DSA_do_verify);
