/* $OpenBSD: bn_div.c,v 1.44 2025/09/07 06:28:03 jsing Exp $ */
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
#include <stdio.h>

#include <openssl/opensslconf.h>

#include <openssl/bn.h>

#include "bn_arch.h"
#include "bn_local.h"
#include "bn_internal.h"
#include "err_local.h"

BN_ULONG bn_div_3_words(const BN_ULONG *m, BN_ULONG d1, BN_ULONG d0);

#ifndef HAVE_BN_DIV_WORDS
/* Divide h,l by d and return the result. */
/* I need to test this some more :-( */
BN_ULONG
bn_div_words(BN_ULONG h, BN_ULONG l, BN_ULONG d)
{
	BN_ULONG dh, dl, q,ret = 0, th, tl, t;
	int i, count = 2;

	if (d == 0)
		return (BN_MASK2);

	i = BN_num_bits_word(d);
	assert((i == BN_BITS2) || (h <= (BN_ULONG)1 << i));

	i = BN_BITS2 - i;
	if (h >= d)
		h -= d;

	if (i) {
		d <<= i;
		h = (h << i) | (l >> (BN_BITS2 - i));
		l <<= i;
	}
	dh = (d & BN_MASK2h) >> BN_BITS4;
	dl = (d & BN_MASK2l);
	for (;;) {
		if ((h >> BN_BITS4) == dh)
			q = BN_MASK2l;
		else
			q = h / dh;

		th = q * dh;
		tl = dl * q;
		for (;;) {
			t = h - th;
			if ((t & BN_MASK2h) ||
			    ((tl) <= (
			    (t << BN_BITS4) |
			    ((l & BN_MASK2h) >> BN_BITS4))))
				break;
			q--;
			th -= dh;
			tl -= dl;
		}
		t = (tl >> BN_BITS4);
		tl = (tl << BN_BITS4) & BN_MASK2h;
		th += t;

		if (l < tl)
			th++;
		l -= tl;
		if (h < th) {
			h += d;
			q--;
		}
		h -= th;

		if (--count == 0)
			break;

		ret = q << BN_BITS4;
		h = ((h << BN_BITS4) | (l >> BN_BITS4)) & BN_MASK2;
		l = (l & BN_MASK2l) << BN_BITS4;
	}
	ret |= q;
	return (ret);
}
#endif

/*
 * Divide a double word (h:l) by d, returning the quotient q and the remainder
 * r, such that q * d + r is equal to the numerator.
 */
#ifndef HAVE_BN_DIV_REM_WORDS
#ifndef HAVE_BN_DIV_REM_WORDS_INLINE
static inline void
bn_div_rem_words_inline(BN_ULONG h, BN_ULONG l, BN_ULONG d, BN_ULONG *out_q,
    BN_ULONG *out_r)
{
	BN_ULONG q, r;

	q = bn_div_words(h, l, d);
	r = (l - q * d) & BN_MASK2;

	*out_q = q;
	*out_r = r;
}
#endif

void
bn_div_rem_words(BN_ULONG h, BN_ULONG l, BN_ULONG d, BN_ULONG *out_q,
    BN_ULONG *out_r)
{
	bn_div_rem_words_inline(h, l, d, out_q, out_r);
}
#endif

#ifndef HAVE_BN_DIV_3_WORDS

/*
 * Interface is somewhat quirky, |m| is pointer to most significant limb,
 * and less significant limb is referred at |m[-1]|. This means that caller
 * is responsible for ensuring that |m[-1]| is valid. Second condition that
 * has to be met is that |d0|'s most significant bit has to be set. Or in
 * other words divisor has to be "bit-aligned to the left." The subroutine
 * considers four limbs, two of which are "overlapping," hence the name...
 */
