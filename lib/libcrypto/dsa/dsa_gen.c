/* $OpenBSD: dsa_gen.c,v 1.34 2025/02/13 11:18:00 tb Exp $ */
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

#include <openssl/opensslconf.h> /* To see if OPENSSL_NO_SHA is defined */

#ifndef OPENSSL_NO_SHA

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include "bn_local.h"
#include "dsa_local.h"

/*
 * Primality test according to FIPS PUB 186-4, Appendix C.3. Set the number
 * to 64 rounds of Miller-Rabin, which corresponds to 128 bits of security.
 * This is necessary for keys of size >= 3072.
 * XXX - now that we do BPSW the recommendation is to do 2 for p and 27 for q.
 */
#define DSA_prime_checks 64

int
DSA_generate_parameters_ex(DSA *ret, int bits, const unsigned char *seed_in,
    int seed_len, int *counter_ret, unsigned long *h_ret, BN_GENCB *cb)
{
	const EVP_MD *evpmd;
	size_t qbits;

	if (bits >= 2048) {
		qbits = 256;
		evpmd = EVP_sha256();
	} else {
		qbits = 160;
		evpmd = EVP_sha1();
	}

	return dsa_builtin_paramgen(ret, bits, qbits, evpmd, seed_in, seed_len,
	    NULL, counter_ret, h_ret, cb);
}
LCRYPTO_ALIAS(DSA_generate_parameters_ex);

