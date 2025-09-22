/*	$OpenBSD: bn_internal.h,v 1.20 2025/08/02 16:20:00 jsing Exp $ */
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

#ifndef HEADER_BN_INTERNAL_H
#define HEADER_BN_INTERNAL_H

int bn_word_clz(BN_ULONG w);

int bn_bitsize(const BIGNUM *bn);

BN_ULONG bn_add_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    int num);
BN_ULONG bn_sub_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    int num);
BN_ULONG bn_sub_words_borrow(const BN_ULONG *a, const BN_ULONG *b, size_t n);
BN_ULONG bn_add_words_masked(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    BN_ULONG mask, size_t n);
BN_ULONG bn_sub_words_masked(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    BN_ULONG mask, size_t n);
void bn_mod_add_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    const BN_ULONG *m, size_t n);
void bn_mod_sub_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    const BN_ULONG *m, size_t n);
void bn_mod_mul_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *b,
    const BN_ULONG *m, BN_ULONG *t, BN_ULONG m0, size_t n);
void bn_mod_sqr_words(BN_ULONG *r, const BN_ULONG *a, const BN_ULONG *m,
    BN_ULONG *t, BN_ULONG m0, size_t n);

void bn_montgomery_multiply_words(BN_ULONG *rp, const BN_ULONG *ap,
    const BN_ULONG *bp, const BN_ULONG *np, BN_ULONG *tp, BN_ULONG n0,
    int n_len);
void bn_montgomery_reduce_words(BN_ULONG *r, BN_ULONG *a, const BN_ULONG *n,
    BN_ULONG n0, int n_len);

#ifndef HAVE_BN_CT_NE_ZERO
static inline int
bn_ct_ne_zero(BN_ULONG w)
{
	return (w | ~(w - 1)) >> (BN_BITS2 - 1);
}
#endif

#ifndef HAVE_BN_CT_NE_ZERO_MASK
static inline BN_ULONG
bn_ct_ne_zero_mask(BN_ULONG w)
{
	return 0 - bn_ct_ne_zero(w);
}
#endif

#ifndef HAVE_BN_CT_EQ_ZERO
static inline int
bn_ct_eq_zero(BN_ULONG w)
{
	return 1 - bn_ct_ne_zero(w);
}
#endif

#ifndef HAVE_BN_CT_EQ_ZERO_MASK
static inline BN_ULONG
bn_ct_eq_zero_mask(BN_ULONG w)
{
	return 0 - bn_ct_eq_zero(w);
}
#endif

#ifndef HAVE_BN_CLZW
static inline int
bn_clzw(BN_ULONG w)
{
	return bn_word_clz(w);
}
#endif

/*
 * Big number primitives are named as the operation followed by a suffix
 * that indicates the number of words that it operates on, where 'w' means
 * single word, 'dw' means double word, 'tw' means triple word and 'qw' means
 * quadruple word. Unless otherwise noted, the size of the output is implied
 * based on its inputs, for example bn_mulw() takes two single word inputs
 * and is going to produce a double word result.
 *
 * Where a function implements multiple operations, these are listed in order.
 * For example, a function that computes (r1:r0) = a * b + c is named
 * bn_mulw_addw(), producing a double word result.
 */

/*
 * Default implementations for BN_ULLONG architectures.
 *
 * On these platforms the C compiler is generally better at optimising without
 * the use of inline assembly primitives. However, it can be difficult for the
 * compiler to see through primitives in order to combine operations, due to
 * type changes/narrowing. For this reason compound primitives are usually
 * explicitly provided.
 */
#ifdef BN_ULLONG

#ifndef HAVE_BN_ADDW
#define HAVE_BN_ADDW
static inline void
bn_addw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULLONG r;

	r = (BN_ULLONG)a + (BN_ULLONG)b;

	*out_r1 = r >> BN_BITS2;
	*out_r0 = r & BN_MASK2;
}
#endif

#ifndef HAVE_BN_ADDW_ADDW
#define HAVE_BN_ADDW_ADDW
static inline void
bn_addw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULLONG r;

	r = (BN_ULLONG)a + (BN_ULLONG)b + (BN_ULLONG)c;

	*out_r1 = r >> BN_BITS2;
	*out_r0 = r & BN_MASK2;
}
#endif

#ifndef HAVE_BN_MULW
#define HAVE_BN_MULW
static inline void
bn_mulw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULLONG r;

	r = (BN_ULLONG)a * (BN_ULLONG)b;

	*out_r1 = r >> BN_BITS2;
	*out_r0 = r & BN_MASK2;
}
#endif