BN_ULONG
bn_div_3_words(const BN_ULONG *m, BN_ULONG d1, BN_ULONG d0)
{
	BN_ULONG n0, n1, q, t2h, t2l;
	BN_ULONG rem = 0;

	n0 = m[0];
	n1 = m[-1];

	if (n0 == d0)
		return BN_MASK2;

	/* n0 < d0 */
	bn_div_rem_words(n0, n1, d0, &q, &rem);

	bn_mulw(d1, q, &t2h, &t2l);

	for (;;) {
		if (t2h < rem || (t2h == rem && t2l <= m[-2]))
			break;
		q--;
		rem += d0;
		if (rem < d0)
			break; /* don't let rem overflow */
		if (t2l < d1)
			t2h--;
		t2l -= d1;
	}

	return q;
}
#endif /* !HAVE_BN_DIV_3_WORDS */

/*
 * BN_div_internal computes quotient := numerator / divisor, rounding towards
 * zero and setting remainder such that quotient * divisor + remainder equals
 * the numerator. Thus:
 *
 *   quotient->neg  == numerator->neg ^ divisor->neg   (unless result is zero)
 *   remainder->neg == numerator->neg           (unless the remainder is zero)
 *
 * If either the quotient or remainder is NULL, the respective value is not
 * returned.
 */
