/* $OpenBSD: bn_gcd.c,v 1.31 2025/06/02 12:40:10 tb Exp $ */
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

#include "bn_local.h"
#include "err_local.h"

static BIGNUM *
euclid(BIGNUM *a, BIGNUM *b)
{
	BIGNUM *t;
	int shifts = 0;

	/* Loop invariant: 0 <= b <= a. */
	while (!BN_is_zero(b)) {
		if (BN_is_odd(a) && BN_is_odd(b)) {
			if (!BN_sub(a, a, b))
				goto err;
			if (!BN_rshift1(a, a))
				goto err;
		} else if (BN_is_odd(a) && !BN_is_odd(b)) {
			if (!BN_rshift1(b, b))
				goto err;
		} else if (!BN_is_odd(a) && BN_is_odd(b)) {
			if (!BN_rshift1(a, a))
				goto err;
		} else {
			if (!BN_rshift1(a, a))
				goto err;
			if (!BN_rshift1(b, b))
				goto err;
			shifts++;
			continue;
		}

		if (BN_cmp(a, b) < 0) {
			t = a;
			a = b;
			b = t;
		}
	}

	if (shifts) {
		if (!BN_lshift(a, a, shifts))
			goto err;
	}

	return a;

 err:
	return NULL;
}

int
BN_gcd(BIGNUM *r, const BIGNUM *in_a, const BIGNUM *in_b, BN_CTX *ctx)
{
	BIGNUM *a, *b, *t;
	int ret = 0;

	BN_CTX_start(ctx);
	if ((a = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((b = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!bn_copy(a, in_a))
		goto err;
	if (!bn_copy(b, in_b))
		goto err;
	a->neg = 0;
	b->neg = 0;

	if (BN_cmp(a, b) < 0) {
		t = a;
		a = b;
		b = t;
	}
	t = euclid(a, b);
	if (t == NULL)
		goto err;

	if (!bn_copy(r, t))
		goto err;
	ret = 1;

 err:
	BN_CTX_end(ctx);
	return (ret);
}
LCRYPTO_ALIAS(BN_gcd);

/*
 * BN_gcd_no_branch is a special version of BN_mod_inverse_no_branch.
 * that returns the GCD.
 */
static BIGNUM *
BN_gcd_no_branch(BIGNUM *in, const BIGNUM *a, const BIGNUM *n,
    BN_CTX *ctx)
{
	BIGNUM *A, *B, *X, *Y, *M, *D, *T, *R = NULL;
	BIGNUM local_A, local_B;
	BIGNUM *pA, *pB;
	BIGNUM *ret = NULL;
	int sign;

	if (in == NULL)
		goto err;
	R = in;

	BN_init(&local_A);
	BN_init(&local_B);

	BN_CTX_start(ctx);
	if ((A = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((B = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((X = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((D = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((M = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Y = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((T = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (!BN_one(X))
		goto err;
	BN_zero(Y);
	if (!bn_copy(B, a))
		goto err;
	if (!bn_copy(A, n))
		goto err;
	A->neg = 0;

	if (B->neg || (BN_ucmp(B, A) >= 0)) {
		/*
		 * Turn BN_FLG_CONSTTIME flag on, so that when BN_div is invoked,
		 * BN_div_no_branch will be called eventually.
		 */
		pB = &local_B;
		/* BN_init() done at the top of the function. */
		BN_with_flags(pB, B, BN_FLG_CONSTTIME);
		if (!BN_nnmod(B, pB, A, ctx))
			goto err;
	}
	sign = -1;
	/* From  B = a mod |n|,  A = |n|  it follows that
	 *
	 *      0 <= B < A,
	 *     -sign*X*a  ==  B   (mod |n|),
	 *      sign*Y*a  ==  A   (mod |n|).
	 */

	while (!BN_is_zero(B)) {
		BIGNUM *tmp;

		/*
		 *      0 < B < A,
		 * (*) -sign*X*a  ==  B   (mod |n|),
		 *      sign*Y*a  ==  A   (mod |n|)
		 */

		/*
		 * Turn BN_FLG_CONSTTIME flag on, so that when BN_div is invoked,
		 * BN_div_no_branch will be called eventually.
		 */
		pA = &local_A;
		/* BN_init() done at the top of the function. */
		BN_with_flags(pA, A, BN_FLG_CONSTTIME);

		/* (D, M) := (A/B, A%B) ... */
		if (!BN_div_ct(D, M, pA, B, ctx))
			goto err;

		/* Now
		 *      A = D*B + M;
		 * thus we have
		 * (**)  sign*Y*a  ==  D*B + M   (mod |n|).
		 */
		tmp = A; /* keep the BIGNUM object, the value does not matter */

		/* (A, B) := (B, A mod B) ... */
		A = B;
		B = M;
		/* ... so we have  0 <= B < A  again */

		/* Since the former  M  is now  B  and the former  B  is now  A,
		 * (**) translates into
		 *       sign*Y*a  ==  D*A + B    (mod |n|),
		 * i.e.
		 *       sign*Y*a - D*A  ==  B    (mod |n|).
		 * Similarly, (*) translates into
		 *      -sign*X*a  ==  A          (mod |n|).
		 *
		 * Thus,
		 *   sign*Y*a + D*sign*X*a  ==  B  (mod |n|),
		 * i.e.
		 *        sign*(Y + D*X)*a  ==  B  (mod |n|).
		 *
		 * So if we set  (X, Y, sign) := (Y + D*X, X, -sign),  we arrive back at
		 *      -sign*X*a  ==  B   (mod |n|),
		 *       sign*Y*a  ==  A   (mod |n|).
		 * Note that  X  and  Y  stay non-negative all the time.
		 */

		if (!BN_mul(tmp, D, X, ctx))
			goto err;
		if (!BN_add(tmp, tmp, Y))
			goto err;

		M = Y; /* keep the BIGNUM object, the value does not matter */
		Y = X;
		X = tmp;
		sign = -sign;
	}

	/*
	 * The while loop (Euclid's algorithm) ends when
	 *      A == gcd(a,n);
	 */

	if (!bn_copy(R, A))
		goto err;
	ret = R;
 err:
	if ((ret == NULL) && (in == NULL))
		BN_free(R);
	BN_CTX_end(ctx);
	return (ret);
}

int
BN_gcd_ct(BIGNUM *r, const BIGNUM *in_a, const BIGNUM *in_b, BN_CTX *ctx)
{
	if (BN_gcd_no_branch(r, in_a, in_b, ctx) == NULL)
		return 0;
	return 1;
}

/* BN_mod_inverse_no_branch is a special version of BN_mod_inverse.
 * It does not contain branches that may leak sensitive information.
 */
static BIGNUM *
BN_mod_inverse_no_branch(BIGNUM *in, const BIGNUM *a, const BIGNUM *n,
    BN_CTX *ctx)
{
	BIGNUM *A, *B, *X, *Y, *M, *D, *T, *R = NULL;
	BIGNUM local_A, local_B;
	BIGNUM *pA, *pB;
	BIGNUM *ret = NULL;
	int sign;

	BN_init(&local_A);
	BN_init(&local_B);

	BN_CTX_start(ctx);
	if ((A = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((B = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((X = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((D = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((M = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Y = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((T = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (in == NULL)
		R = BN_new();
	else
		R = in;
	if (R == NULL)
		goto err;

	if (!BN_one(X))
		goto err;
	BN_zero(Y);
	if (!bn_copy(B, a))
		goto err;
	if (!bn_copy(A, n))
		goto err;
	A->neg = 0;

	if (B->neg || (BN_ucmp(B, A) >= 0)) {
		/*
		 * Turn BN_FLG_CONSTTIME flag on, so that when BN_div is invoked,
		 * BN_div_no_branch will be called eventually.
		 */
		pB = &local_B;
		/* BN_init() done at the top of the function. */
		BN_with_flags(pB, B, BN_FLG_CONSTTIME);
		if (!BN_nnmod(B, pB, A, ctx))
			goto err;
	}
	sign = -1;
	/* From  B = a mod |n|,  A = |n|  it follows that
	 *
	 *      0 <= B < A,
	 *     -sign*X*a  ==  B   (mod |n|),
	 *      sign*Y*a  ==  A   (mod |n|).
	 */

	while (!BN_is_zero(B)) {
		BIGNUM *tmp;

		/*
		 *      0 < B < A,
		 * (*) -sign*X*a  ==  B   (mod |n|),
		 *      sign*Y*a  ==  A   (mod |n|)
		 */

		/*
		 * Turn BN_FLG_CONSTTIME flag on, so that when BN_div is invoked,
		 * BN_div_no_branch will be called eventually.
		 */
		pA = &local_A;
		/* BN_init() done at the top of the function. */
		BN_with_flags(pA, A, BN_FLG_CONSTTIME);

		/* (D, M) := (A/B, A%B) ... */
		if (!BN_div_ct(D, M, pA, B, ctx))
			goto err;

		/* Now
		 *      A = D*B + M;
		 * thus we have
		 * (**)  sign*Y*a  ==  D*B + M   (mod |n|).
		 */
		tmp = A; /* keep the BIGNUM object, the value does not matter */

		/* (A, B) := (B, A mod B) ... */
		A = B;
		B = M;
		/* ... so we have  0 <= B < A  again */

		/* Since the former  M  is now  B  and the former  B  is now  A,
		 * (**) translates into
		 *       sign*Y*a  ==  D*A + B    (mod |n|),
		 * i.e.
		 *       sign*Y*a - D*A  ==  B    (mod |n|).
		 * Similarly, (*) translates into
		 *      -sign*X*a  ==  A          (mod |n|).
		 *
		 * Thus,
		 *   sign*Y*a + D*sign*X*a  ==  B  (mod |n|),
		 * i.e.
		 *        sign*(Y + D*X)*a  ==  B  (mod |n|).
		 *
		 * So if we set  (X, Y, sign) := (Y + D*X, X, -sign),  we arrive back at
		 *      -sign*X*a  ==  B   (mod |n|),
		 *       sign*Y*a  ==  A   (mod |n|).
		 * Note that  X  and  Y  stay non-negative all the time.
		 */

		if (!BN_mul(tmp, D, X, ctx))
			goto err;
		if (!BN_add(tmp, tmp, Y))
			goto err;

		M = Y; /* keep the BIGNUM object, the value does not matter */
		Y = X;
		X = tmp;
		sign = -sign;
	}

	/*
	 * The while loop (Euclid's algorithm) ends when
	 *      A == gcd(a,n);
	 * we have
	 *       sign*Y*a  ==  A  (mod |n|),
	 * where  Y  is non-negative.
	 */

	if (sign < 0) {
		if (!BN_sub(Y, n, Y))
			goto err;
	}
	/* Now  Y*a  ==  A  (mod |n|).  */

	if (!BN_is_one(A)) {
		BNerror(BN_R_NO_INVERSE);
		goto err;
	}

	if (!BN_nnmod(Y, Y, n, ctx))
		goto err;
	if (!bn_copy(R, Y))
		goto err;

	ret = R;

 err:
	if ((ret == NULL) && (in == NULL))
		BN_free(R);
	BN_CTX_end(ctx);
	return (ret);
}

/* solves ax == 1 (mod n) */
static BIGNUM *
BN_mod_inverse_internal(BIGNUM *in, const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx,
    int ct)
{
	BIGNUM *A, *B, *X, *Y, *M, *D, *T, *R = NULL;
	BIGNUM *ret = NULL;
	int sign;

	if (ct)
		return BN_mod_inverse_no_branch(in, a, n, ctx);

	BN_CTX_start(ctx);
	if ((A = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((B = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((X = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((D = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((M = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((Y = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((T = BN_CTX_get(ctx)) == NULL)
		goto err;

	if (in == NULL)
		R = BN_new();
	else
		R = in;
	if (R == NULL)
		goto err;

	if (!BN_one(X))
		goto err;
	BN_zero(Y);
	if (!bn_copy(B, a))
		goto err;
	if (!bn_copy(A, n))
		goto err;
	A->neg = 0;
	if (B->neg || (BN_ucmp(B, A) >= 0)) {
		if (!BN_nnmod(B, B, A, ctx))
			goto err;
	}
	sign = -1;
	/* From  B = a mod |n|,  A = |n|  it follows that
	 *
	 *      0 <= B < A,
	 *     -sign*X*a  ==  B   (mod |n|),
	 *      sign*Y*a  ==  A   (mod |n|).
	 */

	if (BN_is_odd(n) && (BN_num_bits(n) <= (BN_BITS <= 32 ? 450 : 2048))) {
		/* Binary inversion algorithm; requires odd modulus.
		 * This is faster than the general algorithm if the modulus
		 * is sufficiently small (about 400 .. 500 bits on 32-bit
		 * systems, but much more on 64-bit systems) */
		int shift;

		while (!BN_is_zero(B)) {
			/*
			 *      0 < B < |n|,
			 *      0 < A <= |n|,
			 * (1) -sign*X*a  ==  B   (mod |n|),
			 * (2)  sign*Y*a  ==  A   (mod |n|)
			 */

			/* Now divide  B  by the maximum possible power of two in the integers,
			 * and divide  X  by the same value mod |n|.
			 * When we're done, (1) still holds. */
			shift = 0;
			while (!BN_is_bit_set(B, shift)) /* note that 0 < B */
			{
				shift++;

				if (BN_is_odd(X)) {
					if (!BN_uadd(X, X, n))
						goto err;
				}
				/* now X is even, so we can easily divide it by two */
				if (!BN_rshift1(X, X))
					goto err;
			}
			if (shift > 0) {
				if (!BN_rshift(B, B, shift))
					goto err;
			}

			/* Same for  A  and  Y.  Afterwards, (2) still holds. */
			shift = 0;
			while (!BN_is_bit_set(A, shift)) /* note that 0 < A */
			{
				shift++;

				if (BN_is_odd(Y)) {
					if (!BN_uadd(Y, Y, n))
						goto err;
				}
				/* now Y is even */
				if (!BN_rshift1(Y, Y))
					goto err;
			}
			if (shift > 0) {
				if (!BN_rshift(A, A, shift))
					goto err;
			}

			/* We still have (1) and (2).
			 * Both  A  and  B  are odd.
			 * The following computations ensure that
			 *
			 *     0 <= B < |n|,
			 *      0 < A < |n|,
			 * (1) -sign*X*a  ==  B   (mod |n|),
			 * (2)  sign*Y*a  ==  A   (mod |n|),
			 *
			 * and that either  A  or  B  is even in the next iteration.
			 */
			if (BN_ucmp(B, A) >= 0) {
				/* -sign*(X + Y)*a == B - A  (mod |n|) */
				if (!BN_uadd(X, X, Y))
					goto err;
				/* NB: we could use BN_mod_add_quick(X, X, Y, n), but that
				 * actually makes the algorithm slower */
				if (!BN_usub(B, B, A))
					goto err;
			} else {
				/*  sign*(X + Y)*a == A - B  (mod |n|) */
				if (!BN_uadd(Y, Y, X))
					goto err;
				/* as above, BN_mod_add_quick(Y, Y, X, n) would slow things down */
				if (!BN_usub(A, A, B))
					goto err;
			}
		}
	} else {
		/* general inversion algorithm */

		while (!BN_is_zero(B)) {
			BIGNUM *tmp;

			/*
			 *      0 < B < A,
			 * (*) -sign*X*a  ==  B   (mod |n|),
			 *      sign*Y*a  ==  A   (mod |n|)
			 */

			/* (D, M) := (A/B, A%B) ... */
			if (BN_num_bits(A) == BN_num_bits(B)) {
				if (!BN_one(D))
					goto err;
				if (!BN_sub(M, A, B))
					goto err;
			} else if (BN_num_bits(A) == BN_num_bits(B) + 1) {
				/* A/B is 1, 2, or 3 */
				if (!BN_lshift1(T, B))
					goto err;
				if (BN_ucmp(A, T) < 0) {
					/* A < 2*B, so D=1 */
					if (!BN_one(D))
						goto err;
					if (!BN_sub(M, A, B))
						goto err;
				} else {
					/* A >= 2*B, so D=2 or D=3 */
					if (!BN_sub(M, A, T))
						goto err;
					/* use D (:= 3*B) as temp */
					if (!BN_add(D, T, B))
						goto err;
					if (BN_ucmp(A, D) < 0) {
						/* A < 3*B, so D=2 */
						if (!BN_set_word(D, 2))
							goto err;
						/* M (= A - 2*B) already has the correct value */
					} else {
						/* only D=3 remains */
						if (!BN_set_word(D, 3))
							goto err;
						/* currently  M = A - 2*B,  but we need  M = A - 3*B */
						if (!BN_sub(M, M, B))
							goto err;
					}
				}
			} else {
				if (!BN_div_nonct(D, M, A, B, ctx))
					goto err;
			}

			/* Now
			 *      A = D*B + M;
			 * thus we have
			 * (**)  sign*Y*a  ==  D*B + M   (mod |n|).
			 */
			tmp = A; /* keep the BIGNUM object, the value does not matter */

			/* (A, B) := (B, A mod B) ... */
			A = B;
			B = M;
			/* ... so we have  0 <= B < A  again */

			/* Since the former  M  is now  B  and the former  B  is now  A,
			 * (**) translates into
			 *       sign*Y*a  ==  D*A + B    (mod |n|),
			 * i.e.
			 *       sign*Y*a - D*A  ==  B    (mod |n|).
			 * Similarly, (*) translates into
			 *      -sign*X*a  ==  A          (mod |n|).
			 *
			 * Thus,
			 *   sign*Y*a + D*sign*X*a  ==  B  (mod |n|),
			 * i.e.
			 *        sign*(Y + D*X)*a  ==  B  (mod |n|).
			 *
			 * So if we set  (X, Y, sign) := (Y + D*X, X, -sign),  we arrive back at
			 *      -sign*X*a  ==  B   (mod |n|),
			 *       sign*Y*a  ==  A   (mod |n|).
			 * Note that  X  and  Y  stay non-negative all the time.
			 */

			/* most of the time D is very small, so we can optimize tmp := D*X+Y */
			if (BN_is_one(D)) {
				if (!BN_add(tmp, X, Y))
					goto err;
			} else {
				if (BN_is_word(D, 2)) {
					if (!BN_lshift1(tmp, X))
						goto err;
				} else if (BN_is_word(D, 4)) {
					if (!BN_lshift(tmp, X, 2))
						goto err;
				} else if (D->top == 1) {
					if (!bn_copy(tmp, X))
						goto err;
					if (!BN_mul_word(tmp, D->d[0]))
						goto err;
				} else {
					if (!BN_mul(tmp, D,X, ctx))
						goto err;
				}
				if (!BN_add(tmp, tmp, Y))
					goto err;
			}

			M = Y; /* keep the BIGNUM object, the value does not matter */
			Y = X;
			X = tmp;
			sign = -sign;
		}
	}

	/*
	 * The while loop (Euclid's algorithm) ends when
	 *      A == gcd(a,n);
	 * we have
	 *       sign*Y*a  ==  A  (mod |n|),
	 * where  Y  is non-negative.
	 */

	if (sign < 0) {
		if (!BN_sub(Y, n, Y))
			goto err;
	}
	/* Now  Y*a  ==  A  (mod |n|).  */

	if (!BN_is_one(A)) {
		BNerror(BN_R_NO_INVERSE);
		goto err;
	}

	if (!BN_nnmod(Y, Y, n, ctx))
		goto err;
	if (!bn_copy(R, Y))
		goto err;

	ret = R;

 err:
	if ((ret == NULL) && (in == NULL))
		BN_free(R);
	BN_CTX_end(ctx);
	return (ret);
}

BIGNUM *
BN_mod_inverse(BIGNUM *in, const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx)
{
	int ct = ((BN_get_flags(a, BN_FLG_CONSTTIME) != 0) ||
	    (BN_get_flags(n, BN_FLG_CONSTTIME) != 0));
	return BN_mod_inverse_internal(in, a, n, ctx, ct);
}
LCRYPTO_ALIAS(BN_mod_inverse);

BIGNUM *
BN_mod_inverse_nonct(BIGNUM *in, const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx)
{
	return BN_mod_inverse_internal(in, a, n, ctx, 0);
}

BIGNUM *
BN_mod_inverse_ct(BIGNUM *in, const BIGNUM *a, const BIGNUM *n, BN_CTX *ctx)
{
	return BN_mod_inverse_internal(in, a, n, ctx, 1);
}