#ifndef HAVE_BN_MULW_ADDW
#define HAVE_BN_MULW_ADDW
static inline void
bn_mulw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULLONG r;

	r = (BN_ULLONG)a * (BN_ULLONG)b + (BN_ULLONG)c;

	*out_r1 = r >> BN_BITS2;
	*out_r0 = r & BN_MASK2;
}
#endif

#ifndef HAVE_BN_MULW_ADDW_ADDW
#define HAVE_BN_MULW_ADDW_ADDW
static inline void
bn_mulw_addw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG d,
    BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULLONG r;

	r = (BN_ULLONG)a * (BN_ULLONG)b + (BN_ULLONG)c + (BN_ULLONG)d;

	*out_r1 = r >> BN_BITS2;
	*out_r0 = r & BN_MASK2;
}
#endif

#endif /* !BN_ULLONG */

/*
 * bn_addw() computes (r1:r0) = a + b, where both inputs are single words,
 * producing a double word result. The value of r1 is the carry from the
 * addition.
 */
#ifndef HAVE_BN_ADDW
static inline void
bn_addw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r1, r0, c1, c2;

	c1 = a | b;
	c2 = a & b;
	r0 = a + b;
	r1 = ((c1 & ~r0) | c2) >> (BN_BITS2 - 1); /* carry */

	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_addw_addw() computes (r1:r0) = a + b + c, where all inputs are single
 * words, producing a double word result.
 */
#ifndef HAVE_BN_ADDW_ADDW
static inline void
bn_addw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULONG carry, r1, r0;

	bn_addw(a, b, &r1, &r0);
	bn_addw(r0, c, &carry, &r0);
	r1 += carry;

	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_qwaddqw() computes
 * (r4:r3:r2:r1:r0) = (a3:a2:a1:a0) + (b3:b2:b1:b0) + carry, where a is a quad word,
 * b is a quad word, and carry is a single word with value 0 or 1, producing a four
 * word result and carry.
 */
#ifndef HAVE_BN_QWADDQW
static inline void
bn_qwaddqw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0, BN_ULONG b3,
    BN_ULONG b2, BN_ULONG b1, BN_ULONG b0, BN_ULONG carry, BN_ULONG *out_carry,
    BN_ULONG *out_r3, BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r3, r2, r1, r0;

	bn_addw_addw(a0, b0, carry, &carry, &r0);
	bn_addw_addw(a1, b1, carry, &carry, &r1);
	bn_addw_addw(a2, b2, carry, &carry, &r2);
	bn_addw_addw(a3, b3, carry, &carry, &r3);

	*out_carry = carry;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_subw() computes r0 = a - b, where both inputs are single words,
 * producing a single word result and borrow.
 */
#ifndef HAVE_BN_SUBW
static inline void
bn_subw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_borrow, BN_ULONG *out_r0)
{
	BN_ULONG borrow, r0;

	r0 = a - b;
	borrow = ((r0 | (b & ~a)) & (b | ~a)) >> (BN_BITS2 - 1);

	*out_borrow = borrow;
	*out_r0 = r0;
}
#endif

/*
 * bn_subw_subw() computes r0 = a - b - c, where all inputs are single words,
 * producing a single word result and borrow.
 */
#ifndef HAVE_BN_SUBW_SUBW
static inline void
bn_subw_subw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_borrow,
    BN_ULONG *out_r0)
{
	BN_ULONG b1, b2, r0;

	bn_subw(a, b, &b1, &r0);
	bn_subw(r0, c, &b2, &r0);

	*out_borrow = b1 + b2;
	*out_r0 = r0;
}
#endif

/*
 * bn_qwsubqw() computes
 * (r3:r2:r1:r0) = (a3:a2:a1:a0) - (b3:b2:b1:b0) - borrow, where a is a quad word,
 * b is a quad word, and borrow is a single word with value 0 or 1, producing a
 * four word result and borrow.
 */
#ifndef HAVE_BN_QWSUBQW
static inline void
bn_qwsubqw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0, BN_ULONG b3,
    BN_ULONG b2, BN_ULONG b1, BN_ULONG b0, BN_ULONG borrow, BN_ULONG *out_borrow,
    BN_ULONG *out_r3, BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r3, r2, r1, r0;

	bn_subw_subw(a0, b0, borrow, &borrow, &r0);
	bn_subw_subw(a1, b1, borrow, &borrow, &r1);
	bn_subw_subw(a2, b2, borrow, &borrow, &r2);
	bn_subw_subw(a3, b3, borrow, &borrow, &r3);

	*out_borrow = borrow;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_mulw() computes (r1:r0) = a * b, where both inputs are single words,
 * producing a double word result.
 */
