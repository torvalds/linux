/* $OpenBSD: bn_mod.c,v 1.23 2025/05/10 05:54:38 tb Exp $ */
/* Includes code written by Lenka Fibikova <fibikova@exp-math.uni-essen.de>
 * for the OpenSSL project. */
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

#include "bn_local.h"
#include "err_local.h"

int
BN_mod_ct(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx)
{
	return BN_div_ct(NULL, r, a, m, ctx);
}

int
BN_mod_nonct(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx)
{
	return BN_div_nonct(NULL, r, a, m, ctx);
}

/*
 * BN_nnmod() is like BN_mod(), but always returns a non-negative remainder
 * (that is 0 <= r < |m| always holds). If both a and m have the same sign then
 * the result is already non-negative. Otherwise, -|m| < r < 0, which needs to
 * be adjusted as r := r + |m|. This equates to r := |m| - |r|.
 */
int
BN_nnmod(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx)
{
	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}
	if (!BN_mod_ct(r, a, m, ctx))
		return 0;
	if (BN_is_negative(r))
		return BN_usub(r, m, r);
	return 1;
}
LCRYPTO_ALIAS(BN_nnmod);

int
BN_mod_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m,
    BN_CTX *ctx)
{
	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}
	if (!BN_add(r, a, b))
		return 0;
	return BN_nnmod(r, r, m, ctx);
}
LCRYPTO_ALIAS(BN_mod_add);

/*
 * BN_mod_add() variant that may only be used if both a and b are non-negative
 * and have already been reduced (less than m).
 */
int
BN_mod_add_quick(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m)
{
	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}
	if (!BN_uadd(r, a, b))
		return 0;
	if (BN_ucmp(r, m) >= 0)
		return BN_usub(r, r, m);
	return 1;
}
LCRYPTO_ALIAS(BN_mod_add_quick);

int
BN_mod_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m,
    BN_CTX *ctx)
{
	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}
	if (!BN_sub(r, a, b))
		return 0;
	return BN_nnmod(r, r, m, ctx);
}
LCRYPTO_ALIAS(BN_mod_sub);

/*
 * BN_mod_sub() variant that may only be used if both a and b are non-negative
 * and have already been reduced (less than m).
 */
int
BN_mod_sub_quick(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m)
{
	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}
	if (BN_ucmp(a, b) >= 0)
		return BN_usub(r, a, b);
	if (!BN_usub(r, b, a))
		return 0;
	return BN_usub(r, m, r);
}
LCRYPTO_ALIAS(BN_mod_sub_quick);

int
BN_mod_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, const BIGNUM *m,
    BN_CTX *ctx)
{
	BIGNUM *rr;
	int ret = 0;

	BN_CTX_start(ctx);

	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		goto err;
	}

	rr = r;
	if (rr == a || rr == b)
		rr = BN_CTX_get(ctx);
	if (rr == NULL)
		goto err;

	if (a == b) {
		if (!BN_sqr(rr, a, ctx))
			goto err;
	} else {
		if (!BN_mul(rr, a, b, ctx))
			goto err;
	}
	if (!BN_nnmod(r, rr, m, ctx))
		goto err;

	ret = 1;

 err:
	BN_CTX_end(ctx);

	return ret;
}
LCRYPTO_ALIAS(BN_mod_mul);

int
BN_mod_sqr(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx)
{
	return BN_mod_mul(r, a, a, m, ctx);
}
LCRYPTO_ALIAS(BN_mod_sqr);

int
BN_mod_lshift1(BIGNUM *r, const BIGNUM *a, const BIGNUM *m, BN_CTX *ctx)
{
	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}
	if (!BN_lshift1(r, a))
		return 0;
	return BN_nnmod(r, r, m, ctx);
}
LCRYPTO_ALIAS(BN_mod_lshift1);

/*
 * BN_mod_lshift1() variant that may be used if a is non-negative
 * and has already been reduced (less than m).
 */
int
BN_mod_lshift1_quick(BIGNUM *r, const BIGNUM *a, const BIGNUM *m)
{
	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}
	if (!BN_lshift1(r, a))
		return 0;
	if (BN_ucmp(r, m) >= 0)
		return BN_usub(r, r, m);
	return 1;
}
LCRYPTO_ALIAS(BN_mod_lshift1_quick);

int
BN_mod_lshift(BIGNUM *r, const BIGNUM *a, int n, const BIGNUM *m, BN_CTX *ctx)
{
	BIGNUM *abs_m;
	int ret = 0;

	BN_CTX_start(ctx);

	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		goto err;
	}

	if (!BN_nnmod(r, a, m, ctx))
		goto err;

	if (BN_is_negative(m)) {
		if ((abs_m = BN_CTX_get(ctx)) == NULL)
			goto err;
		if (!bn_copy(abs_m, m))
			goto err;
		BN_set_negative(abs_m, 0);
		m = abs_m;
	}
	if (!BN_mod_lshift_quick(r, r, n, m))
		goto err;

	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}
LCRYPTO_ALIAS(BN_mod_lshift);

/*
 * BN_mod_lshift() variant that may be used if a is non-negative
 * and has already been reduced (less than m).
 */
int
BN_mod_lshift_quick(BIGNUM *r, const BIGNUM *a, int n, const BIGNUM *m)
{
	int max_shift;

	if (r == m) {
		BNerror(BN_R_INVALID_ARGUMENT);
		return 0;
	}

	if (!bn_copy(r, a))
		return 0;

	while (n > 0) {
		if ((max_shift = BN_num_bits(m) - BN_num_bits(r)) < 0) {
			BNerror(BN_R_INPUT_NOT_REDUCED);
			return 0;
		}
		if (max_shift == 0)
			max_shift = 1;
		if (max_shift > n)
			max_shift = n;

		if (!BN_lshift(r, r, max_shift))
			return 0;
		n -= max_shift;

		if (BN_ucmp(r, m) >= 0) {
			if (!BN_usub(r, r, m))
				return 0;
		}
	}

	return 1;
}
LCRYPTO_ALIAS(BN_mod_lshift_quick);
