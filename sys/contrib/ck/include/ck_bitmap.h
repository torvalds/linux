/*
 * Copyright 2012-2015 Samy Al Bahra.
 * Copyright 2012-2014 AppNexus, Inc.
 * Copyright 2014 Paul Khuong.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 */

#ifndef CK_BITMAP_H
#define CK_BITMAP_H

#include <ck_cc.h>
#include <ck_limits.h>
#include <ck_pr.h>
#include <ck_stdint.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>
#include <ck_stdbool.h>
#include <ck_stddef.h>
#include <ck_string.h>

#if !defined(CK_F_PR_LOAD_UINT) || !defined(CK_F_PR_STORE_UINT) || \
    !defined(CK_F_PR_AND_UINT) || !defined(CK_F_PR_OR_UINT) || \
    !defined(CK_F_CC_CTZ)
#error "ck_bitmap is not supported on your platform."
#endif

#define CK_BITMAP_BLOCK 	(sizeof(unsigned int) * CHAR_BIT)
#define CK_BITMAP_OFFSET(i)	((i) % CK_BITMAP_BLOCK)
#define CK_BITMAP_BIT(i)	(1U << CK_BITMAP_OFFSET(i))
#define CK_BITMAP_PTR(x, i)	((x) + ((i) / CK_BITMAP_BLOCK))
#define CK_BITMAP_BLOCKS(n)	(((n) + CK_BITMAP_BLOCK - 1) / CK_BITMAP_BLOCK)

#define CK_BITMAP_INSTANCE(n_entries)					\
	union {								\
		struct {						\
			unsigned int n_bits;				\
			unsigned int map[CK_BITMAP_BLOCKS(n_entries)];	\
		} content;						\
		struct ck_bitmap bitmap;				\
	}

#define CK_BITMAP_ITERATOR_INIT(a, b) \
	ck_bitmap_iterator_init((a), &(b)->bitmap)

#define CK_BITMAP_INIT(a, b, c) \
	ck_bitmap_init(&(a)->bitmap, (b), (c))

#define CK_BITMAP_NEXT(a, b, c) \
	ck_bitmap_next(&(a)->bitmap, (b), (c))

#define CK_BITMAP_SET(a, b) \
	ck_bitmap_set(&(a)->bitmap, (b))

#define CK_BITMAP_BTS(a, b) \
	ck_bitmap_bts(&(a)->bitmap, (b))

#define CK_BITMAP_RESET(a, b) \
	ck_bitmap_reset(&(a)->bitmap, (b))

#define CK_BITMAP_TEST(a, b) \
	ck_bitmap_test(&(a)->bitmap, (b))

#define CK_BITMAP_UNION(a, b) \
	ck_bitmap_union(&(a)->bitmap, &(b)->bitmap)

#define CK_BITMAP_INTERSECTION(a, b) \
	ck_bitmap_intersection(&(a)->bitmap, &(b)->bitmap)

#define CK_BITMAP_INTERSECTION_NEGATE(a, b) \
	ck_bitmap_intersection_negate(&(a)->bitmap, &(b)->bitmap)

#define CK_BITMAP_CLEAR(a) \
	ck_bitmap_clear(&(a)->bitmap)

#define CK_BITMAP_EMPTY(a, b) \
	ck_bitmap_empty(&(a)->bitmap, b)

#define CK_BITMAP_FULL(a, b) \
	ck_bitmap_full(&(a)->bitmap, b)

#define CK_BITMAP_COUNT(a, b) \
	ck_bitmap_count(&(a)->bitmap, b)

#define CK_BITMAP_COUNT_INTERSECT(a, b, c) \
	ck_bitmap_count_intersect(&(a)->bitmap, b, c)

#define CK_BITMAP_BITS(a) \
	ck_bitmap_bits(&(a)->bitmap)

#define CK_BITMAP_BUFFER(a) \
	ck_bitmap_buffer(&(a)->bitmap)