#ifndef HAVE_BN_MULW
/*
 * Multiply two words (a * b) producing a double word result (h:l).
 *
 * This can be rewritten as:
 *
 *  a * b = (hi32(a) * 2^32 + lo32(a)) * (hi32(b) * 2^32 + lo32(b))
 *        = hi32(a) * hi32(b) * 2^64 +
 *          hi32(a) * lo32(b) * 2^32 +
 *          hi32(b) * lo32(a) * 2^32 +
 *          lo32(a) * lo32(b)
 *
 * The multiplication for each part of a and b can be calculated for each of
 * these four terms without overflowing a BN_ULONG, as the maximum value of a
 * 32 bit x 32 bit multiplication is 32 + 32 = 64 bits. Once these
 * multiplications have been performed the result can be partitioned and summed
 * into a double word (h:l). The same applies on a 32 bit system, substituting
 * 16 for 32 and 32 for 64.
 */
#if 1
static inline void
bn_mulw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG a1, a0, b1, b0, r1, r0;
	BN_ULONG carry, x;

	a1 = a >> BN_BITS4;
	a0 = a & BN_MASK2l;
	b1 = b >> BN_BITS4;
	b0 = b & BN_MASK2l;

	r1 = a1 * b1;
	r0 = a0 * b0;

	/* (a1 * b0) << BN_BITS4, partition the result across r1:r0 with carry. */
	x = a1 * b0;
	r1 += x >> BN_BITS4;
	bn_addw(r0, x << BN_BITS4, &carry, &r0);
	r1 += carry;

	/* (b1 * a0) << BN_BITS4, partition the result across r1:r0 with carry. */
	x = b1 * a0;
	r1 += x >> BN_BITS4;
	bn_addw(r0, x << BN_BITS4, &carry, &r0);
	r1 += carry;

	*out_r1 = r1;
	*out_r0 = r0;
}
#else

/*
 * XXX - this accumulator based version uses fewer instructions, however
 * requires more variables/registers. It seems to be slower on at least amd64
 * and i386, however may be faster on other architectures that have more
 * registers available. Further testing is required and one of the two
 * implementations should eventually be removed.
 */
static inline void
bn_mulw(BN_ULONG a, BN_ULONG b, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG a1, a0, b1, b0, r1, r0, x;
	BN_ULONG acc0, acc1, acc2, acc3;

	a1 = a >> BN_BITS4;
	b1 = b >> BN_BITS4;
	a0 = a & BN_MASK2l;
	b0 = b & BN_MASK2l;

	r1 = a1 * b1;
	r0 = a0 * b0;

	acc0 = r0 & BN_MASK2l;
	acc1 = r0 >> BN_BITS4;
	acc2 = r1 & BN_MASK2l;
	acc3 = r1 >> BN_BITS4;

	/* (a1 * b0) << BN_BITS4, partition the result across r1:r0. */
	x = a1 * b0;
	acc1 += x & BN_MASK2l;
	acc2 += (acc1 >> BN_BITS4) + (x >> BN_BITS4);
	acc1 &= BN_MASK2l;
	acc3 += acc2 >> BN_BITS4;
	acc2 &= BN_MASK2l;

	/* (b1 * a0) << BN_BITS4, partition the result across r1:r0. */
	x = b1 * a0;
	acc1 += x & BN_MASK2l;
	acc2 += (acc1 >> BN_BITS4) + (x >> BN_BITS4);
	acc1 &= BN_MASK2l;
	acc3 += acc2 >> BN_BITS4;
	acc2 &= BN_MASK2l;

	*out_r1 = (acc3 << BN_BITS4) | acc2;
	*out_r0 = (acc1 << BN_BITS4) | acc0;
}
#endif
#endif

#ifndef HAVE_BN_MULW_LO
static inline BN_ULONG
bn_mulw_lo(BN_ULONG a, BN_ULONG b)
{
	return a * b;
}
#endif

#ifndef HAVE_BN_MULW_HI
static inline BN_ULONG
bn_mulw_hi(BN_ULONG a, BN_ULONG b)
{
	BN_ULONG h, l;

	bn_mulw(a, b, &h, &l);

	return h;
}
#endif

/*
 * bn_mulw_addw() computes (r1:r0) = a * b + c with all inputs being single
 * words, producing a double word result.
 */
#ifndef HAVE_BN_MULW_ADDW
static inline void
bn_mulw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULONG carry, r1, r0;

	bn_mulw(a, b, &r1, &r0);
	bn_addw(r0, c, &carry, &r0);
	r1 += carry;

	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_mulw_addw_addw() computes (r1:r0) = a * b + c + d with all inputs being
 * single words, producing a double word result.
 */
