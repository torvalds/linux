/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_BITMAP_H
#define __LINUX_BITMAP_H

#ifndef __ASSEMBLY__

#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/cleanup.h>
#include <linux/errno.h>
#include <linux/find.h>
#include <linux/limits.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bitmap-str.h>

struct device;

/*
 * bitmaps provide bit arrays that consume one or more unsigned
 * longs.  The bitmap interface and available operations are listed
 * here, in bitmap.h
 *
 * Function implementations generic to all architectures are in
 * lib/bitmap.c.  Functions implementations that are architecture
 * specific are in various include/asm-<arch>/bitops.h headers
 * and other arch/<arch> specific files.
 *
 * See lib/bitmap.c for more details.
 */

/**
 * DOC: bitmap overview
 *
 * The available bitmap operations and their rough meaning in the
 * case that the bitmap is a single unsigned long are thus:
 *
 * The generated code is more efficient when nbits is known at
 * compile-time and at most BITS_PER_LONG.
 *
 * ::
 *
 *  bitmap_zero(dst, nbits)                     *dst = 0UL
 *  bitmap_fill(dst, nbits)                     *dst = ~0UL
 *  bitmap_copy(dst, src, nbits)                *dst = *src
 *  bitmap_and(dst, src1, src2, nbits)          *dst = *src1 & *src2
 *  bitmap_or(dst, src1, src2, nbits)           *dst = *src1 | *src2
 *  bitmap_xor(dst, src1, src2, nbits)          *dst = *src1 ^ *src2
 *  bitmap_andnot(dst, src1, src2, nbits)       *dst = *src1 & ~(*src2)
 *  bitmap_complement(dst, src, nbits)          *dst = ~(*src)
 *  bitmap_equal(src1, src2, nbits)             Are *src1 and *src2 equal?
 *  bitmap_intersects(src1, src2, nbits)        Do *src1 and *src2 overlap?
 *  bitmap_subset(src1, src2, nbits)            Is *src1 a subset of *src2?
 *  bitmap_empty(src, nbits)                    Are all bits zero in *src?
 *  bitmap_full(src, nbits)                     Are all bits set in *src?
 *  bitmap_weight(src, nbits)                   Hamming Weight: number set bits
 *  bitmap_weight_and(src1, src2, nbits)        Hamming Weight of and'ed bitmap
 *  bitmap_weight_andnot(src1, src2, nbits)     Hamming Weight of andnot'ed bitmap
 *  bitmap_set(dst, pos, nbits)                 Set specified bit area
 *  bitmap_clear(dst, pos, nbits)               Clear specified bit area
 *  bitmap_find_next_zero_area(buf, len, pos, n, mask)  Find bit free area
 *  bitmap_find_next_zero_area_off(buf, len, pos, n, mask, mask_off)  as above
 *  bitmap_shift_right(dst, src, n, nbits)      *dst = *src >> n
 *  bitmap_shift_left(dst, src, n, nbits)       *dst = *src << n
 *  bitmap_cut(dst, src, first, n, nbits)       Cut n bits from first, copy rest
 *  bitmap_replace(dst, old, new, mask, nbits)  *dst = (*old & ~(*mask)) | (*new & *mask)
 *  bitmap_scatter(dst, src, mask, nbits)	*dst = map(dense, sparse)(src)
 *  bitmap_gather(dst, src, mask, nbits)	*dst = map(sparse, dense)(src)
 *  bitmap_remap(dst, src, old, new, nbits)     *dst = map(old, new)(src)
 *  bitmap_bitremap(oldbit, old, new, nbits)    newbit = map(old, new)(oldbit)
 *  bitmap_onto(dst, orig, relmap, nbits)       *dst = orig relative to relmap
 *  bitmap_fold(dst, orig, sz, nbits)           dst bits = orig bits mod sz
 *  bitmap_parse(buf, buflen, dst, nbits)       Parse bitmap dst from kernel buf
 *  bitmap_parse_user(ubuf, ulen, dst, nbits)   Parse bitmap dst from user buf
 *  bitmap_parselist(buf, dst, nbits)           Parse bitmap dst from kernel buf
 *  bitmap_parselist_user(buf, dst, nbits)      Parse bitmap dst from user buf
 *  bitmap_find_free_region(bitmap, bits, order)  Find and allocate bit region
 *  bitmap_release_region(bitmap, pos, order)   Free specified bit region
 *  bitmap_allocate_region(bitmap, pos, order)  Allocate specified bit region
 *  bitmap_from_arr32(dst, buf, nbits)          Copy nbits from u32[] buf to dst
 *  bitmap_from_arr64(dst, buf, nbits)          Copy nbits from u64[] buf to dst
 *  bitmap_to_arr32(buf, src, nbits)            Copy nbits from buf to u32[] dst
 *  bitmap_to_arr64(buf, src, nbits)            Copy nbits from buf to u64[] dst
 *  bitmap_get_value8(map, start)               Get 8bit value from map at start
 *  bitmap_set_value8(map, value, start)        Set 8bit value to map at start
 *  bitmap_read(map, start, nbits)              Read an nbits-sized value from
 *                                              map at start
 *  bitmap_write(map, value, start, nbits)      Write an nbits-sized value to
 *                                              map at start
 *
 * Note, bitmap_zero() and bitmap_fill() operate over the region of
 * unsigned longs, that is, bits behind bitmap till the unsigned long
 * boundary will be zeroed or filled as well. Consider to use
 * bitmap_clear() or bitmap_set() to make explicit zeroing or filling
 * respectively.
 */

