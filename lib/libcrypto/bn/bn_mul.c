/* $OpenBSD: bn_mul.c,v 1.46 2025/09/01 15:39:59 jsing Exp $ */
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
#include <string.h>

#include <openssl/opensslconf.h>

#include "bn_arch.h"
#include "bn_internal.h"
#include "bn_local.h"

/*
 * bn_mul_comba4() computes r[] = a[] * b[] using Comba multiplication
 * (https://everything2.com/title/Comba+multiplication), where a and b are both
 * four word arrays, producing an eight word array result.
 */
#ifndef HAVE_BN_MUL_COMBA4
void
bn_mul_comba4(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b)
{
	BN_ULONG c0, c1, c2;

	bn_mulw_addtw(a[0], b[0],  0,  0,  0, &c2, &c1, &r[0]);

	bn_mulw_addtw(a[0], b[1],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[0], c2, c1, c0, &c2, &c1, &r[1]);

	bn_mulw_addtw(a[2], b[0],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[0], b[2], c2, c1, c0, &c2, &c1, &r[2]);

	bn_mulw_addtw(a[0], b[3],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[0], c2, c1, c0, &c2, &c1, &r[3]);

	bn_mulw_addtw(a[3], b[1],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[3], c2, c1, c0, &c2, &c1, &r[4]);

	bn_mulw_addtw(a[2], b[3],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[2], c2, c1, c0, &c2, &c1, &r[5]);

	bn_mulw_addtw(a[3], b[3],  0, c2, c1, &c2, &r[7], &r[6]);
}
#endif

/*
 * bn_mul_comba6() computes r[] = a[] * b[] using Comba multiplication
 * (https://everything2.com/title/Comba+multiplication), where a and b are both
 * six word arrays, producing a 12 word array result.
 */
#ifndef HAVE_BN_MUL_COMBA6
void
bn_mul_comba6(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b)
{
	BN_ULONG c0, c1, c2;

	bn_mulw_addtw(a[0], b[0],  0,  0,  0, &c2, &c1, &r[0]);

	bn_mulw_addtw(a[0], b[1],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[0], c2, c1, c0, &c2, &c1, &r[1]);

	bn_mulw_addtw(a[2], b[0],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[0], b[2], c2, c1, c0, &c2, &c1, &r[2]);

	bn_mulw_addtw(a[0], b[3],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[0], c2, c1, c0, &c2, &c1, &r[3]);

	bn_mulw_addtw(a[4], b[0],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[0], b[4], c2, c1, c0, &c2, &c1, &r[4]);

	bn_mulw_addtw(a[0], b[5],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[0], c2, c1, c0, &c2, &c1, &r[5]);

	bn_mulw_addtw(a[5], b[1],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[5], c2, c1, c0, &c2, &c1, &r[6]);

	bn_mulw_addtw(a[2], b[5],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[2], c2, c1, c0, &c2, &c1, &r[7]);

	bn_mulw_addtw(a[5], b[3],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[5], c2, c1, c0, &c2, &c1, &r[8]);

	bn_mulw_addtw(a[4], b[5],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[4], c2, c1, c0, &c2, &c1, &r[9]);

	bn_mulw_addtw(a[5], b[5],  0, c2, c1, &c2, &r[11], &r[10]);
}
#endif

/*
 * bn_mul_comba8() computes r[] = a[] * b[] using Comba multiplication
 * (https://everything2.com/title/Comba+multiplication), where a and b are both
 * eight word arrays, producing a 16 word array result.
 */
#ifndef HAVE_BN_MUL_COMBA8
void
bn_mul_comba8(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b)
{
	BN_ULONG c0, c1, c2;

	bn_mulw_addtw(a[0], b[0],  0,  0,  0, &c2, &c1, &r[0]);

	bn_mulw_addtw(a[0], b[1],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[0], c2, c1, c0, &c2, &c1, &r[1]);

	bn_mulw_addtw(a[2], b[0],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[0], b[2], c2, c1, c0, &c2, &c1, &r[2]);

	bn_mulw_addtw(a[0], b[3],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[0], c2, c1, c0, &c2, &c1, &r[3]);

	bn_mulw_addtw(a[4], b[0],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[0], b[4], c2, c1, c0, &c2, &c1, &r[4]);

	bn_mulw_addtw(a[0], b[5],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[0], c2, c1, c0, &c2, &c1, &r[5]);

	bn_mulw_addtw(a[6], b[0],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[5], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[0], b[6], c2, c1, c0, &c2, &c1, &r[6]);

	bn_mulw_addtw(a[0], b[7],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[6], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[5], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[6], b[1], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[7], b[0], c2, c1, c0, &c2, &c1, &r[7]);

	bn_mulw_addtw(a[7], b[1],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[6], b[2], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[5], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[2], b[6], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[1], b[7], c2, c1, c0, &c2, &c1, &r[8]);

	bn_mulw_addtw(a[2], b[7],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[6], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[5], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[6], b[3], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[7], b[2], c2, c1, c0, &c2, &c1, &r[9]);

	bn_mulw_addtw(a[7], b[3],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[6], b[4], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[5], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[4], b[6], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[3], b[7], c2, c1, c0, &c2, &c1, &r[10]);

	bn_mulw_addtw(a[4], b[7],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[6], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[6], b[5], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[7], b[4], c2, c1, c0, &c2, &c1, &r[11]);

	bn_mulw_addtw(a[7], b[5],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[6], b[6], c2, c1, c0, &c2, &c1, &c0);
	bn_mulw_addtw(a[5], b[7], c2, c1, c0, &c2, &c1, &r[12]);

	bn_mulw_addtw(a[6], b[7],  0, c2, c1, &c2, &c1, &c0);
	bn_mulw_addtw(a[7], b[6], c2, c1, c0, &c2, &c1, &r[13]);

	bn_mulw_addtw(a[7], b[7],  0, c2, c1, &c2, &r[15], &r[14]);
}
#endif

