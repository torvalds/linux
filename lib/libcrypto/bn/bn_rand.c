/* $OpenBSD: bn_rand.c,v 1.31 2025/05/10 05:54:38 tb Exp $ */
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bn_local.h"
#include "err_local.h"

static int
bnrand(int pseudorand, BIGNUM *rnd, int bits, int top, int bottom)
{
	unsigned char *buf = NULL;
	int ret = 0, bit, bytes, mask;

	if (rnd == NULL) {
		BNerror(ERR_R_PASSED_NULL_PARAMETER);
		return (0);
	}

	if (bits < 0 || (bits == 1 && top > 0)) {
		BNerror(BN_R_BITS_TOO_SMALL);
		return (0);
	}
	if (bits > INT_MAX - 7) {
		BNerror(BN_R_BIGNUM_TOO_LONG);
		return (0);
	}

	if (bits == 0) {
		BN_zero(rnd);
		return (1);
	}

	bytes = (bits + 7) / 8;
	bit = (bits - 1) % 8;
	mask = 0xff << (bit + 1);

	buf = malloc(bytes);
	if (buf == NULL) {
		BNerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	/* make a random number and set the top and bottom bits */
	arc4random_buf(buf, bytes);

#if 1
	if (pseudorand == 2) {
		/* generate patterns that are more likely to trigger BN
		   library bugs */
		int i;
		unsigned char c;

		for (i = 0; i < bytes; i++) {
			arc4random_buf(&c, 1);
			if (c >= 128 && i > 0)
				buf[i] = buf[i - 1];
			else if (c < 42)
				buf[i] = 0;
			else if (c < 84)
				buf[i] = 255;
		}
	}
#endif

	if (top > 0) {
		if (bit == 0) {
			buf[0] = 1;
			buf[1] |= 0x80;
		} else {
			buf[0] |= (3 << (bit - 1));
		}
	}
	if (top == 0)
		buf[0] |= (1 << bit);
	buf[0] &= ~mask;
	if (bottom) /* set bottom bit if requested */
		buf[bytes - 1] |= 1;
	if (BN_bin2bn(buf, bytes, rnd) == NULL)
		goto err;
	ret = 1;

err:
	freezero(buf, bytes);
	return (ret);
}

int
BN_rand(BIGNUM *rnd, int bits, int top, int bottom)
{
	return bnrand(0, rnd, bits, top, bottom);
}
LCRYPTO_ALIAS(BN_rand);

int
BN_pseudo_rand(BIGNUM *rnd, int bits, int top, int bottom)
{
	return bnrand(1, rnd, bits, top, bottom);
}
LCRYPTO_ALIAS(BN_pseudo_rand);

#if 1
int
BN_bntest_rand(BIGNUM *rnd, int bits, int top, int bottom)
{
	return bnrand(2, rnd, bits, top, bottom);
}
#endif


/* random number r:  0 <= r < range */
static int
bn_rand_range(int pseudo, BIGNUM *r, const BIGNUM *range)
{
	int (*bn_rand)(BIGNUM *, int, int, int) = pseudo ? BN_pseudo_rand : BN_rand;
	int n;
	int count = 100;

	if (range->neg || BN_is_zero(range)) {
		BNerror(BN_R_INVALID_RANGE);
		return 0;
	}

	n = BN_num_bits(range); /* n > 0 */

	/* BN_is_bit_set(range, n - 1) always holds */

	if (n == 1)
		BN_zero(r);
	else if (!BN_is_bit_set(range, n - 2) && !BN_is_bit_set(range, n - 3)) {
		/* range = 100..._2,
		 * so  3*range (= 11..._2)  is exactly one bit longer than  range */
		do {
			if (!bn_rand(r, n + 1, -1, 0))
				return 0;
			/* If  r < 3*range,  use  r := r MOD range
			 * (which is either  r, r - range,  or  r - 2*range).
			 * Otherwise, iterate once more.
			 * Since  3*range = 11..._2, each iteration succeeds with
			 * probability >= .75. */
			if (BN_cmp(r, range) >= 0) {
				if (!BN_sub(r, r, range))
					return 0;
				if (BN_cmp(r, range) >= 0)
					if (!BN_sub(r, r, range))
						return 0;
			}

			if (!--count) {
				BNerror(BN_R_TOO_MANY_ITERATIONS);
				return 0;
			}

		} while (BN_cmp(r, range) >= 0);
	} else {
		do {
			/* range = 11..._2  or  range = 101..._2 */
			if (!bn_rand(r, n, -1, 0))
				return 0;

			if (!--count) {
				BNerror(BN_R_TOO_MANY_ITERATIONS);
				return 0;
			}
		} while (BN_cmp(r, range) >= 0);
	}

	return 1;
}

int
BN_rand_range(BIGNUM *r, const BIGNUM *range)
{
	return bn_rand_range(0, r, range);
}
LCRYPTO_ALIAS(BN_rand_range);

int
bn_rand_in_range(BIGNUM *rnd, const BIGNUM *lower_inc, const BIGNUM *upper_exc)
{
	BIGNUM *len;
	int ret = 0;

	if ((len = BN_new()) == NULL)
		goto err;
	if (!BN_sub(len, upper_exc, lower_inc))
		goto err;
	if (!BN_rand_range(rnd, len))
		goto err;
	if (!BN_add(rnd, rnd, lower_inc))
		goto err;

	ret = 1;

 err:
	BN_free(len);

	return ret;
}

int
bn_rand_interval(BIGNUM *rnd, BN_ULONG lower_word, const BIGNUM *upper_exc)
{
	BIGNUM *lower_inc = NULL;
	int ret = 0;

	if ((lower_inc = BN_new()) == NULL)
		goto err;
	if (!BN_set_word(lower_inc, lower_word))
		goto err;
	if (!bn_rand_in_range(rnd, lower_inc, upper_exc))
		goto err;

	ret = 1;

 err:
	BN_free(lower_inc);

	return ret;
}

int
BN_pseudo_rand_range(BIGNUM *r, const BIGNUM *range)
{
	return bn_rand_range(1, r, range);
}
LCRYPTO_ALIAS(BN_pseudo_rand_range);
