/* $OpenBSD: bn_primitives.c,v 1.2 2023/06/21 07:48:41 jsing Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include "bn_arch.h"
#include "bn_internal.h"
#include "bn_local.h"

#ifndef HAVE_BN_CLZW
#ifndef HAVE_BN_WORD_CLZ
int
bn_word_clz(BN_ULONG w)
{
	BN_ULONG bits, mask, shift;

	bits = shift = BN_BITS2;
	mask = 0;

	while ((shift >>= 1) != 0) {
		bits += (shift & mask) - (shift & ~mask);
		mask = bn_ct_ne_zero_mask(w >> bits);
	}
	bits += 1 & mask;

	bits -= bn_ct_eq_zero(w);

	return BN_BITS2 - bits;
}
#endif
#endif

#ifndef HAVE_BN_BITSIZE
int
bn_bitsize(const BIGNUM *bn)
{
	BN_ULONG n = 0, x = 0;
	BN_ULONG mask, w;
	int i = 0;

	while (i < bn->top) {
		w = bn->d[i];
		mask = bn_ct_ne_zero_mask(w);
		n = ((BN_ULONG)i & mask) | (n & ~mask);
		x = (w & mask) | (x & ~mask);
		i++;
	}

	return (n + 1) * BN_BITS2 - bn_clzw(x);
}
#endif