/*
 * bn_mulw_words() computes (carry:r[i]) = a[i] * w + carry, where a is an array
 * of words and w is a single word. This is used as a step in the multiplication
 * of word arrays.
 */
#ifndef HAVE_BN_MULW_WORDS
BN_ULONG
bn_mulw_words(BN_ULONG *r, const BN_ULONG *a, int num, BN_ULONG w)
{
	BN_ULONG carry = 0;

	assert(num >= 0);
	if (num <= 0)
		return 0;

	while (num & ~3) {
		bn_qwmulw_addw(a[3], a[2], a[1], a[0], w, carry, &carry,
		    &r[3], &r[2], &r[1], &r[0]);
		a += 4;
		r += 4;
		num -= 4;
	}
	while (num) {
		bn_mulw_addw(a[0], w, carry, &carry, &r[0]);
		a++;
		r++;
		num--;
	}
	return carry;
}
#endif

/*
 * bn_mulw_add_words() computes (carry:r[i]) = a[i] * w + r[i] + carry, where
 * a is an array of words and w is a single word. This is used as a step in the
 * multiplication of word arrays.
 */
#ifndef HAVE_BN_MULW_ADD_WORDS
BN_ULONG
bn_mulw_add_words(BN_ULONG *r, const BN_ULONG *a, int num, BN_ULONG w)
{
	BN_ULONG carry = 0;

	assert(num >= 0);
	if (num <= 0)
		return 0;

	while (num & ~3) {
		bn_qwmulw_addqw_addw(a[3], a[2], a[1], a[0], w,
		    r[3], r[2], r[1], r[0], carry, &carry,
		    &r[3], &r[2], &r[1], &r[0]);
		a += 4;
		r += 4;
		num -= 4;
	}
	while (num) {
		bn_mulw_addw_addw(a[0], w, r[0], carry, &carry, &r[0]);
		a++;
		r++;
		num--;
	}

	return carry;
}
#endif

#ifndef HAVE_BN_MUL_WORDS
void
bn_mul_words(BN_ULONG *r, const BN_ULONG *a, int a_len, const BN_ULONG *b,
    int b_len)
{
	BN_ULONG *rr;

	if (a_len < b_len) {
		int itmp;
		const BN_ULONG *ltmp;

		itmp = a_len;
		a_len = b_len;
		b_len = itmp;
		ltmp = a;
		a = b;
		b = ltmp;

	}
	rr = &(r[a_len]);
	if (b_len <= 0) {
		(void)bn_mulw_words(r, a, a_len, 0);
		return;
	} else
		rr[0] = bn_mulw_words(r, a, a_len, b[0]);

	for (;;) {
		if (--b_len <= 0)
			return;
		rr[1] = bn_mulw_add_words(&(r[1]), a, a_len, b[1]);
		if (--b_len <= 0)
			return;
		rr[2] = bn_mulw_add_words(&(r[2]), a, a_len, b[2]);
		if (--b_len <= 0)
			return;
		rr[3] = bn_mulw_add_words(&(r[3]), a, a_len, b[3]);
		if (--b_len <= 0)
			return;
		rr[4] = bn_mulw_add_words(&(r[4]), a, a_len, b[4]);
		rr += 4;
		r += 4;
		b += 4;
	}
}
#endif

static int
bn_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, int rn, BN_CTX *ctx)
{
	bn_mul_words(r->d, a->d, a->top, b->d, b->top);

	return 1;
}

int
BN_mul(BIGNUM *r, const BIGNUM *a, const BIGNUM *b, BN_CTX *ctx)
{
	BIGNUM *rr;
	int rn;
	int ret = 0;

	BN_CTX_start(ctx);

	if (BN_is_zero(a) || BN_is_zero(b)) {
		BN_zero(r);
		goto done;
	}

	rr = r;
	if (rr == a || rr == b)
		rr = BN_CTX_get(ctx);
	if (rr == NULL)
		goto err;

	if (a->top > INT_MAX - b->top)
		goto err;
	rn = a->top + b->top;
	if (!bn_wexpand(rr, rn))
		goto err;

	if (a->top == 4 && b->top == 4) {
		bn_mul_comba4(rr->d, a->d, b->d);
	} else if (a->top == 6 && b->top == 6) {
		bn_mul_comba6(rr->d, a->d, b->d);
	} else if (a->top == 8 && b->top == 8) {
		bn_mul_comba8(rr->d, a->d, b->d);
	} else {
		if (!bn_mul(rr, a, b, rn, ctx))
			goto err;
	}

	rr->top = rn;
	bn_correct_top(rr);

	BN_set_negative(rr, a->neg ^ b->neg);

	if (!bn_copy(r, rr))
		goto err;
 done:
	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}
LCRYPTO_ALIAS(BN_mul);
