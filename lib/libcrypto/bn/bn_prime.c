/* $OpenBSD: bn_prime.c,v 1.35 2025/05/10 05:54:38 tb Exp $ */
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
 * Copyright (c) 1998-2001 The OpenSSL Project.  All rights reserved.
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

#include <stdio.h>
#include <time.h>

#include "bn_local.h"
#include "err_local.h"

/* The quick sieve algorithm approach to weeding out primes is
 * Philip Zimmermann's, as implemented in PGP.  I have had a read of
 * his comments and implemented my own version.
 */
#include "bn_prime.h"

static int probable_prime(BIGNUM *rnd, int bits);
static int probable_prime_dh(BIGNUM *rnd, int bits,
    const BIGNUM *add, const BIGNUM *rem, BN_CTX *ctx);
static int probable_prime_dh_safe(BIGNUM *rnd, int bits,
    const BIGNUM *add, const BIGNUM *rem, BN_CTX *ctx);

int
BN_GENCB_call(BN_GENCB *cb, int a, int b)
{
	/* No callback means continue */
	if (!cb)
		return 1;
	switch (cb->ver) {
	case 1:
		/* Deprecated-style callbacks */
		if (!cb->cb.cb_1)
			return 1;
		cb->cb.cb_1(a, b, cb->arg);
		return 1;
	case 2:
		/* New-style callbacks */
		return cb->cb.cb_2(a, b, cb);
	default:
		break;
	}
	/* Unrecognised callback type */
	return 0;
}
LCRYPTO_ALIAS(BN_GENCB_call);

int
BN_generate_prime_ex(BIGNUM *ret, int bits, int safe, const BIGNUM *add,
    const BIGNUM *rem, BN_GENCB *cb)
{
	BN_CTX *ctx;
	BIGNUM *p;
	int is_prime;
	int loops = 0;
	int found = 0;

	if (bits < 2 || (bits == 2 && safe)) {
		/*
		 * There are no prime numbers smaller than 2, and the smallest
		 * safe prime (7) spans three bits.
		 */
		BNerror(BN_R_BITS_TOO_SMALL);
		return 0;
	}

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;
	BN_CTX_start(ctx);
	if ((p = BN_CTX_get(ctx)) == NULL)
		goto err;

 loop:
	/* Make a random number and set the top and bottom bits. */
	if (add == NULL) {
		if (!probable_prime(ret, bits))
			goto err;
	} else {
		if (safe) {
			if (!probable_prime_dh_safe(ret, bits, add, rem, ctx))
				goto err;
		} else {
			if (!probable_prime_dh(ret, bits, add, rem, ctx))
				goto err;
		}
	}

	if (!BN_GENCB_call(cb, 0, loops++))
		goto err;

	if (!safe) {
		if (!bn_is_prime_bpsw(&is_prime, ret, ctx, 1))
			goto err;
		if (!is_prime)
			goto loop;
	} else {
		if (!bn_is_prime_bpsw(&is_prime, ret, ctx, 1))
			goto err;
		if (!is_prime)
			goto loop;

		/*
		 * For safe prime generation, check that p = (ret-1)/2 is prime.
		 * Since this prime has >= 3 bits, it is odd, and we can simply
		 * divide by 2.
		 */
		if (!BN_rshift1(p, ret))
			goto err;

		if (!bn_is_prime_bpsw(&is_prime, p, ctx, 1))
			goto err;
		if (!is_prime)
			goto loop;

		if (!BN_GENCB_call(cb, 2, loops - 1))
			goto err;
	}

	found = 1;

 err:
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);

	return found;
}
LCRYPTO_ALIAS(BN_generate_prime_ex);

int
BN_is_prime_ex(const BIGNUM *a, int checks, BN_CTX *ctx_passed, BN_GENCB *cb)
{
	return BN_is_prime_fasttest_ex(a, checks, ctx_passed, 0, cb);
}
LCRYPTO_ALIAS(BN_is_prime_ex);

#define BN_PRIME_MAXIMUM_BITS (32 * 1024)

int
BN_is_prime_fasttest_ex(const BIGNUM *a, int checks, BN_CTX *ctx_passed,
    int do_trial_division, BN_GENCB *cb)
{
	int is_prime;

	if (checks < 0)
		return -1;

	/*
	 * Prime numbers this large do not appear in everyday cryptography
	 * and checking such numbers for primality is very expensive.
	 */
	if (BN_num_bits(a) > BN_PRIME_MAXIMUM_BITS) {
		BNerror(BN_R_BIGNUM_TOO_LONG);
		return -1;
	}

	if (checks == BN_prime_checks)
		checks = BN_prime_checks_for_size(BN_num_bits(a));

	/* XXX - tickle BN_GENCB in bn_is_prime_bpsw(). */
	if (!bn_is_prime_bpsw(&is_prime, a, ctx_passed, checks))
		return -1;

	return is_prime;
}
LCRYPTO_ALIAS(BN_is_prime_fasttest_ex);

