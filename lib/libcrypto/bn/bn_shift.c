/* $OpenBSD: bn_shift.c,v 1.23 2025/05/10 05:54:38 tb Exp $ */
/*
 * Copyright (c) 2022, 2023 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <openssl/bn.h>

#include "bn_local.h"
#include "err_local.h"

static inline int
bn_lshift(BIGNUM *r, const BIGNUM *a, int n)
{
	size_t count, shift_bits, shift_words;
	size_t lshift, rshift;
	ssize_t rstride;
	BN_ULONG *dst, *src;

	if (n < 0) {
		BNerror(BN_R_INVALID_LENGTH);
		return 0;
	}
	shift_bits = n;

	/*
	 * Left bit shift, potentially across word boundaries.
	 *
	 * When shift is not an exact multiple of BN_BITS2, the bottom bits of
	 * the previous word need to be right shifted and combined with the left
	 * shifted bits using bitwise OR. If shift is an exact multiple of
	 * BN_BITS2, the source for the left and right shifts are the same
	 * and the shifts become zero bits (which is effectively a memmove).
	 */
	shift_words = shift_bits / BN_BITS2;
	lshift = shift_bits % BN_BITS2;
	rshift = (BN_BITS2 - lshift) % BN_BITS2;
	rstride = 0 - (lshift + rshift) / BN_BITS2;

	if (a->top < 1) {
		BN_zero(r);
		return 1;
	}

	count = a->top + shift_words + 1;

	if (count < shift_words)
		return 0;

	if (!bn_wexpand(r, count))
		return 0;

	src = a->d + a->top - 1;
	dst = r->d + a->top + shift_words;

	/* Handle right shift for top most word. */
	*dst = (*src >> rshift) & rstride;
	dst--;

	/* Handle left shift and right shift for remaining words. */
	while (src > a->d) {
		*dst = *src << lshift | src[rstride] >> rshift;
		src--;
		dst--;
	}
	*dst = *src << lshift;

	/* Zero any additional words resulting from the left shift. */
	while (dst > r->d) {
		dst--;
		*dst = 0;
	}

	r->top = count;
	bn_correct_top(r);

	BN_set_negative(r, a->neg);

	return 1;
}

static inline int
bn_rshift(BIGNUM *r, const BIGNUM *a, int n)
{
	size_t count, shift_bits, shift_words;
	size_t lshift, rshift;
	ssize_t lstride;
	BN_ULONG *dst, *src;
	size_t i;

	if (n < 0) {
		BNerror(BN_R_INVALID_LENGTH);
		return 0;
	}
	shift_bits = n;

	/*
	 * Right bit shift, potentially across word boundaries.
	 *
	 * When shift is not an exact multiple of BN_BITS2, the top bits of
	 * the next word need to be left shifted and combined with the right
	 * shifted bits using bitwise OR. If shift is an exact multiple of
	 * BN_BITS2, the source for the left and right shifts are the same
	 * and the shifts become zero (which is effectively a memmove).
	 */
	shift_words = shift_bits / BN_BITS2;
	rshift = shift_bits % BN_BITS2;
	lshift = (BN_BITS2 - rshift) % BN_BITS2;
	lstride = (lshift + rshift) / BN_BITS2;

	if (a->top <= shift_words) {
		BN_zero(r);
		return 1;
	}
	count = a->top - shift_words;

	if (!bn_wexpand(r, count))
		return 0;

	src = a->d + shift_words;
	dst = r->d;

	for (i = 1; i < count; i++) {
		*dst = src[lstride] << lshift | *src >> rshift;
		src++;
		dst++;
	}
	*dst = *src >> rshift;

	r->top = count;
	bn_correct_top(r);

	BN_set_negative(r, a->neg);

	return 1;
}

int
BN_lshift1(BIGNUM *r, const BIGNUM *a)
{
	return bn_lshift(r, a, 1);
}
LCRYPTO_ALIAS(BN_lshift1);

int
BN_lshift(BIGNUM *r, const BIGNUM *a, int n)
{
	return bn_lshift(r, a, n);
}
LCRYPTO_ALIAS(BN_lshift);

int
BN_rshift1(BIGNUM *r, const BIGNUM *a)
{
	return bn_rshift(r, a, 1);
}
LCRYPTO_ALIAS(BN_rshift1);

int
BN_rshift(BIGNUM *r, const BIGNUM *a, int n)
{
	return bn_rshift(r, a, n);
}
LCRYPTO_ALIAS(BN_rshift);
