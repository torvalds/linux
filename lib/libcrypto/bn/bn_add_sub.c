/* $OpenBSD: bn_add_sub.c,v 1.1 2025/05/25 04:30:55 jsing Exp $ */
/*
 * Copyright (c) 2023,2024,2025 Joel Sing <jsing@openbsd.org>
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

#include "bn_internal.h"

/*
 * bn_add_words() computes (carry:r[i]) = a[i] + b[i] + carry, where a and b
 * are both arrays of words. Any carry resulting from the addition is returned.
 */
#ifndef HAVE_BN_ADD_WORDS
BN_ULONG
bn_add_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b, int n)
{
	BN_ULONG carry = 0;

	while (n >= 4) {
		bn_qwaddqw(a[3], a[2], a[1], a[0], b[3], b[2], b[1], b[0],
		    carry, &carry, &r[3], &r[2], &r[1], &r[0]);
		a += 4;
		b += 4;
		r += 4;
		n -= 4;
	}
	while (n > 0) {
		bn_addw_addw(a[0], b[0], carry, &carry, &r[0]);
		a++;
		b++;
		r++;
		n--;
	}

	return carry;
}
#endif

/*
 * bn_sub_words() computes (borrow:r[i]) = a[i] - b[i] - borrow, where a and b
 * are both arrays of words. Any borrow resulting from the subtraction is
 * returned.
 */
#ifndef HAVE_BN_SUB_WORDS
BN_ULONG
bn_sub_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b, int n)
{
	BN_ULONG borrow = 0;

	while (n >= 4) {
		bn_qwsubqw(a[3], a[2], a[1], a[0], b[3], b[2], b[1], b[0],
		    borrow, &borrow, &r[3], &r[2], &r[1], &r[0]);
		a += 4;
		b += 4;
		r += 4;
		n -= 4;
	}
	while (n > 0) {
		bn_subw_subw(a[0], b[0], borrow, &borrow, &r[0]);
		a++;
		b++;
		r++;
		n--;
	}

	return borrow;
}
#endif

/*
 * bn_sub_borrow() computes a[i] - b[i], returning the resulting borrow only.
 */
#ifndef HAVE_BN_SUB_WORDS_BORROW
BN_ULONG
bn_sub_words_borrow(const BN_ULONG *a, const BN_ULONG *b, size_t n)
{
	BN_ULONG borrow = 0;
	BN_ULONG r;

	while (n >= 4) {
		bn_qwsubqw(a[3], a[2], a[1], a[0], b[3], b[2], b[1], b[0],
		    borrow, &borrow, &r, &r, &r, &r);
		a += 4;
		b += 4;
		n -= 4;
	}
	while (n > 0) {
		bn_subw_subw(a[0], b[0], borrow, &borrow, &r);
		a++;
		b++;
		n--;
	}

	return borrow;
}
#endif

/*
 * bn_add_words_masked() computes r[] = a[] + (b[] & mask), where a, b and r are
 * arrays of words with length n (r may be the same as a or b).
 */
#ifndef HAVE_BN_ADD_WORDS_MASKED
BN_ULONG
bn_add_words_masked(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    BN_ULONG mask, size_t n)
{
	BN_ULONG carry = 0;

	/* XXX - consider conditional/masked versions of bn_addw_addw/bn_qwaddqw. */

	while (n >= 4) {
		bn_qwaddqw(a[3], a[2], a[1], a[0], b[3] & mask, b[2] & mask,
		    b[1] & mask, b[0] & mask, carry, &carry, &r[3], &r[2],
		    &r[1], &r[0]);
		a += 4;
		b += 4;
		r += 4;
		n -= 4;
	}
	while (n > 0) {
		bn_addw_addw(a[0], b[0] & mask, carry, &carry, &r[0]);
		a++;
		b++;
		r++;
		n--;
	}

	return carry;
}
#endif

/*
 * bn_sub_words_masked() computes r[] = a[] - (b[] & mask), where a, b and r are
 * arrays of words with length n (r may be the same as a or b).
 */
#ifndef HAVE_BN_SUB_WORDS_MASKED
BN_ULONG
bn_sub_words_masked(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    BN_ULONG mask, size_t n)
{
	BN_ULONG borrow = 0;

	/* XXX - consider conditional/masked versions of bn_subw_subw/bn_qwsubqw. */

	/* Compute conditional r[i] = a[i] - b[i]. */
	while (n >= 4) {
		bn_qwsubqw(a[3], a[2], a[1], a[0], b[3] & mask, b[2] & mask,
		    b[1] & mask, b[0] & mask, borrow, &borrow, &r[3], &r[2],
		    &r[1], &r[0]);
		a += 4;
		b += 4;
		r += 4;
		n -= 4;
	}
	while (n > 0) {
		bn_subw_subw(a[0], b[0] & mask, borrow, &borrow, &r[0]);
		a++;
		b++;
		r++;
		n--;
	}

	return borrow;
}
#endif