#define CK_BITMAP(a) \
	(&(a)->bitmap)

struct ck_bitmap {
	unsigned int n_bits;
	unsigned int map[];
};
typedef struct ck_bitmap ck_bitmap_t;

struct ck_bitmap_iterator {
	unsigned int cache;
	unsigned int n_block;
	unsigned int n_limit;
};
typedef struct ck_bitmap_iterator ck_bitmap_iterator_t;

CK_CC_INLINE static unsigned int
ck_bitmap_base(unsigned int n_bits)
{

	return CK_BITMAP_BLOCKS(n_bits) * sizeof(unsigned int);
}

/*
 * Returns the required number of bytes for a ck_bitmap_t object supporting the
 * specified number of bits.
 */
CK_CC_INLINE static unsigned int
ck_bitmap_size(unsigned int n_bits)
{

	return ck_bitmap_base(n_bits) + sizeof(struct ck_bitmap);
}

/*
 * Returns total number of bits in specified bitmap.
 */
CK_CC_INLINE static unsigned int
ck_bitmap_bits(const struct ck_bitmap *bitmap)
{

	return bitmap->n_bits;
}

/*
 * Returns a pointer to the bit buffer associated
 * with the specified bitmap.
 */
CK_CC_INLINE static void *
ck_bitmap_buffer(struct ck_bitmap *bitmap)
{

	return bitmap->map;
}

/*
 * Sets the bit at the offset specified in the second argument.
 */
CK_CC_INLINE static void
ck_bitmap_set(struct ck_bitmap *bitmap, unsigned int n)
{

	ck_pr_or_uint(CK_BITMAP_PTR(bitmap->map, n), CK_BITMAP_BIT(n));
	return;
}

/*
 * Performs a test-and-set operation at the offset specified in the
 * second argument.
 * Returns true if the bit at the specified offset was already set,
 * false otherwise.
 */
CK_CC_INLINE static bool
ck_bitmap_bts(struct ck_bitmap *bitmap, unsigned int n)
{

	return ck_pr_bts_uint(CK_BITMAP_PTR(bitmap->map, n),
	    CK_BITMAP_OFFSET(n));
}

/*
 * Resets the bit at the offset specified in the second argument.
 */
CK_CC_INLINE static void
ck_bitmap_reset(struct ck_bitmap *bitmap, unsigned int n)
{

	ck_pr_and_uint(CK_BITMAP_PTR(bitmap->map, n), ~CK_BITMAP_BIT(n));
	return;
}

/*
 * Determines whether the bit at offset specified in the
 * second argument is set.
 */
CK_CC_INLINE static bool
ck_bitmap_test(const struct ck_bitmap *bitmap, unsigned int n)
{
	unsigned int block;

	block = ck_pr_load_uint(CK_BITMAP_PTR(bitmap->map, n));
	return block & CK_BITMAP_BIT(n);
}

/*
 * Combines bits from second bitmap into the first bitmap. This is not a
 * linearized operation with respect to the complete bitmap.
 */
CK_CC_INLINE static void
ck_bitmap_union(struct ck_bitmap *dst, const struct ck_bitmap *src)
{
	unsigned int n;
	unsigned int n_buckets = dst->n_bits;

	if (src->n_bits < dst->n_bits)
		n_buckets = src->n_bits;

	n_buckets = CK_BITMAP_BLOCKS(n_buckets);
	for (n = 0; n < n_buckets; n++) {
		ck_pr_or_uint(&dst->map[n],
		    ck_pr_load_uint(&src->map[n]));
	}

	return;
}

/*
 * Intersects bits from second bitmap into the first bitmap. This is
 * not a linearized operation with respect to the complete bitmap.
 * Any trailing bit in dst is cleared.
 */
