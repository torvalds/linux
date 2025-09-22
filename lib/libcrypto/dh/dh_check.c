/* $OpenBSD: dh_check.c,v 1.31 2025/05/10 05:54:38 tb Exp $ */
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

#define DH_NUMBER_ITERATIONS_FOR_PRIME 64

/*
 * Check that p is odd and 1 < g < p - 1.
 */

static int
DH_check_params(const DH *dh, int *flags)
{
	BIGNUM *max_g = NULL;
	int ok = 0;

	*flags = 0;

	if (!BN_is_odd(dh->p))
		*flags |= DH_CHECK_P_NOT_PRIME;

	/*
	 * Check that 1 < dh->g < p - 1
	 */

	if (BN_cmp(dh->g, BN_value_one()) <= 0)
		*flags |= DH_NOT_SUITABLE_GENERATOR;
	/* max_g = p - 1 */
	if ((max_g = BN_dup(dh->p)) == NULL)
		goto err;
	if (!BN_sub_word(max_g, 1))
		goto err;
	/* check that g < max_g */
	if (BN_cmp(dh->g, max_g) >= 0)
		*flags |= DH_NOT_SUITABLE_GENERATOR;

	ok = 1;

 err:
	BN_free(max_g);

	return ok;
}

/*
 * Check that p is a safe prime and that g is a suitable generator.
 */

int
DH_check(const DH *dh, int *flags)
{
	BN_CTX *ctx = NULL;
	int is_prime;
	int ok = 0;

	*flags = 0;

	if (!DH_check_params(dh, flags))
		goto err;

	ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;
	BN_CTX_start(ctx);

	if (dh->q != NULL) {
		BIGNUM *residue;

		if ((residue = BN_CTX_get(ctx)) == NULL)
			goto err;
		if ((*flags & DH_NOT_SUITABLE_GENERATOR) == 0) {
			/* Check g^q == 1 mod p */
			if (!BN_mod_exp_ct(residue, dh->g, dh->q, dh->p, ctx))
				goto err;
			if (!BN_is_one(residue))
				*flags |= DH_NOT_SUITABLE_GENERATOR;
		}
		is_prime = BN_is_prime_ex(dh->q, DH_NUMBER_ITERATIONS_FOR_PRIME,
		    ctx, NULL);
		if (is_prime < 0)
			goto err;
		if (is_prime == 0)
			*flags |= DH_CHECK_Q_NOT_PRIME;
		/* Check p == 1 mod q, i.e., q divides p - 1 */
		if (!BN_div_ct(NULL, residue, dh->p, dh->q, ctx))
			goto err;
		if (!BN_is_one(residue))
			*flags |= DH_CHECK_INVALID_Q_VALUE;
	}

	is_prime = BN_is_prime_ex(dh->p, DH_NUMBER_ITERATIONS_FOR_PRIME,
	    ctx, NULL);
	if (is_prime < 0)
		goto err;
	if (is_prime == 0)
		*flags |= DH_CHECK_P_NOT_PRIME;
	else if (dh->q == NULL) {
		BIGNUM *q;

		if ((q = BN_CTX_get(ctx)) == NULL)
			goto err;
		if (!BN_rshift1(q, dh->p))
			goto err;
		is_prime = BN_is_prime_ex(q, DH_NUMBER_ITERATIONS_FOR_PRIME,
		    ctx, NULL);
		if (is_prime < 0)
			goto err;
		if (is_prime == 0)
			*flags |= DH_CHECK_P_NOT_SAFE_PRIME;
	}

	ok = 1;

 err:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	return ok;
}
LCRYPTO_ALIAS(DH_check);

int
DH_check_pub_key(const DH *dh, const BIGNUM *pub_key, int *flags)
{
	BN_CTX *ctx = NULL;
	BIGNUM *max_pub_key;
	int ok = 0;

	*flags = 0;

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;
	BN_CTX_start(ctx);
	if ((max_pub_key = BN_CTX_get(ctx)) == NULL)
		goto err;

	/*
	 * Check that 1 < pub_key < dh->p - 1
	 */

	if (BN_cmp(pub_key, BN_value_one()) <= 0)
		*flags |= DH_CHECK_PUBKEY_TOO_SMALL;

	/* max_pub_key = dh->p - 1 */
	if (!BN_sub(max_pub_key, dh->p, BN_value_one()))
		goto err;

	if (BN_cmp(pub_key, max_pub_key) >= 0)
		*flags |= DH_CHECK_PUBKEY_TOO_LARGE;

	/*
	 * If dh->q is set, check that pub_key^q == 1 mod p
	 */

	if (dh->q != NULL) {
		BIGNUM *residue;

		if ((residue = BN_CTX_get(ctx)) == NULL)
			goto err;

		if (!BN_mod_exp_ct(residue, pub_key, dh->q, dh->p, ctx))
			goto err;
		if (!BN_is_one(residue))
			*flags |= DH_CHECK_PUBKEY_INVALID;
	}

	ok = 1;

 err:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return ok;
}
LCRYPTO_ALIAS(DH_check_pub_key);
