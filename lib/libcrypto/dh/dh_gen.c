/* $OpenBSD: dh_gen.c,v 1.22 2025/05/10 05:54:38 tb Exp $ */
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

static int dh_builtin_genparams(DH *ret, int prime_len, int generator,
	    BN_GENCB *cb);

int
DH_generate_parameters_ex(DH *ret, int prime_len, int generator, BN_GENCB *cb)
{
	if (ret->meth->generate_params)
		return ret->meth->generate_params(ret, prime_len, generator, cb);
	return dh_builtin_genparams(ret, prime_len, generator, cb);
}
LCRYPTO_ALIAS(DH_generate_parameters_ex);

/*
 * We generate DH parameters as follows:
 * find a prime q which is prime_len/2 bits long.
 * p=(2*q)+1 or (p-1)/2 = q
 * For this case, g is a generator if
 * g^((p-1)/q) mod p != 1 for values of q which are the factors of p-1.
 * Since the factors of p-1 are q and 2, we just need to check
 * g^2 mod p != 1 and g^q mod p != 1.
 *
 * Having said all that,
 * there is another special case method for the generators 2, 3 and 5.
 * for 2, p mod 24 == 11
 * for 3, p mod 12 == 5  <<<<< does not work for safe primes.
 * for 5, p mod 10 == 3 or 7
 *
 * Thanks to Phil Karn <karn@qualcomm.com> for the pointers about the
 * special generators and for answering some of my questions.
 *
 * I've implemented the second simple method :-).
 * Since DH should be using a safe prime (both p and q are prime),
 * this generator function can take a very very long time to run.
 */
/* Actually there is no reason to insist that 'generator' be a generator.
 * It's just as OK (and in some sense better) to use a generator of the
 * order-q subgroup.
 */
static int
dh_builtin_genparams(DH *ret, int prime_len, int generator, BN_GENCB *cb)
{
	BIGNUM *t1, *t2;
	int g, ok = -1;
	BN_CTX *ctx = NULL;

	ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;
	BN_CTX_start(ctx);
	if ((t1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((t2 = BN_CTX_get(ctx)) == NULL)
		goto err;

	/* Make sure 'ret' has the necessary elements */
	if (!ret->p && ((ret->p = BN_new()) == NULL))
		goto err;
	if (!ret->g && ((ret->g = BN_new()) == NULL))
		goto err;

	if (generator <= 1) {
		DHerror(DH_R_BAD_GENERATOR);
		goto err;
	}
	if (generator == DH_GENERATOR_2) {
		if (!BN_set_word(t1, 24))
			goto err;
		if (!BN_set_word(t2, 11))
			goto err;
		g = 2;
	} else if (generator == DH_GENERATOR_5) {
		if (!BN_set_word(t1, 10))
			goto err;
		if (!BN_set_word(t2, 3))
			goto err;
		/* BN_set_word(t3,7); just have to miss
		 * out on these ones :-( */
		g = 5;
	} else {
		/*
		 * in the general case, don't worry if 'generator' is a
		 * generator or not: since we are using safe primes,
		 * it will generate either an order-q or an order-2q group,
		 * which both is OK
		 */
		if (!BN_set_word(t1, 2))
			goto err;
		if (!BN_set_word(t2, 1))
			goto err;
		g = generator;
	}

	if (!BN_generate_prime_ex(ret->p, prime_len, 1, t1, t2, cb))
		goto err;
	if (!BN_GENCB_call(cb, 3, 0))
		goto err;
	if (!BN_set_word(ret->g, g))
		goto err;
	ok = 1;
err:
	if (ok == -1) {
		DHerror(ERR_R_BN_LIB);
		ok = 0;
	}

	if (ctx != NULL) {
		BN_CTX_end(ctx);
		BN_CTX_free(ctx);
	}
	return ok;
}

DH *
DH_generate_parameters(int prime_len, int generator,
    void (*callback)(int, int, void *), void *cb_arg)
{
	BN_GENCB cb;
	DH *ret = NULL;

	if ((ret = DH_new()) == NULL)
		return NULL;

	BN_GENCB_set_old(&cb, callback, cb_arg);

	if (DH_generate_parameters_ex(ret, prime_len, generator, &cb))
		return ret;
	DH_free(ret);
	return NULL;
}
LCRYPTO_ALIAS(DH_generate_parameters);