static int
BN_div_internal(BIGNUM *quotient, BIGNUM *remainder, const BIGNUM *numerator,
    const BIGNUM *divisor, BN_CTX *ctx, int ct)
{
	int norm_shift, i, loop, r_neg;
	BIGNUM *tmp, wnum, *snum, *sdiv, *res;
	BN_ULONG *resp, *wnump;
	BN_ULONG d0, d1;
	int num_n, div_n;
	int no_branch = 0;
	int ret = 0;

	BN_CTX_start(ctx);

	/* Invalid zero-padding would have particularly bad consequences. */
	if (numerator->top > 0 && numerator->d[numerator->top - 1] == 0) {
		BNerror(BN_R_NOT_INITIALIZED);
		goto err;
	}

	if (ct)
		no_branch = 1;

	if (BN_is_zero(divisor)) {
		BNerror(BN_R_DIV_BY_ZERO);
		goto err;
	}

	if (!no_branch) {
		if (BN_ucmp(numerator, divisor) < 0) {
			if (remainder != NULL) {
				if (!bn_copy(remainder, numerator))
					goto err;
			}
			if (quotient != NULL)
				BN_zero(quotient);

			goto done;
		}
	}

	if ((tmp = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((snum = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((sdiv = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((res = quotient) == NULL) {
		if ((res = BN_CTX_get(ctx)) == NULL)
			goto err;
	}

	/* First we normalise the numbers. */
	norm_shift = BN_BITS2 - BN_num_bits(divisor) % BN_BITS2;
	if (!BN_lshift(sdiv, divisor, norm_shift))
		goto err;
	sdiv->neg = 0;
	norm_shift += BN_BITS2;
	if (!BN_lshift(snum, numerator, norm_shift))
		goto err;
	snum->neg = 0;

	if (no_branch) {
		/*
		 * Since we don't know whether snum is larger than sdiv, we pad
		 * snum with enough zeroes without changing its value.
		 */
		if (snum->top <= sdiv->top + 1) {
			if (!bn_wexpand(snum, sdiv->top + 2))
				goto err;
			for (i = snum->top; i < sdiv->top + 2; i++)
				snum->d[i] = 0;
			snum->top = sdiv->top + 2;
		} else {
			if (!bn_wexpand(snum, snum->top + 1))
				goto err;
			snum->d[snum->top] = 0;
			snum->top++;
		}
	}

	div_n = sdiv->top;
	num_n = snum->top;
	loop = num_n - div_n;

	/*
	 * Setup a 'window' into snum - this is the part that corresponds to the
	 * current 'area' being divided.
	 */
	wnum.neg = 0;
	wnum.d = &(snum->d[loop]);
	wnum.top = div_n;
	/* only needed when BN_ucmp messes up the values between top and max */
	wnum.dmax  = snum->dmax - loop; /* so we don't step out of bounds */
	wnum.flags = snum->flags | BN_FLG_STATIC_DATA;

	/* Get the top 2 words of sdiv */
	/* div_n=sdiv->top; */
	d0 = sdiv->d[div_n - 1];
	d1 = (div_n == 1) ? 0 : sdiv->d[div_n - 2];

	/* pointer to the 'top' of snum */
	wnump = &(snum->d[num_n - 1]);

	/* Setup to 'res' */
	if (!bn_wexpand(res, (loop + 1)))
		goto err;
	res->top = loop - no_branch;
	r_neg = numerator->neg ^ divisor->neg;
	resp = &(res->d[loop - 1]);

	/* space for temp */
	if (!bn_wexpand(tmp, (div_n + 1)))
		goto err;

	if (!no_branch) {
		if (BN_ucmp(&wnum, sdiv) >= 0) {
			bn_sub_words(wnum.d, wnum.d, sdiv->d, div_n);
			*resp = 1;
		} else
			res->top--;
	}

	/*
	 * If res->top == 0 then clear the neg value otherwise decrease the resp
	 * pointer.
	 */
	if (res->top == 0)
		res->neg = 0;
	else
		resp--;

	for (i = 0; i < loop - 1; i++, wnump--, resp--) {
		BN_ULONG q, l0;

		/*
		 * The first part of the loop uses the top two words of snum and
		 * sdiv to calculate a BN_ULONG q such that:
		 *
		 *  | wnum - sdiv * q | < sdiv
		 */
		q = bn_div_3_words(wnump, d1, d0);
		l0 = bn_mulw_words(tmp->d, sdiv->d, div_n, q);
		tmp->d[div_n] = l0;
		wnum.d--;

		/*
		 * Ignore top values of the bignums just sub the two BN_ULONG
		 * arrays with bn_sub_words.
		 */
		if (bn_sub_words(wnum.d, wnum.d, tmp->d, div_n + 1)) {
			/*
			 * Note: As we have considered only the leading two
			 * BN_ULONGs in the calculation of q, sdiv * q might be
			 * greater than wnum (but then (q-1) * sdiv is less or
			 * equal than wnum).
			 */
			q--;
			if (bn_add_words(wnum.d, wnum.d, sdiv->d, div_n)) {
				/*
				 * We can't have an overflow here (assuming
				 * that q != 0, but if q == 0 then tmp is
				 * zero anyway).
				 */
				(*wnump)++;
			}
		}
		/* store part of the result */
		*resp = q;
	}

	bn_correct_top(snum);

	if (remainder != NULL) {
		/*
		 * Keep a copy of the neg flag in numerator because if
		 * remainder == numerator, BN_rshift() will overwrite it.
		 */
		int neg = numerator->neg;

		BN_rshift(remainder, snum, norm_shift);
		BN_set_negative(remainder, neg);
	}

	if (no_branch)
		bn_correct_top(res);

	BN_set_negative(res, r_neg);

 done:
	ret = 1;
 err:
	BN_CTX_end(ctx);

	return ret;
}

int
BN_div(BIGNUM *quotient, BIGNUM *remainder, const BIGNUM *numerator,
    const BIGNUM *divisor, BN_CTX *ctx)
{
	int ct;

	ct = BN_get_flags(numerator, BN_FLG_CONSTTIME) != 0 ||
	    BN_get_flags(divisor, BN_FLG_CONSTTIME) != 0;

	return BN_div_internal(quotient, remainder, numerator, divisor, ctx, ct);
}
LCRYPTO_ALIAS(BN_div);

int
BN_div_nonct(BIGNUM *quotient, BIGNUM *remainder, const BIGNUM *numerator,
    const BIGNUM *divisor, BN_CTX *ctx)
{
	return BN_div_internal(quotient, remainder, numerator, divisor, ctx, 0);
}

int
BN_div_ct(BIGNUM *quotient, BIGNUM *remainder, const BIGNUM *numerator,
    const BIGNUM *divisor, BN_CTX *ctx)
{
	return BN_div_internal(quotient, remainder, numerator, divisor, ctx, 1);
}
