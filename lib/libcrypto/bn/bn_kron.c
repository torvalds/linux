/* $OpenBSD: bn_kron.c,v 1.15 2023/07/08 12:21:58 beck Exp $ */
/* ====================================================================
 * Copyright (c) 1998-2000 The OpenSSL Project.  All rights reserved.
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

#include "bn_local.h"

/*
 * Kronecker symbol, implemented according to Henri Cohen, "A Course in
 * Computational Algebraic Number Theory", Algorithm 1.4.10.
 *
 * Returns -1, 0, or 1 on success and -2 on error.
 */

int
BN_kronecker(const BIGNUM *A, const BIGNUM *B, BN_CTX *ctx)
{
	/* tab[BN_lsw(n) & 7] = (-1)^((n^2 - 1)) / 8) for odd values of n. */
	static const int tab[8] = {0, 1, 0, -1, 0, -1, 0, 1};
	BIGNUM *a, *b, *tmp;
	int k, v;
	int ret = -2;

	BN_CTX_start(ctx);

	if ((a = BN_CTX_get(ctx)) == NULL)
		goto end;
	if ((b = BN_CTX_get(ctx)) == NULL)
		goto end;

	if (!bn_copy(a, A))
		goto end;
	if (!bn_copy(b, B))
		goto end;

	/*
	 * Cohen's step 1:
	 */

	/* If b is zero, output 1 if |a| is 1, otherwise output 0. */
	if (BN_is_zero(b)) {
		ret = BN_abs_is_word(a, 1);
		goto end;
	}

	/*
	 * Cohen's step 2:
	 */

	/* If both are even, they have a factor in common, so output 0. */
	if (!BN_is_odd(a) && !BN_is_odd(b)) {
		ret = 0;
		goto end;
	}

	/* Factorize b = 2^v * u with odd u and replace b with u. */
	v = 0;
	while (!BN_is_bit_set(b, v))
		v++;
	if (!BN_rshift(b, b, v))
		goto end;

	/* If v is even set k = 1, otherwise set it to (-1)^((a^2 - 1) / 8). */
	k = 1;
	if (v % 2 != 0)
		k = tab[BN_lsw(a) & 7];

	/*
	 * If b is negative, replace it with -b and if a is also negative
	 * replace k with -k.
	 */
	if (BN_is_negative(b)) {
		BN_set_negative(b, 0);

		if (BN_is_negative(a))
			k = -k;
	}

	/*
	 * Now b is positive and odd, so compute the Jacobi symbol (a/b)
	 * and multiply it by k.
	 */

	while (1) {
		/*
		 * Cohen's step 3:
		 */

		/* b is positive and odd. */

		/* If a is zero output k if b is one, otherwise output 0. */
		if (BN_is_zero(a)) {
			ret = BN_is_one(b) ? k : 0;
			goto end;
		}

		/* Factorize a = 2^v * u with odd u and replace a with u. */
		v = 0;
		while (!BN_is_bit_set(a, v))
			v++;
		if (!BN_rshift(a, a, v))
			goto end;

		/* If v is odd, multiply k with (-1)^((b^2 - 1) / 8). */
		if (v % 2 != 0)
			k *= tab[BN_lsw(b) & 7];

		/*
		 * Cohen's step 4:
		 */

		/*
		 * Apply the reciprocity law: multiply k by (-1)^((a-1)(b-1)/4).
		 *
		 * This expression is -1 if and only if a and b are 3 (mod 4).
		 * In turn, this is the case if and only if their two's
		 * complement representations have the second bit set.
		 * a could be negative in the first iteration, b is positive.
		 */
		if ((BN_is_negative(a) ? ~BN_lsw(a) : BN_lsw(a)) & BN_lsw(b) & 2)
			k = -k;

		/*
		 * (a, b) := (b mod |a|, |a|)
		 *
		 * Once this is done, we know that 0 < a < b at the start of the
		 * loop. Since b is strictly decreasing, the loop terminates.
		 */

		if (!BN_nnmod(b, b, a, ctx))
			goto end;

		tmp = a;
		a = b;
		b = tmp;

		BN_set_negative(b, 0);
	}

 end:
	BN_CTX_end(ctx);

	return ret;
}
LCRYPTO_ALIAS(BN_kronecker);
