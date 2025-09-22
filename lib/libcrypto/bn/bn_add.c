/* $OpenBSD: bn_add.c,v 1.29 2025/05/25 04:53:05 jsing Exp $ */
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

#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "bn_arch.h"
#include "bn_local.h"
#include "bn_internal.h"
#include "err_local.h"

/*
 * bn_add() computes (carry:r[i]) = a[i] + b[i] + carry, where a and b are both
 * arrays of words (r may be the same as a or b). The length of a and b may
 * differ, while r must be at least max(a_len, b_len) in length. Any carry
 * resulting from the addition is returned.
 */
#ifndef HAVE_BN_ADD
BN_ULONG
bn_add(BN_ULONG *r, int r_len, const BN_ULONG *a, int a_len, const BN_ULONG *b,
    int b_len)
{
	int min_len, diff_len;
	BN_ULONG carry = 0;

	if ((min_len = a_len) > b_len)
		min_len = b_len;

	diff_len = a_len - b_len;

	carry = bn_add_words(r, a, b, min_len);

	a += min_len;
	b += min_len;
	r += min_len;

	/* XXX - consider doing four at a time to match bn_add_words(). */
	while (diff_len < 0) {
		/* Compute r[0] = 0 + b[0] + carry. */
		bn_addw(b[0], carry, &carry, &r[0]);
		diff_len++;
		b++;
		r++;
	}

	/* XXX - consider doing four at a time to match bn_add_words(). */
	while (diff_len > 0) {
		/* Compute r[0] = a[0] + 0 + carry. */
		bn_addw(a[0], carry, &carry, &r[0]);
		diff_len--;
		a++;
		r++;
	}

	return carry;
}
#endif

/*
 * bn_sub() computes (borrow:r[i]) = a[i] - b[i] - borrow, where a and b are both
 * arrays of words (r may be the same as a or b). The length of a and b may
 * differ, while r must be at least max(a_len, b_len) in length. Any borrow
 * resulting from the subtraction is returned.
 */
#ifndef HAVE_BN_SUB
BN_ULONG
bn_sub(BN_ULONG *r, int r_len, const BN_ULONG *a, int a_len, const BN_ULONG *b,
    int b_len)
{
	int min_len, diff_len;
	BN_ULONG borrow = 0;

	if ((min_len = a_len) > b_len)
		min_len = b_len;

	diff_len = a_len - b_len;

	borrow = bn_sub_words(r, a, b, min_len);

	a += min_len;
	b += min_len;
	r += min_len;

	/* XXX - consider doing four at a time to match bn_sub_words. */
	while (diff_len < 0) {
		/* Compute r[0] = 0 - b[0] - borrow. */
		bn_subw_subw(0, b[0], borrow, &borrow, &r[0]);
		diff_len++;
		b++;
		r++;
	}

	/* XXX - consider doing four at a time to match bn_sub_words. */
	while (diff_len > 0) {
		/* Compute r[0] = a[0] - 0 - borrow. */
		bn_subw_subw(a[0], 0, borrow, &borrow, &r[0]);
		diff_len--;
		a++;
		r++;
	}

	return borrow;
}
#endif

int
BN_uadd(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
	BN_ULONG carry;
	int rn;

	if ((rn = a->top) < b->top)
		rn = b->top;
	if (rn == INT_MAX)
		return 0;
	if (!bn_wexpand(r, rn + 1))
		return 0;

	carry = bn_add(r->d, rn, a->d, a->top, b->d, b->top);
	r->d[rn] = carry;

	r->top = rn + (carry & 1);
	r->neg = 0;

	return 1;
}
LCRYPTO_ALIAS(BN_uadd);

int
BN_usub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
	BN_ULONG borrow;
	int rn;

	if (a->top < b->top) {
		BNerror(BN_R_ARG2_LT_ARG3);
		return 0;
	}
	rn = a->top;

	if (!bn_wexpand(r, rn))
		return 0;

	borrow = bn_sub(r->d, rn, a->d, a->top, b->d, b->top);
	if (borrow > 0) {
		BNerror(BN_R_ARG2_LT_ARG3);
		return 0;
	}

	r->top = rn;
	r->neg = 0;

	bn_correct_top(r);

	return 1;
}
LCRYPTO_ALIAS(BN_usub);

int
BN_add(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
	int ret, r_neg;

	if (a->neg == b->neg) {
		r_neg = a->neg;
		ret = BN_uadd(r, a, b);
	} else {
		int cmp = BN_ucmp(a, b);

		if (cmp > 0) {
			r_neg = a->neg;
			ret = BN_usub(r, a, b);
		} else if (cmp < 0) {
			r_neg = b->neg;
			ret = BN_usub(r, b, a);
		} else {
			r_neg = 0;
			BN_zero(r);
			ret = 1;
		}
	}

	BN_set_negative(r, r_neg);

	return ret;
}
LCRYPTO_ALIAS(BN_add);

int
BN_sub(BIGNUM *r, const BIGNUM *a, const BIGNUM *b)
{
	int ret, r_neg;

	if (a->neg != b->neg) {
		r_neg = a->neg;
		ret = BN_uadd(r, a, b);
	} else {
		int cmp = BN_ucmp(a, b);

		if (cmp > 0) {
			r_neg = a->neg;
			ret = BN_usub(r, a, b);
		} else if (cmp < 0) {
			r_neg = !b->neg;
			ret = BN_usub(r, b, a);
		} else {
			r_neg = 0;
			BN_zero(r);
			ret = 1;
		}
	}

	BN_set_negative(r, r_neg);

	return ret;
}
LCRYPTO_ALIAS(BN_sub);