/**
 * DOC: bitmap bitops
 *
 * Also the following operations in asm/bitops.h apply to bitmaps.::
 *
 *  set_bit(bit, addr)                  *addr |= bit
 *  clear_bit(bit, addr)                *addr &= ~bit
 *  change_bit(bit, addr)               *addr ^= bit
 *  test_bit(bit, addr)                 Is bit set in *addr?
 *  test_and_set_bit(bit, addr)         Set bit and return old value
 *  test_and_clear_bit(bit, addr)       Clear bit and return old value
 *  test_and_change_bit(bit, addr)      Change bit and return old value
 *  find_first_zero_bit(addr, nbits)    Position first zero bit in *addr
 *  find_first_bit(addr, nbits)         Position first set bit in *addr
 *  find_next_zero_bit(addr, nbits, bit)
 *                                      Position next zero bit in *addr >= bit
 *  find_next_bit(addr, nbits, bit)     Position next set bit in *addr >= bit
 *  find_next_and_bit(addr1, addr2, nbits, bit)
 *                                      Same as find_next_bit, but in
 *                                      (*addr1 & *addr2)
 *
 */

/**
 * DOC: declare bitmap
 * The DECLARE_BITMAP(name,bits) macro, in linux/types.h, can be used
 * to declare an array named 'name' of just enough unsigned longs to
 * contain all bit positions from 0 to 'bits' - 1.
 */

/*
 * Allocation and deallocation of bitmap.
 * Provided in lib/bitmap.c to avoid circular dependency.
 */
unsigned long *bitmap_alloc(unsigned int nbits, gfp_t flags);
unsigned long *bitmap_zalloc(unsigned int nbits, gfp_t flags);
unsigned long *bitmap_alloc_node(unsigned int nbits, gfp_t flags, int node);
unsigned long *bitmap_zalloc_node(unsigned int nbits, gfp_t flags, int node);
void bitmap_free(const unsigned long *bitmap);

DEFINE_FREE(bitmap, unsigned long *, if (_T) bitmap_free(_T))

/* Managed variants of the above. */
unsigned long *devm_bitmap_alloc(struct device *dev,
				 unsigned int nbits, gfp_t flags);
unsigned long *devm_bitmap_zalloc(struct device *dev,
				  unsigned int nbits, gfp_t flags);

/*
 * lib/bitmap.c provides these functions:
 */

bool __bitmap_equal(const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int nbits);
bool __pure __bitmap_or_equal(const unsigned long *src1,
			      const unsigned long *src2,
			      const unsigned long *src3,
			      unsigned int nbits);