CK_CC_INLINE static void
ck_bitmap_intersection(struct ck_bitmap *dst, const struct ck_bitmap *src)
{
	unsigned int n;
	unsigned int n_buckets = dst->n_bits;
	unsigned int n_intersect = n_buckets;

	if (src->n_bits < n_intersect)
		n_intersect = src->n_bits;

	n_buckets = CK_BITMAP_BLOCKS(n_buckets);
	n_intersect = CK_BITMAP_BLOCKS(n_intersect);
	for (n = 0; n < n_intersect; n++) {
		ck_pr_and_uint(&dst->map[n],
		    ck_pr_load_uint(&src->map[n]));
	}

	for (; n < n_buckets; n++)
		ck_pr_store_uint(&dst->map[n], 0);

	return;
}

/*
 * Intersects the complement of bits from second bitmap into the first
 * bitmap. This is not a linearized operation with respect to the
 * complete bitmap.  Any trailing bit in dst is left as is.
 */
CK_CC_INLINE static void
ck_bitmap_intersection_negate(struct ck_bitmap *dst,
    const struct ck_bitmap *src)
{
	unsigned int n;
	unsigned int n_intersect = dst->n_bits;

	if (src->n_bits < n_intersect)
		n_intersect = src->n_bits;

	n_intersect = CK_BITMAP_BLOCKS(n_intersect);
	for (n = 0; n < n_intersect; n++) {
		ck_pr_and_uint(&dst->map[n],
		    (~ck_pr_load_uint(&src->map[n])));
	}

	return;
}

/*
 * Resets all bits in the provided bitmap. This is not a linearized
 * operation in ck_bitmap.
 */
CK_CC_INLINE static void
ck_bitmap_clear(struct ck_bitmap *bitmap)
{
	unsigned int i;
	unsigned int n_buckets = ck_bitmap_base(bitmap->n_bits) /
	    sizeof(unsigned int);

	for (i = 0; i < n_buckets; i++)
		ck_pr_store_uint(&bitmap->map[i], 0);

	return;
}

/*
 * Returns true if the first limit bits in bitmap are cleared.  If
 * limit is greater than the bitmap size, limit is truncated to that
 * size.
 */
CK_CC_INLINE static bool
ck_bitmap_empty(const ck_bitmap_t *bitmap, unsigned int limit)
{
	unsigned int i, words, slop;

	if (limit > bitmap->n_bits)
		limit = bitmap->n_bits;

	words = limit / CK_BITMAP_BLOCK;
	slop = limit % CK_BITMAP_BLOCK;
	for (i = 0; i < words; i++) {
		if (ck_pr_load_uint(&bitmap->map[i]) != 0) {
			return false;
		}
	}

	if (slop > 0) {
		unsigned int word;

		word = ck_pr_load_uint(&bitmap->map[i]);
		if ((word & ((1U << slop) - 1)) != 0)
			return false;
	}

	return true;
}

/*
 * Returns true if the first limit bits in bitmap are set.  If limit
 * is greater than the bitmap size, limit is truncated to that size.
 */
CK_CC_UNUSED static bool
ck_bitmap_full(const ck_bitmap_t *bitmap, unsigned int limit)
{
	unsigned int i, slop, words;

	if (limit > bitmap->n_bits) {
		limit = bitmap->n_bits;
	}

	words = limit / CK_BITMAP_BLOCK;
	slop = limit % CK_BITMAP_BLOCK;
	for (i = 0; i < words; i++) {
		if (ck_pr_load_uint(&bitmap->map[i]) != -1U)
			return false;
	}

	if (slop > 0) {
		unsigned int word;

		word = ~ck_pr_load_uint(&bitmap->map[i]);
		if ((word & ((1U << slop) - 1)) != 0)
			return false;
	}
	return true;
}

/*
 * Returns the number of set bit in bitmap, upto (and excluding)
 * limit.  If limit is greater than the bitmap size, it is truncated
 * to that size.
 */
