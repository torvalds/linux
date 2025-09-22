/* $OpenBSD: rsa_gen.c,v 1.31 2025/05/10 05:54:38 tb Exp $ */
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
#include <time.h>

#include <openssl/bn.h>
#include <openssl/rsa.h>

#include "bn_local.h"
#include "err_local.h"
#include "rsa_local.h"

static int rsa_builtin_keygen(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb);

int
RSA_generate_key_ex(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb)
{
	if (rsa->meth->rsa_keygen)
		return rsa->meth->rsa_keygen(rsa, bits, e_value, cb);
	return rsa_builtin_keygen(rsa, bits, e_value, cb);
}
LCRYPTO_ALIAS(RSA_generate_key_ex);

static int
rsa_builtin_keygen(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb)
{
	BIGNUM *r0 = NULL, *r1 = NULL, *r2 = NULL, *r3 = NULL, *tmp;
	BIGNUM pr0, d, p;
	int bitsp, bitsq, ok = -1, n = 0;
	BN_CTX *ctx = NULL;

	ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;
	BN_CTX_start(ctx);
	if ((r0 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r2 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r3 = BN_CTX_get(ctx)) == NULL)
		goto err;

	bitsp = (bits + 1) / 2;
	bitsq = bits - bitsp;

	/* We need the RSA components non-NULL */
	if (!rsa->n && ((rsa->n = BN_new()) == NULL))
		goto err;
	if (!rsa->d && ((rsa->d = BN_new()) == NULL))
		goto err;
	if (!rsa->e && ((rsa->e = BN_new()) == NULL))
		goto err;
	if (!rsa->p && ((rsa->p = BN_new()) == NULL))
		goto err;
	if (!rsa->q && ((rsa->q = BN_new()) == NULL))
		goto err;
	if (!rsa->dmp1 && ((rsa->dmp1 = BN_new()) == NULL))
		goto err;
	if (!rsa->dmq1 && ((rsa->dmq1 = BN_new()) == NULL))
		goto err;
	if (!rsa->iqmp && ((rsa->iqmp = BN_new()) == NULL))
		goto err;

	if (!bn_copy(rsa->e, e_value))
		goto err;

	/* generate p and q */
	for (;;) {
		if (!BN_generate_prime_ex(rsa->p, bitsp, 0, NULL, NULL, cb))
			goto err;
		if (!BN_sub(r2, rsa->p, BN_value_one()))
			goto err;
		if (!BN_gcd_ct(r1, r2, rsa->e, ctx))
			goto err;
		if (BN_is_one(r1))
			break;
		if (!BN_GENCB_call(cb, 2, n++))
			goto err;
	}
	if (!BN_GENCB_call(cb, 3, 0))
		goto err;
	for (;;) {
		/*
		 * When generating ridiculously small keys, we can get stuck
		 * continually regenerating the same prime values. Check for
		 * this and bail if it happens 3 times.
		 */
		unsigned int degenerate = 0;
		do {
			if (!BN_generate_prime_ex(rsa->q, bitsq, 0, NULL, NULL,
			    cb))
				goto err;
		} while (BN_cmp(rsa->p, rsa->q) == 0 &&
		    ++degenerate < 3);
		if (degenerate == 3) {
			ok = 0; /* we set our own err */
			RSAerror(RSA_R_KEY_SIZE_TOO_SMALL);
			goto err;
		}
		if (!BN_sub(r2, rsa->q, BN_value_one()))
			goto err;
		if (!BN_gcd_ct(r1, r2, rsa->e, ctx))
			goto err;
		if (BN_is_one(r1))
			break;
		if (!BN_GENCB_call(cb, 2, n++))
			goto err;
	}
	if (!BN_GENCB_call(cb, 3, 1))
		goto err;
	if (BN_cmp(rsa->p, rsa->q) < 0) {
		tmp = rsa->p;
		rsa->p = rsa->q;
		rsa->q = tmp;
	}

	/* calculate n */
	if (!BN_mul(rsa->n, rsa->p, rsa->q, ctx))
		goto err;

	/* calculate d */
	if (!BN_sub(r1, rsa->p, BN_value_one()))	/* p-1 */
		goto err;
	if (!BN_sub(r2, rsa->q, BN_value_one()))	/* q-1 */
		goto err;
	if (!BN_mul(r0, r1, r2, ctx))			/* (p-1)(q-1) */
		goto err;

	BN_init(&pr0);
	BN_with_flags(&pr0, r0, BN_FLG_CONSTTIME);

	if (BN_mod_inverse_ct(rsa->d, rsa->e, &pr0, ctx) == NULL) /* d */
		goto err;

	/* set up d for correct BN_FLG_CONSTTIME flag */
	BN_init(&d);
	BN_with_flags(&d, rsa->d, BN_FLG_CONSTTIME);

	/* calculate d mod (p-1) */
	if (!BN_mod_ct(rsa->dmp1, &d, r1, ctx))
		goto err;

	/* calculate d mod (q-1) */
	if (!BN_mod_ct(rsa->dmq1, &d, r2, ctx))
		goto err;

	/* calculate inverse of q mod p */
	BN_init(&p);
	BN_with_flags(&p, rsa->p, BN_FLG_CONSTTIME);
	if (BN_mod_inverse_ct(rsa->iqmp, rsa->q, &p, ctx) == NULL)
		goto err;

	ok = 1;
err:
	if (ok == -1) {
		RSAerror(ERR_LIB_BN);
		ok = 0;
	}
	if (ctx != NULL) {
		BN_CTX_end(ctx);
		BN_CTX_free(ctx);
	}

	return ok;
}

RSA *
RSA_generate_key(int bits, unsigned long e_value,
    void (*callback)(int, int, void *), void *cb_arg)
{
	BN_GENCB cb;
	int i;
	RSA *rsa = RSA_new();
	BIGNUM *e = BN_new();

	if (!rsa || !e)
		goto err;

	/* The problem is when building with 8, 16, or 32 BN_ULONG,
	 * unsigned long can be larger */
	for (i = 0; i < (int)sizeof(unsigned long) * 8; i++) {
		if (e_value & (1UL << i))
			if (BN_set_bit(e, i) == 0)
				goto err;
	}

	BN_GENCB_set_old(&cb, callback, cb_arg);

	if (RSA_generate_key_ex(rsa, bits, e, &cb)) {
		BN_free(e);
		return rsa;
	}
err:
	BN_free(e);
	RSA_free(rsa);

	return 0;
}
LCRYPTO_ALIAS(RSA_generate_key);