#ifndef HAVE_BN_MULW_ADDW_ADDW
static inline void
bn_mulw_addw_addw(BN_ULONG a, BN_ULONG b, BN_ULONG c, BN_ULONG d,
    BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG carry, r1, r0;

	bn_mulw_addw(a, b, c, &r1, &r0);
	bn_addw(r0, d, &carry, &r0);
	r1 += carry;

	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_mulw_addtw() computes (r2:r1:r0) = a * b + (c2:c1:c0), where a and b are
 * single words and (c2:c1:c0) is a triple word, producing a triple word result.
 * The caller must ensure that the inputs provided do not result in c2
 * overflowing.
 */
#ifndef HAVE_BN_MULW_ADDTW
static inline void
bn_mulw_addtw(BN_ULONG a, BN_ULONG b, BN_ULONG c2, BN_ULONG c1, BN_ULONG c0,
    BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG carry, r2, r1, r0, x1;

	bn_mulw_addw(a, b, c0, &x1, &r0);
	bn_addw(c1, x1, &carry, &r1);
	r2 = c2 + carry;

	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_mul2_mulw_addtw() computes (r2:r1:r0) = 2 * a * b + (c2:c1:c0), where a
 * and b are single words and (c2:c1:c0) is a triple word, producing a triple
 * word result. The caller must ensure that the inputs provided do not result
 * in c2 overflowing.
 */
#ifndef HAVE_BN_MUL2_MULW_ADDTW
static inline void
bn_mul2_mulw_addtw(BN_ULONG a, BN_ULONG b, BN_ULONG c2, BN_ULONG c1, BN_ULONG c0,
    BN_ULONG *out_r2, BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r2, r1, r0, x1, x0;
	BN_ULONG carry;

	bn_mulw(a, b, &x1, &x0);
	bn_addw(c0, x0, &carry, &r0);
	bn_addw(c1, x1 + carry, &r2, &r1);
	bn_addw(c2, r2, &carry, &r2);
	bn_addw(r0, x0, &carry, &r0);
	bn_addw(r1, x1 + carry, &carry, &r1);
	r2 += carry;

	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_qwmulw_addw() computes (r4:r3:r2:r1:r0) = (a3:a2:a1:a0) * b + c, where a
 * is a quad word, b is a single word and c is a single word, producing a five
 * word result.
 */
#ifndef HAVE_BN_QWMULW_ADDW
static inline void
bn_qwmulw_addw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0, BN_ULONG b,
    BN_ULONG c, BN_ULONG *out_r4, BN_ULONG *out_r3, BN_ULONG *out_r2,
    BN_ULONG *out_r1, BN_ULONG *out_r0)
{
	BN_ULONG r3, r2, r1, r0;

	bn_mulw_addw(a0, b, c, &c, &r0);
	bn_mulw_addw(a1, b, c, &c, &r1);
	bn_mulw_addw(a2, b, c, &c, &r2);
	bn_mulw_addw(a3, b, c, &c, &r3);

	*out_r4 = c;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

/*
 * bn_qwmulw_addqw_addw() computes
 * (r4:r3:r2:r1:r0) = (a3:a2:a1:a0) * b + (c3:c2:c1:c0) + d, where a
 * is a quad word, b is a single word, c is a quad word, and d is a single word,
 * producing a five word result.
 */
#ifndef HAVE_BN_QWMULW_ADDQW_ADDW
static inline void
bn_qwmulw_addqw_addw(BN_ULONG a3, BN_ULONG a2, BN_ULONG a1, BN_ULONG a0,
    BN_ULONG b, BN_ULONG c3, BN_ULONG c2, BN_ULONG c1, BN_ULONG c0, BN_ULONG d,
    BN_ULONG *out_r4, BN_ULONG *out_r3, BN_ULONG *out_r2, BN_ULONG *out_r1,
    BN_ULONG *out_r0)
{
	BN_ULONG r3, r2, r1, r0;

	bn_mulw_addw_addw(a0, b, c0, d, &d, &r0);
	bn_mulw_addw_addw(a1, b, c1, d, &d, &r1);
	bn_mulw_addw_addw(a2, b, c2, d, &d, &r2);
	bn_mulw_addw_addw(a3, b, c3, d, &d, &r3);

	*out_r4 = d;
	*out_r3 = r3;
	*out_r2 = r2;
	*out_r1 = r1;
	*out_r0 = r0;
}
#endif

#endif