void __bitmap_complement(unsigned long *dst, const unsigned long *src,
			 unsigned int nbits);
void __bitmap_shift_right(unsigned long *dst, const unsigned long *src,
			  unsigned int shift, unsigned int nbits);
void __bitmap_shift_left(unsigned long *dst, const unsigned long *src,
			 unsigned int shift, unsigned int nbits);
void bitmap_cut(unsigned long *dst, const unsigned long *src,
		unsigned int first, unsigned int cut, unsigned int nbits);
bool __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
		 const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
		 const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
		  const unsigned long *bitmap2, unsigned int nbits);
bool __bitmap_andnot(unsigned long *dst, const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_replace(unsigned long *dst,
		      const unsigned long *old, const unsigned long *new,
		      const unsigned long *mask, unsigned int nbits);
bool __bitmap_intersects(const unsigned long *bitmap1,
			 const unsigned long *bitmap2, unsigned int nbits);
bool __bitmap_subset(const unsigned long *bitmap1,
		     const unsigned long *bitmap2, unsigned int nbits);
unsigned int __bitmap_weight(const unsigned long *bitmap, unsigned int nbits);
unsigned int __bitmap_weight_and(const unsigned long *bitmap1,
				 const unsigned long *bitmap2, unsigned int nbits);
unsigned int __bitmap_weight_andnot(const unsigned long *bitmap1,
				    const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_set(unsigned long *map, unsigned int start, int len);
void __bitmap_clear(unsigned long *map, unsigned int start, int len);

unsigned long bitmap_find_next_zero_area_off(unsigned long *map,
					     unsigned long size,
					     unsigned long start,
					     unsigned int nr,
					     unsigned long align_mask,
					     unsigned long align_offset);

/**
 * bitmap_find_next_zero_area - find a contiguous aligned zero area
 * @map: The address to base the search on
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @align_mask: Alignment mask for zero area
 *
 * The @align_mask should be one less than a power of 2; the effect is that
 * the bit offset of all zero areas this function finds is multiples of that
 * power of 2. A @align_mask of 0 means no alignment is required.
 */
static inline unsigned long
bitmap_find_next_zero_area(unsigned long *map,
			   unsigned long size,
			   unsigned long start,
			   unsigned int nr,
			   unsigned long align_mask)
{
	return bitmap_find_next_zero_area_off(map, size, start, nr,
					      align_mask, 0);
}

void bitmap_remap(unsigned long *dst, const unsigned long *src,
		const unsigned long *old, const unsigned long *new, unsigned int nbits);
int bitmap_bitremap(int oldbit,
		const unsigned long *old, const unsigned long *new, int bits);
void bitmap_onto(unsigned long *dst, const unsigned long *orig,
		const unsigned long *relmap, unsigned int bits);
void bitmap_fold(unsigned long *dst, const unsigned long *orig,
		unsigned int sz, unsigned int nbits);

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) & (BITS_PER_LONG - 1)))
#define BITMAP_LAST_WORD_MASK(nbits) (~0UL >> (-(nbits) & (BITS_PER_LONG - 1)))

#define bitmap_size(nbits)	(ALIGN(nbits, BITS_PER_LONG) / BITS_PER_BYTE)

static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
	unsigned int len = bitmap_size(nbits);

	if (small_const_nbits(nbits))
		*dst = 0;
	else
		memset(dst, 0, len);
}

static inline void bitmap_fill(unsigned long *dst, unsigned int nbits)
{
	unsigned int len = bitmap_size(nbits);

	if (small_const_nbits(nbits))
		*dst = ~0UL;
	else
		memset(dst, 0xff, len);
}

static inline void bitmap_copy(unsigned long *dst, const unsigned long *src,
			unsigned int nbits)
{
	unsigned int len = bitmap_size(nbits);

	if (small_const_nbits(nbits))
		*dst = *src;
	else
		memcpy(dst, src, len);
}

/*
 * Copy bitmap and clear tail bits in last word.
 */