static int
probable_prime(BIGNUM *rnd, int bits)
{
	int i;
	BN_ULONG mods[NUMPRIMES];
	BN_ULONG delta, maxdelta;

again:
	if (!BN_rand(rnd, bits, 1, 1))
		return (0);
	/* we now have a random number 'rand' to test. */
	for (i = 1; i < NUMPRIMES; i++) {
		BN_ULONG mod = BN_mod_word(rnd, primes[i]);
		if (mod == (BN_ULONG)-1)
			return (0);
		mods[i] = mod;
	}
	maxdelta = BN_MASK2 - primes[NUMPRIMES - 1];
	delta = 0;
loop:
	for (i = 1; i < NUMPRIMES; i++) {
		/* check that rnd is not a prime and also
		 * that gcd(rnd-1,primes) == 1 (except for 2) */
		if (((mods[i] + delta) % primes[i]) <= 1) {
			delta += 2;
			if (delta > maxdelta)
				goto again;
			goto loop;
		}
	}
	if (!BN_add_word(rnd, delta))
		return (0);
	return (1);
}

static int
probable_prime_dh(BIGNUM *rnd, int bits, const BIGNUM *add, const BIGNUM *rem,
    BN_CTX *ctx)
{
	int i, ret = 0;
	BIGNUM *t1;

	BN_CTX_start(ctx);
	if ((t1 = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_rand(rnd, bits, 0, 1))
		goto err;

	/* we need ((rnd-rem) % add) == 0 */

	if (!BN_mod_ct(t1, rnd, add, ctx))
		goto err;
	if (!BN_sub(rnd, rnd, t1))
		goto err;
	if (rem == NULL) {
		if (!BN_add_word(rnd, 1))
			goto err;
	} else {
		if (!BN_add(rnd, rnd, rem))
			goto err;
	}

	/* we now have a random number 'rand' to test. */

loop:
	for (i = 1; i < NUMPRIMES; i++) {
		/* check that rnd is a prime */
		BN_LONG mod = BN_mod_word(rnd, primes[i]);
		if (mod == (BN_ULONG)-1)
			goto err;
		if (mod <= 1) {
			if (!BN_add(rnd, rnd, add))
				goto err;
			goto loop;
		}
	}
	ret = 1;

err:
	BN_CTX_end(ctx);
	return (ret);
}

static int
probable_prime_dh_safe(BIGNUM *p, int bits, const BIGNUM *padd,
    const BIGNUM *rem, BN_CTX *ctx)
{
	int i, ret = 0;
	BIGNUM *t1, *qadd, *q;

	bits--;
	BN_CTX_start(ctx);
	if ((t1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((q = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((qadd = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_rshift1(qadd, padd))
		goto err;

	if (!BN_rand(q, bits, 0, 1))
		goto err;

	/* we need ((rnd-rem) % add) == 0 */
	if (!BN_mod_ct(t1, q,qadd, ctx))
		goto err;
	if (!BN_sub(q, q, t1))
		goto err;
	if (rem == NULL) {
		if (!BN_add_word(q, 1))
			goto err;
	} else {
		if (!BN_rshift1(t1, rem))
			goto err;
		if (!BN_add(q, q, t1))
			goto err;
	}

	/* we now have a random number 'rand' to test. */
	if (!BN_lshift1(p, q))
		goto err;
	if (!BN_add_word(p, 1))
		goto err;

loop:
	for (i = 1; i < NUMPRIMES; i++) {
		/* check that p and q are prime */
		/* check that for p and q
		 * gcd(p-1,primes) == 1 (except for 2) */
		BN_ULONG pmod = BN_mod_word(p, primes[i]);
		BN_ULONG qmod = BN_mod_word(q, primes[i]);
		if (pmod == (BN_ULONG)-1 || qmod == (BN_ULONG)-1)
			goto err;
		if (pmod == 0 || qmod == 0) {
			if (!BN_add(p, p, padd))
				goto err;
			if (!BN_add(q, q, qadd))
				goto err;
			goto loop;
		}
	}
	ret = 1;

err:
	BN_CTX_end(ctx);
	return (ret);
}