int
dsa_builtin_paramgen(DSA *ret, size_t bits, size_t qbits, const EVP_MD *evpmd,
    const unsigned char *seed_in, size_t seed_len, unsigned char *seed_out,
    int *counter_ret, unsigned long *h_ret, BN_GENCB *cb)
{
	int ok = 0;
	unsigned char seed[SHA256_DIGEST_LENGTH];
	unsigned char md[SHA256_DIGEST_LENGTH];
	unsigned char buf[SHA256_DIGEST_LENGTH], buf2[SHA256_DIGEST_LENGTH];
	BIGNUM *r0, *W, *X, *c, *test;
	BIGNUM *g = NULL, *q = NULL, *p = NULL;
	BN_MONT_CTX *mont = NULL;
	int i, k, n = 0, m = 0, qsize = qbits >> 3;
	int counter = 0;
	int r = 0;
	BN_CTX *ctx = NULL;
	unsigned int h = 2;

	if (qsize != SHA_DIGEST_LENGTH && qsize != SHA224_DIGEST_LENGTH &&
	    qsize != SHA256_DIGEST_LENGTH)
		/* invalid q size */
		return 0;

	if (evpmd == NULL)
		/* use SHA1 as default */
		evpmd = EVP_sha1();

	if (bits < 512)
		bits = 512;

	bits = (bits + 63) / 64 * 64;

	if (seed_len < (size_t)qsize) {
		seed_in = NULL;		/* seed buffer too small -- ignore */
		seed_len = 0;
	}
	/*
	 * App. 2.2 of FIPS PUB 186 allows larger SEED,
	 * but our internal buffers are restricted to 160 bits
	 */
	if (seed_len > (size_t)qsize)
		seed_len = qsize;
	if (seed_in != NULL)
		memcpy(seed, seed_in, seed_len);
	else if (seed_len != 0)
		goto err;

	if ((ctx = BN_CTX_new()) == NULL)
		goto err;

	BN_CTX_start(ctx);

	if ((r0 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((g = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((W = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((q = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((X = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((c = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((p = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((test = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_lshift(test, BN_value_one(), bits - 1))
		goto err;

	for (;;) {
		for (;;) { /* find q */
			int seed_is_random;

			/* step 1 */
			if (!BN_GENCB_call(cb, 0, m++))
				goto err;

			if (seed_len == 0) {
				arc4random_buf(seed, qsize);
				seed_is_random = 1;
			} else {
				seed_is_random = 0;
				/* use random seed if 'seed_in' turns out
				   to be bad */
				seed_len = 0;
			}
			memcpy(buf, seed, qsize);
			memcpy(buf2, seed, qsize);
			/* precompute "SEED + 1" for step 7: */
			for (i = qsize - 1; i >= 0; i--) {
				buf[i]++;
				if (buf[i] != 0)
					break;
			}

			/* step 2 */
			if (!EVP_Digest(seed, qsize, md,   NULL, evpmd, NULL))
				goto err;
			if (!EVP_Digest(buf,  qsize, buf2, NULL, evpmd, NULL))
				goto err;
			for (i = 0; i < qsize; i++)
				md[i] ^= buf2[i];

			/* step 3 */
			md[0] |= 0x80;
			md[qsize - 1] |= 0x01;
			if (!BN_bin2bn(md, qsize, q))
				goto err;

			/* step 4 */
			r = BN_is_prime_fasttest_ex(q, DSA_prime_checks, ctx,
			    seed_is_random, cb);
			if (r > 0)
				break;
			if (r != 0)
				goto err;

			/* do a callback call */
			/* step 5 */
		}

		if (!BN_GENCB_call(cb, 2, 0))
			goto err;
		if (!BN_GENCB_call(cb, 3, 0))
			goto err;

		/* step 6 */
		counter = 0;
		/* "offset = 2" */

		n = (bits - 1) / 160;

		for (;;) {
			if (counter != 0 && !BN_GENCB_call(cb, 0, counter))
				goto err;

			/* step 7 */
			BN_zero(W);
			/* now 'buf' contains "SEED + offset - 1" */
			for (k = 0; k <= n; k++) {
				/* obtain "SEED + offset + k" by incrementing: */
				for (i = qsize - 1; i >= 0; i--) {
					buf[i]++;
					if (buf[i] != 0)
						break;
				}

				if (!EVP_Digest(buf, qsize, md ,NULL, evpmd,
				    NULL))
					goto err;

				/* step 8 */
				if (!BN_bin2bn(md, qsize, r0))
					goto err;
				if (!BN_lshift(r0, r0, (qsize << 3) * k))
					goto err;
				if (!BN_add(W, W, r0))
					goto err;
			}

			/* more of step 8 */
			if (!BN_mask_bits(W, bits - 1))
				goto err;
			if (!bn_copy(X, W))
				goto err;
			if (!BN_add(X, X, test))
				goto err;

			/* step 9 */
			if (!BN_lshift1(r0, q))
				goto err;
			if (!BN_mod_ct(c, X, r0, ctx))
				goto err;
			if (!BN_sub(r0, c, BN_value_one()))
				goto err;
			if (!BN_sub(p, X, r0))
				goto err;

			/* step 10 */
			if (BN_cmp(p, test) >= 0) {
				/* step 11 */
				r = BN_is_prime_fasttest_ex(p, DSA_prime_checks,
				    ctx, 1, cb);
				if (r > 0)
					goto end; /* found it */
				if (r != 0)
					goto err;
			}

			/* step 13 */
			counter++;
			/* "offset = offset + n + 1" */

			/* step 14 */
			if (counter >= 4096)
				break;
		}
	}
end:
	if (!BN_GENCB_call(cb, 2, 1))
		goto err;

	/* We now need to generate g */
	/* Set r0=(p-1)/q */
	if (!BN_sub(test, p, BN_value_one()))
		goto err;
	if (!BN_div_ct(r0, NULL, test, q, ctx))
		goto err;

	if (!BN_set_word(test, h))
		goto err;
	if ((mont = BN_MONT_CTX_create(p, ctx)) == NULL)
		goto err;

	for (;;) {
		/* g=test^r0%p */
		if (!BN_mod_exp_mont_ct(g, test, r0, p, ctx, mont))
			goto err;
		if (!BN_is_one(g))
			break;
		if (!BN_add(test, test, BN_value_one()))
			goto err;
		h++;
	}

	if (!BN_GENCB_call(cb, 3, 1))
		goto err;

	ok = 1;
err:
	if (ok) {
		BN_free(ret->p);
		BN_free(ret->q);
		BN_free(ret->g);
		ret->p = BN_dup(p);
		ret->q = BN_dup(q);
		ret->g = BN_dup(g);
		if (ret->p == NULL || ret->q == NULL || ret->g == NULL) {
			ok = 0;
			goto err;
		}
		if (counter_ret != NULL)
			*counter_ret = counter;
		if (h_ret != NULL)
			*h_ret = h;
		if (seed_out != NULL)
			memcpy(seed_out, seed, qsize);
	}
	BN_CTX_end(ctx);
	BN_CTX_free(ctx);
	BN_MONT_CTX_free(mont);

	return ok;
}

#endif