static inline void bitmap_copy_clear_tail(unsigned long *dst,
		const unsigned long *src, unsigned int nbits)
{
	bitmap_copy(dst, src, nbits);
	if (nbits % BITS_PER_LONG)
		dst[nbits / BITS_PER_LONG] &= BITMAP_LAST_WORD_MASK(nbits);
}

/*
 * On 32-bit systems bitmaps are represented as u32 arrays internally. On LE64
 * machines the order of hi and lo parts of numbers match the bitmap structure.
 * In both cases conversion is not needed when copying data from/to arrays of
 * u32. But in LE64 case, typecast in bitmap_copy_clear_tail() may lead
 * to out-of-bound access. To avoid that, both LE and BE variants of 64-bit
 * architectures are not using bitmap_copy_clear_tail().
 */
#if BITS_PER_LONG == 64
void bitmap_from_arr32(unsigned long *bitmap, const u32 *buf,
							unsigned int nbits);
void bitmap_to_arr32(u32 *buf, const unsigned long *bitmap,
							unsigned int nbits);
#else
#define bitmap_from_arr32(bitmap, buf, nbits)			\
	bitmap_copy_clear_tail((unsigned long *) (bitmap),	\
			(const unsigned long *) (buf), (nbits))
#define bitmap_to_arr32(buf, bitmap, nbits)			\
	bitmap_copy_clear_tail((unsigned long *) (buf),		\
			(const unsigned long *) (bitmap), (nbits))
#endif

/*
 * On 64-bit systems bitmaps are represented as u64 arrays internally. So,
 * the conversion is not needed when copying data from/to arrays of u64.
 */
#if BITS_PER_LONG == 32
void bitmap_from_arr64(unsigned long *bitmap, const u64 *buf, unsigned int nbits);
void bitmap_to_arr64(u64 *buf, const unsigned long *bitmap, unsigned int nbits);
#else
#define bitmap_from_arr64(bitmap, buf, nbits)			\
	bitmap_copy_clear_tail((unsigned long *)(bitmap), (const unsigned long *)(buf), (nbits))
#define bitmap_to_arr64(buf, bitmap, nbits)			\
	bitmap_copy_clear_tail((unsigned long *)(buf), (const unsigned long *)(bitmap), (nbits))
#endif

static inline bool bitmap_and(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return (*dst = *src1 & *src2 & BITMAP_LAST_WORD_MASK(nbits)) != 0;
	return __bitmap_and(dst, src1, src2, nbits);
}

static inline void bitmap_or(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = *src1 | *src2;
	else
		__bitmap_or(dst, src1, src2, nbits);
}

static inline void bitmap_xor(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = *src1 ^ *src2;
	else
		__bitmap_xor(dst, src1, src2, nbits);
}

static inline bool bitmap_andnot(unsigned long *dst, const unsigned long *src1,
			const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return (*dst = *src1 & ~(*src2) & BITMAP_LAST_WORD_MASK(nbits)) != 0;
	return __bitmap_andnot(dst, src1, src2, nbits);
}

static inline void bitmap_complement(unsigned long *dst, const unsigned long *src,
			unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = ~(*src);
	else
		__bitmap_complement(dst, src, nbits);
}

#ifdef __LITTLE_ENDIAN
#define BITMAP_MEM_ALIGNMENT 8
#else
#define BITMAP_MEM_ALIGNMENT (8 * sizeof(unsigned long))
#endif
#define BITMAP_MEM_MASK (BITMAP_MEM_ALIGNMENT - 1)

static inline bool bitmap_equal(const unsigned long *src1,
				const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return !((*src1 ^ *src2) & BITMAP_LAST_WORD_MASK(nbits));
	if (__builtin_constant_p(nbits & BITMAP_MEM_MASK) &&
	    IS_ALIGNED(nbits, BITMAP_MEM_ALIGNMENT))
		return !memcmp(src1, src2, nbits / 8);
	return __bitmap_equal(src1, src2, nbits);
}

