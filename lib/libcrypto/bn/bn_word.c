/* $OpenBSD: bn_word.c,v 1.22 2025/08/30 07:54:27 jsing Exp $ */
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

#include "bn_local.h"

BN_ULONG
BN_mod_word(const BIGNUM *a, BN_ULONG w)
{
#ifndef BN_LLONG
	BN_ULONG ret = 0;
#else
	BN_ULLONG ret = 0;
#endif
	int i;

	if (w == 0)
		return (BN_ULONG) - 1;

#ifndef BN_ULLONG
	/* If |w| is too long and we don't have |BN_ULLONG| then we need to fall back
	* to using |BN_div_word|. */
	if (w > ((BN_ULONG)1 << BN_BITS4)) {
		BIGNUM *tmp = BN_dup(a);
		if (tmp == NULL) {
			return (BN_ULONG)-1;
		}
		ret = BN_div_word(tmp, w);
		BN_free(tmp);
		return ret;
	}
#endif

	w &= BN_MASK2;
	for (i = a->top - 1; i >= 0; i--) {
#ifndef BN_LLONG
		ret = ((ret << BN_BITS4) | ((a->d[i] >> BN_BITS4) &
		    BN_MASK2l)) % w;
		ret = ((ret << BN_BITS4) | (a->d[i] & BN_MASK2l)) % w;
#else
		ret = (BN_ULLONG)(((ret << (BN_ULLONG)BN_BITS2) |
		    a->d[i]) % (BN_ULLONG)w);
#endif
	}
	return ((BN_ULONG)ret);
}
LCRYPTO_ALIAS(BN_mod_word);

BN_ULONG
BN_div_word(BIGNUM *a, BN_ULONG w)
{
	BN_ULONG ret = 0;
	int i, j;

	w &= BN_MASK2;

	if (!w)
		/* actually this an error (division by zero) */
		return (BN_ULONG) - 1;
	if (a->top == 0)
		return 0;

	/* normalize input (so bn_div_words doesn't complain) */
	j = BN_BITS2 - BN_num_bits_word(w);
	w <<= j;
	if (!BN_lshift(a, a, j))
		return (BN_ULONG) - 1;

	for (i = a->top - 1; i >= 0; i--) {
		BN_ULONG l, d;

		l = a->d[i];
		bn_div_rem_words(ret, l, w, &d, &ret);
		a->d[i] = d;
	}
	if ((a->top > 0) && (a->d[a->top - 1] == 0))
		a->top--;
	ret >>= j;

	/* Set negative again, to handle -0 case. */
	BN_set_negative(a, a->neg);

	return (ret);
}
LCRYPTO_ALIAS(BN_div_word);

int
BN_add_word(BIGNUM *a, BN_ULONG w)
{
	BN_ULONG l;
	int i;

	w &= BN_MASK2;

	/* degenerate case: w is zero */
	if (!w)
		return 1;
	/* degenerate case: a is zero */
	if (BN_is_zero(a))
		return BN_set_word(a, w);
	/* handle 'a' when negative */
	if (a->neg) {
		a->neg = 0;
		i = BN_sub_word(a, w);
		BN_set_negative(a, !a->neg);
		return (i);
	}
	for (i = 0; w != 0 && i < a->top; i++) {
		a->d[i] = l = (a->d[i] + w) & BN_MASK2;
		w = (w > l) ? 1 : 0;
	}
	if (w && i == a->top) {
		if (!bn_wexpand(a, a->top + 1))
			return 0;
		a->top++;
		a->d[i] = w;
	}
	return (1);
}
LCRYPTO_ALIAS(BN_add_word);

int
BN_sub_word(BIGNUM *a, BN_ULONG w)
{
	int i;

	w &= BN_MASK2;

	/* degenerate case: w is zero */
	if (!w)
		return 1;
	/* degenerate case: a is zero */
	if (BN_is_zero(a)) {
		i = BN_set_word(a, w);
		if (i != 0)
			BN_set_negative(a, 1);
		return i;
	}
	/* handle 'a' when negative */
	if (a->neg) {
		a->neg = 0;
		i = BN_add_word(a, w);
		BN_set_negative(a, !a->neg);
		return (i);
	}

	if ((a->top == 1) && (a->d[0] < w)) {
		a->d[0] = w - a->d[0];
		BN_set_negative(a, 1);
		return (1);
	}
	i = 0;
	for (;;) {
		if (a->d[i] >= w) {
			a->d[i] -= w;
			break;
		} else {
			a->d[i] = (a->d[i] - w) & BN_MASK2;
			i++;
			w = 1;
		}
	}
	if ((a->d[i] == 0) && (i == (a->top - 1)))
		a->top--;
	return (1);
}
LCRYPTO_ALIAS(BN_sub_word);

int
BN_mul_word(BIGNUM *a, BN_ULONG w)
{
	BN_ULONG ll;

	w &= BN_MASK2;
	if (a->top) {
		if (w == 0)
			BN_zero(a);
		else {
			ll = bn_mulw_words(a->d, a->d, a->top, w);
			if (ll) {
				if (!bn_wexpand(a, a->top + 1))
					return (0);
				a->d[a->top++] = ll;
			}
		}
	}
	return (1);
}
LCRYPTO_ALIAS(BN_mul_word);