CK_CC_INLINE static unsigned int
ck_bitmap_count(const ck_bitmap_t *bitmap, unsigned int limit)
{
	unsigned int count, i, slop, words;

	if (limit > bitmap->n_bits)
		limit = bitmap->n_bits;

	words = limit / CK_BITMAP_BLOCK;
	slop = limit % CK_BITMAP_BLOCK;
	for (i = 0, count = 0; i < words; i++)
		count += ck_cc_popcount(ck_pr_load_uint(&bitmap->map[i]));

	if (slop > 0) {
		unsigned int word;

		word = ck_pr_load_uint(&bitmap->map[i]);
		count += ck_cc_popcount(word & ((1U << slop) - 1));
	}
	return count;
}

/*
 * Returns the number of set bit in the intersection of two bitmaps,
 * upto (and excluding) limit.  If limit is greater than either bitmap
 * size, it is truncated to the smallest.
 */
CK_CC_INLINE static unsigned int
ck_bitmap_count_intersect(const ck_bitmap_t *x, const ck_bitmap_t *y,
    unsigned int limit)
{
	unsigned int count, i, slop, words;

	if (limit > x->n_bits)
		limit = x->n_bits;

	if (limit > y->n_bits)
		limit = y->n_bits;

	words = limit / CK_BITMAP_BLOCK;
	slop = limit % CK_BITMAP_BLOCK;
	for (i = 0, count = 0; i < words; i++) {
		unsigned int xi, yi;

		xi = ck_pr_load_uint(&x->map[i]);
		yi = ck_pr_load_uint(&y->map[i]);
		count += ck_cc_popcount(xi & yi);
	}

	if (slop > 0) {
		unsigned int word, xi, yi;

		xi = ck_pr_load_uint(&x->map[i]);
		yi = ck_pr_load_uint(&y->map[i]);
		word = xi & yi;
		count += ck_cc_popcount(word & ((1U << slop) - 1));
	}
	return count;
}

/*
 * Initializes a ck_bitmap pointing to a region of memory with
 * ck_bitmap_size(n_bits) bytes. Third argument determines whether
 * default bit value is 1 (true) or 0 (false).
 */
CK_CC_INLINE static void
ck_bitmap_init(struct ck_bitmap *bitmap,
	       unsigned int n_bits,
	       bool set)
{
	unsigned int base = ck_bitmap_base(n_bits);

	bitmap->n_bits = n_bits;
	memset(bitmap->map, -(int)set, base);

	if (set == true) {
		unsigned int b = n_bits % CK_BITMAP_BLOCK;

		if (b == 0)
			return;

		*CK_BITMAP_PTR(bitmap->map, n_bits - 1) &= (1U << b) - 1U;
	}

	return;
}

/*
 * Initialize iterator for use with provided bitmap.
 */
CK_CC_INLINE static void
ck_bitmap_iterator_init(struct ck_bitmap_iterator *i,
    const struct ck_bitmap *bitmap)
{

	i->n_block = 0;
	i->n_limit = CK_BITMAP_BLOCKS(bitmap->n_bits);
	if (i->n_limit > 0) {
		i->cache = ck_pr_load_uint(&bitmap->map[0]);
	} else {
		i->cache = 0;
	}
	return;
}

/*
 * Iterate to next bit.
 */
CK_CC_INLINE static bool
ck_bitmap_next(const struct ck_bitmap *bitmap,
	       struct ck_bitmap_iterator *i,
	       unsigned int *bit)
{
	unsigned int cache = i->cache;
	unsigned int n_block = i->n_block;
	unsigned int n_limit = i->n_limit;

	if (cache == 0) {
		if (n_block >= n_limit)
			return false;

		for (n_block++; n_block < n_limit; n_block++) {
			cache = ck_pr_load_uint(&bitmap->map[n_block]);
			if (cache != 0)
				goto non_zero;
		}

		i->cache = 0;
		i->n_block = n_block;
		return false;
	}

non_zero:
	*bit = CK_BITMAP_BLOCK * n_block + ck_cc_ctz(cache);
	i->cache = cache & (cache - 1);
	i->n_block = n_block;
	return true;
}

#endif /* CK_BITMAP_H */