/**
 * bitmap_or_equal - Check whether the or of two bitmaps is equal to a third
 * @src1:	Pointer to bitmap 1
 * @src2:	Pointer to bitmap 2 will be or'ed with bitmap 1
 * @src3:	Pointer to bitmap 3. Compare to the result of *@src1 | *@src2
 * @nbits:	number of bits in each of these bitmaps
 *
 * Returns: True if (*@src1 | *@src2) == *@src3, false otherwise
 */
static inline bool bitmap_or_equal(const unsigned long *src1,
				   const unsigned long *src2,
				   const unsigned long *src3,
				   unsigned int nbits)
{
	if (!small_const_nbits(nbits))
		return __bitmap_or_equal(src1, src2, src3, nbits);

	return !(((*src1 | *src2) ^ *src3) & BITMAP_LAST_WORD_MASK(nbits));
}

static inline bool bitmap_intersects(const unsigned long *src1,
				     const unsigned long *src2,
				     unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return ((*src1 & *src2) & BITMAP_LAST_WORD_MASK(nbits)) != 0;
	else
		return __bitmap_intersects(src1, src2, nbits);
}

static inline bool bitmap_subset(const unsigned long *src1,
				 const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return ! ((*src1 & ~(*src2)) & BITMAP_LAST_WORD_MASK(nbits));
	else
		return __bitmap_subset(src1, src2, nbits);
}

static inline bool bitmap_empty(const unsigned long *src, unsigned nbits)
{
	if (small_const_nbits(nbits))
		return ! (*src & BITMAP_LAST_WORD_MASK(nbits));

	return find_first_bit(src, nbits) == nbits;
}

static inline bool bitmap_full(const unsigned long *src, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return ! (~(*src) & BITMAP_LAST_WORD_MASK(nbits));

	return find_first_zero_bit(src, nbits) == nbits;
}

static __always_inline
unsigned int bitmap_weight(const unsigned long *src, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return hweight_long(*src & BITMAP_LAST_WORD_MASK(nbits));
	return __bitmap_weight(src, nbits);
}

static __always_inline
unsigned long bitmap_weight_and(const unsigned long *src1,
				const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return hweight_long(*src1 & *src2 & BITMAP_LAST_WORD_MASK(nbits));
	return __bitmap_weight_and(src1, src2, nbits);
}

static __always_inline
unsigned long bitmap_weight_andnot(const unsigned long *src1,
				   const unsigned long *src2, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		return hweight_long(*src1 & ~(*src2) & BITMAP_LAST_WORD_MASK(nbits));
	return __bitmap_weight_andnot(src1, src2, nbits);
}

static __always_inline void bitmap_set(unsigned long *map, unsigned int start,
		unsigned int nbits)
{
	if (__builtin_constant_p(nbits) && nbits == 1)
		__set_bit(start, map);
	else if (small_const_nbits(start + nbits))
		*map |= GENMASK(start + nbits - 1, start);
	else if (__builtin_constant_p(start & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(start, BITMAP_MEM_ALIGNMENT) &&
		 __builtin_constant_p(nbits & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(nbits, BITMAP_MEM_ALIGNMENT))
		memset((char *)map + start / 8, 0xff, nbits / 8);
	else
		__bitmap_set(map, start, nbits);
}

static __always_inline void bitmap_clear(unsigned long *map, unsigned int start,
		unsigned int nbits)
{
	if (__builtin_constant_p(nbits) && nbits == 1)
		__clear_bit(start, map);
	else if (small_const_nbits(start + nbits))
		*map &= ~GENMASK(start + nbits - 1, start);
	else if (__builtin_constant_p(start & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(start, BITMAP_MEM_ALIGNMENT) &&
		 __builtin_constant_p(nbits & BITMAP_MEM_MASK) &&
		 IS_ALIGNED(nbits, BITMAP_MEM_ALIGNMENT))
		memset((char *)map + start / 8, 0, nbits / 8);
	else
		__bitmap_clear(map, start, nbits);
}

static inline void bitmap_shift_right(unsigned long *dst, const unsigned long *src,
				unsigned int shift, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = (*src & BITMAP_LAST_WORD_MASK(nbits)) >> shift;
	else
		__bitmap_shift_right(dst, src, shift, nbits);
}

static inline void bitmap_shift_left(unsigned long *dst, const unsigned long *src,
				unsigned int shift, unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = (*src << shift) & BITMAP_LAST_WORD_MASK(nbits);
	else
		__bitmap_shift_left(dst, src, shift, nbits);
}

static inline void bitmap_replace(unsigned long *dst,
				  const unsigned long *old,
				  const unsigned long *new,
				  const unsigned long *mask,
				  unsigned int nbits)
{
	if (small_const_nbits(nbits))
		*dst = (*old & ~(*mask)) | (*new & *mask);
	else
		__bitmap_replace(dst, old, new, mask, nbits);
}

/**
 * bitmap_scatter - Scatter a bitmap according to the given mask
 * @dst: scattered bitmap
 * @src: gathered bitmap
 * @mask: mask representing bits to assign to in the scattered bitmap
 * @nbits: number of bits in each of these bitmaps
 *
 * Scatters bitmap with sequential bits according to the given @mask.
 *
 * Example:
 * If @src bitmap = 0x005a, with @mask = 0x1313, @dst will be 0x0302.
 *
 * Or in binary form
 * @src			@mask			@dst
 * 0000000001011010	0001001100010011	0000001100000010
 *
 * (Bits 0, 1, 2, 3, 4, 5 are copied to the bits 0, 1, 4, 8, 9, 12)
 *
 * A more 'visual' description of the operation::
 *
 *	src:  0000000001011010
 *	                ||||||
 *	         +------+|||||
 *	         |  +----+||||
 *	         |  |+----+|||
 *	         |  ||   +-+||
 *	         |  ||   |  ||
 *	mask: ...v..vv...v..vv
 *	      ...0..11...0..10
 *	dst:  0000001100000010
 *
 * A relationship exists between bitmap_scatter() and bitmap_gather().
 * bitmap_gather() can be seen as the 'reverse' bitmap_scatter() operation.
 * See bitmap_scatter() for details related to this relationship.
 */
static inline void bitmap_scatter(unsigned long *dst, const unsigned long *src,
				  const unsigned long *mask, unsigned int nbits)
{
	unsigned int n = 0;
	unsigned int bit;

	bitmap_zero(dst, nbits);

	for_each_set_bit(bit, mask, nbits)
		__assign_bit(bit, dst, test_bit(n++, src));
}

/**
 * bitmap_gather - Gather a bitmap according to given mask
 * @dst: gathered bitmap
 * @src: scattered bitmap
 * @mask: mask representing bits to extract from in the scattered bitmap
 * @nbits: number of bits in each of these bitmaps
 *
 * Gathers bitmap with sparse bits according to the given @mask.
 *
 * Example:
 * If @src bitmap = 0x0302, with @mask = 0x1313, @dst will be 0x001a.
 *
 * Or in binary form
 * @src			@mask			@dst
 * 0000001100000010	0001001100010011	0000000000011010
 *
 * (Bits 0, 1, 4, 8, 9, 12 are copied to the bits 0, 1, 2, 3, 4, 5)
 *
 * A more 'visual' description of the operation::
 *
 *	mask: ...v..vv...v..vv
 *	src:  0000001100000010
 *	         ^  ^^   ^   0
 *	         |  ||   |  10
 *	         |  ||   > 010
 *	         |  |+--> 1010
 *	         |  +--> 11010
 *	         +----> 011010
 *	dst:  0000000000011010
 *
 * A relationship exists between bitmap_gather() and bitmap_scatter(). See
 * bitmap_scatter() for the bitmap scatter detailed operations.
 * Suppose scattered computed using bitmap_scatter(scattered, src, mask, n).
 * The operation bitmap_gather(result, scattered, mask, n) leads to a result
 * equal or equivalent to src.
 *
 * The result can be 'equivalent' because bitmap_scatter() and bitmap_gather()
 * are not bijective.
 * The result and src values are equivalent in that sense that a call to
 * bitmap_scatter(res, src, mask, n) and a call to
 * bitmap_scatter(res, result, mask, n) will lead to the same res value.
 */
static inline void bitmap_gather(unsigned long *dst, const unsigned long *src,
				 const unsigned long *mask, unsigned int nbits)
{
	unsigned int n = 0;
	unsigned int bit;

	bitmap_zero(dst, nbits);

	for_each_set_bit(bit, mask, nbits)
		__assign_bit(n++, dst, test_bit(bit, src));
}

static inline void bitmap_next_set_region(unsigned long *bitmap,
					  unsigned int *rs, unsigned int *re,
					  unsigned int end)
{
	*rs = find_next_bit(bitmap, end, *rs);
	*re = find_next_zero_bit(bitmap, end, *rs + 1);
}

/**
 * bitmap_release_region - release allocated bitmap region
 *	@bitmap: array of unsigned longs corresponding to the bitmap
 *	@pos: beginning of bit region to release
 *	@order: region size (log base 2 of number of bits) to release
 *
 * This is the complement to __bitmap_find_free_region() and releases
 * the found region (by clearing it in the bitmap).
 */
static inline void bitmap_release_region(unsigned long *bitmap, unsigned int pos, int order)
{
	bitmap_clear(bitmap, pos, BIT(order));
}

/**
 * bitmap_allocate_region - allocate bitmap region
 *	@bitmap: array of unsigned longs corresponding to the bitmap
 *	@pos: beginning of bit region to allocate
 *	@order: region size (log base 2 of number of bits) to allocate
 *
 * Allocate (set bits in) a specified region of a bitmap.
 *
 * Returns: 0 on success, or %-EBUSY if specified region wasn't
 * free (not all bits were zero).
 */
static inline int bitmap_allocate_region(unsigned long *bitmap, unsigned int pos, int order)
{
	unsigned int len = BIT(order);

	if (find_next_bit(bitmap, pos + len, pos) < pos + len)
		return -EBUSY;
	bitmap_set(bitmap, pos, len);
	return 0;
}

/**
 * bitmap_find_free_region - find a contiguous aligned mem region
 *	@bitmap: array of unsigned longs corresponding to the bitmap
 *	@bits: number of bits in the bitmap
 *	@order: region size (log base 2 of number of bits) to find
 *
 * Find a region of free (zero) bits in a @bitmap of @bits bits and
 * allocate them (set them to one).  Only consider regions of length
 * a power (@order) of two, aligned to that power of two, which
 * makes the search algorithm much faster.
 *
 * Returns: the bit offset in bitmap of the allocated region,
 * or -errno on failure.
 */
static inline int bitmap_find_free_region(unsigned long *bitmap, unsigned int bits, int order)
{
	unsigned int pos, end;		/* scans bitmap by regions of size order */

	for (pos = 0; (end = pos + BIT(order)) <= bits; pos = end) {
		if (!bitmap_allocate_region(bitmap, pos, order))
			return pos;
	}
	return -ENOMEM;
}

/**
 * BITMAP_FROM_U64() - Represent u64 value in the format suitable for bitmap.
 * @n: u64 value
 *
 * Linux bitmaps are internally arrays of unsigned longs, i.e. 32-bit
 * integers in 32-bit environment, and 64-bit integers in 64-bit one.
 *
 * There are four combinations of endianness and length of the word in linux
 * ABIs: LE64, BE64, LE32 and BE32.
 *
 * On 64-bit kernels 64-bit LE and BE numbers are naturally ordered in
 * bitmaps and therefore don't require any special handling.
 *
 * On 32-bit kernels 32-bit LE ABI orders lo word of 64-bit number in memory
 * prior to hi, and 32-bit BE orders hi word prior to lo. The bitmap on the
 * other hand is represented as an array of 32-bit words and the position of
 * bit N may therefore be calculated as: word #(N/32) and bit #(N%32) in that
 * word.  For example, bit #42 is located at 10th position of 2nd word.
 * It matches 32-bit LE ABI, and we can simply let the compiler store 64-bit
 * values in memory as it usually does. But for BE we need to swap hi and lo
 * words manually.
 *
 * With all that, the macro BITMAP_FROM_U64() does explicit reordering of hi and
 * lo parts of u64.  For LE32 it does nothing, and for BE environment it swaps
 * hi and lo words, as is expected by bitmap.
 */
#if __BITS_PER_LONG == 64
#define BITMAP_FROM_U64(n) (n)
#else
#define BITMAP_FROM_U64(n) ((unsigned long) ((u64)(n) & ULONG_MAX)), \
				((unsigned long) ((u64)(n) >> 32))
#endif

/**
 * bitmap_from_u64 - Check and swap words within u64.
 *  @mask: source bitmap
 *  @dst:  destination bitmap
 *
 * In 32-bit Big Endian kernel, when using ``(u32 *)(&val)[*]``
 * to read u64 mask, we will get the wrong word.
 * That is ``(u32 *)(&val)[0]`` gets the upper 32 bits,
 * but we expect the lower 32-bits of u64.
 */
static inline void bitmap_from_u64(unsigned long *dst, u64 mask)
{
	bitmap_from_arr64(dst, &mask, 64);
}

/**
 * bitmap_read - read a value of n-bits from the memory region
 * @map: address to the bitmap memory region
 * @start: bit offset of the n-bit value
 * @nbits: size of value in bits, nonzero, up to BITS_PER_LONG
 *
 * Returns: value of @nbits bits located at the @start bit offset within the
 * @map memory region. For @nbits = 0 and @nbits > BITS_PER_LONG the return
 * value is undefined.
 */
static inline unsigned long bitmap_read(const unsigned long *map,
					unsigned long start,
					unsigned long nbits)
{
	size_t index = BIT_WORD(start);
	unsigned long offset = start % BITS_PER_LONG;
	unsigned long space = BITS_PER_LONG - offset;
	unsigned long value_low, value_high;

	if (unlikely(!nbits || nbits > BITS_PER_LONG))
		return 0;

	if (space >= nbits)
		return (map[index] >> offset) & BITMAP_LAST_WORD_MASK(nbits);

	value_low = map[index] & BITMAP_FIRST_WORD_MASK(start);
	value_high = map[index + 1] & BITMAP_LAST_WORD_MASK(start + nbits);
	return (value_low >> offset) | (value_high << space);
}

/**
 * bitmap_write - write n-bit value within a memory region
 * @map: address to the bitmap memory region
 * @value: value to write, clamped to nbits
 * @start: bit offset of the n-bit value
 * @nbits: size of value in bits, nonzero, up to BITS_PER_LONG.
 *
 * bitmap_write() behaves as-if implemented as @nbits calls of __assign_bit(),
 * i.e. bits beyond @nbits are ignored:
 *
 *   for (bit = 0; bit < nbits; bit++)
 *           __assign_bit(start + bit, bitmap, val & BIT(bit));
 *
 * For @nbits == 0 and @nbits > BITS_PER_LONG no writes are performed.
 */
static inline void bitmap_write(unsigned long *map, unsigned long value,
				unsigned long start, unsigned long nbits)
{
	size_t index;
	unsigned long offset;
	unsigned long space;
	unsigned long mask;
	bool fit;

	if (unlikely(!nbits || nbits > BITS_PER_LONG))
		return;

	mask = BITMAP_LAST_WORD_MASK(nbits);
	value &= mask;
	offset = start % BITS_PER_LONG;
	space = BITS_PER_LONG - offset;
	fit = space >= nbits;
	index = BIT_WORD(start);

	map[index] &= (fit ? (~(mask << offset)) : ~BITMAP_FIRST_WORD_MASK(start));
	map[index] |= value << offset;
	if (fit)
		return;

	map[index + 1] &= BITMAP_FIRST_WORD_MASK(start + nbits);
	map[index + 1] |= (value >> space);
}

#define bitmap_get_value8(map, start)			\
	bitmap_read(map, start, BITS_PER_BYTE)
#define bitmap_set_value8(map, value, start)		\
	bitmap_write(map, value, start, BITS_PER_BYTE)

#endif /* __ASSEMBLY__ */

#endif /* __LINUX_BITMAP_H */
