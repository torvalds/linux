/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_FIND_H_
#define __LINUX_FIND_H_

#ifndef __LINUX_BITMAP_H
#error only <linux/bitmap.h> can be included directly
#endif

#include <linux/bitops.h>

unsigned long _find_next_bit(const unsigned long *addr1, unsigned long nbits,
				unsigned long start);
unsigned long _find_next_and_bit(const unsigned long *addr1, const unsigned long *addr2,
					unsigned long nbits, unsigned long start);
unsigned long _find_next_andnot_bit(const unsigned long *addr1, const unsigned long *addr2,
					unsigned long nbits, unsigned long start);
unsigned long _find_next_or_bit(const unsigned long *addr1, const unsigned long *addr2,
					unsigned long nbits, unsigned long start);
unsigned long _find_next_zero_bit(const unsigned long *addr, unsigned long nbits,
					 unsigned long start);
extern unsigned long _find_first_bit(const unsigned long *addr, unsigned long size);
unsigned long __find_nth_bit(const unsigned long *addr, unsigned long size, unsigned long n);
unsigned long __find_nth_and_bit(const unsigned long *addr1, const unsigned long *addr2,
				unsigned long size, unsigned long n);
unsigned long __find_nth_andnot_bit(const unsigned long *addr1, const unsigned long *addr2,
					unsigned long size, unsigned long n);
unsigned long __find_nth_and_andnot_bit(const unsigned long *addr1, const unsigned long *addr2,
					const unsigned long *addr3, unsigned long size,
					unsigned long n);
extern unsigned long _find_first_and_bit(const unsigned long *addr1,
					 const unsigned long *addr2, unsigned long size);
unsigned long _find_first_and_and_bit(const unsigned long *addr1, const unsigned long *addr2,
				      const unsigned long *addr3, unsigned long size);
extern unsigned long _find_first_zero_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_last_bit(const unsigned long *addr, unsigned long size);

#ifdef __BIG_ENDIAN
unsigned long _find_first_zero_bit_le(const unsigned long *addr, unsigned long size);
unsigned long _find_next_zero_bit_le(const  unsigned long *addr, unsigned
					long size, unsigned long offset);
unsigned long _find_next_bit_le(const unsigned long *addr, unsigned
				long size, unsigned long offset);
#endif

#ifndef find_next_bit
/**
 * find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number for the next set bit
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *addr & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_bit(addr, size, offset);
}
#endif

#ifndef find_next_and_bit
/**
 * find_next_and_bit - find the next set bit in both memory regions
 * @addr1: The first address to base the search on
 * @addr2: The second address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number for the next set bit
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_next_and_bit(const unsigned long *addr1,
		const unsigned long *addr2, unsigned long size,
		unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *addr1 & *addr2 & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_and_bit(addr1, addr2, size, offset);
}
#endif

#ifndef find_next_andnot_bit
/**
 * find_next_andnot_bit - find the next set bit in *addr1 excluding all the bits
 *                        in *addr2
 * @addr1: The first address to base the search on
 * @addr2: The second address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number for the next set bit
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_next_andnot_bit(const unsigned long *addr1,
		const unsigned long *addr2, unsigned long size,
		unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *addr1 & ~*addr2 & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_andnot_bit(addr1, addr2, size, offset);
}
#endif

#ifndef find_next_or_bit
/**
 * find_next_or_bit - find the next set bit in either memory regions
 * @addr1: The first address to base the search on
 * @addr2: The second address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number for the next set bit
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_next_or_bit(const unsigned long *addr1,
		const unsigned long *addr2, unsigned long size,
		unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = (*addr1 | *addr2) & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_or_bit(addr1, addr2, size, offset);
}
#endif

#ifndef find_next_zero_bit
/**
 * find_next_zero_bit - find the next cleared bit in a memory region
 * @addr: The address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number of the next zero bit
 * If no bits are zero, returns @size.
 */
static inline
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
				 unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val;

		if (unlikely(offset >= size))
			return size;

		val = *addr | ~GENMASK(size - 1, offset);
		return val == ~0UL ? size : ffz(val);
	}

	return _find_next_zero_bit(addr, size, offset);
}
#endif

#ifndef find_first_bit
/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum number of bits to search
 *
 * Returns the bit number of the first set bit.
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_first_bit(const unsigned long *addr, unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr & GENMASK(size - 1, 0);

		return val ? __ffs(val) : size;
	}

	return _find_first_bit(addr, size);
}
#endif

/**
 * find_nth_bit - find N'th set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum number of bits to search
 * @n: The number of set bit, which position is needed, counting from 0
 *
 * The following is semantically equivalent:
 *	 idx = find_nth_bit(addr, size, 0);
 *	 idx = find_first_bit(addr, size);
 *
 * Returns the bit number of the N'th set bit.
 * If no such, returns >= @size.
 */
static inline
unsigned long find_nth_bit(const unsigned long *addr, unsigned long size, unsigned long n)
{
	if (n >= size)
		return size;

	if (small_const_nbits(size)) {
		unsigned long val =  *addr & GENMASK(size - 1, 0);

		return val ? fns(val, n) : size;
	}

	return __find_nth_bit(addr, size, n);
}

/**
 * find_nth_and_bit - find N'th set bit in 2 memory regions
 * @addr1: The 1st address to start the search at
 * @addr2: The 2nd address to start the search at
 * @size: The maximum number of bits to search
 * @n: The number of set bit, which position is needed, counting from 0
 *
 * Returns the bit number of the N'th set bit.
 * If no such, returns @size.
 */
static inline
unsigned long find_nth_and_bit(const unsigned long *addr1, const unsigned long *addr2,
				unsigned long size, unsigned long n)
{
	if (n >= size)
		return size;

	if (small_const_nbits(size)) {
		unsigned long val =  *addr1 & *addr2 & GENMASK(size - 1, 0);

		return val ? fns(val, n) : size;
	}

	return __find_nth_and_bit(addr1, addr2, size, n);
}

/**
 * find_nth_andnot_bit - find N'th set bit in 2 memory regions,
 *			 flipping bits in 2nd region
 * @addr1: The 1st address to start the search at
 * @addr2: The 2nd address to start the search at
 * @size: The maximum number of bits to search
 * @n: The number of set bit, which position is needed, counting from 0
 *
 * Returns the bit number of the N'th set bit.
 * If no such, returns @size.
 */
static inline
unsigned long find_nth_andnot_bit(const unsigned long *addr1, const unsigned long *addr2,
				unsigned long size, unsigned long n)
{
	if (n >= size)
		return size;

	if (small_const_nbits(size)) {
		unsigned long val =  *addr1 & (~*addr2) & GENMASK(size - 1, 0);

		return val ? fns(val, n) : size;
	}

	return __find_nth_andnot_bit(addr1, addr2, size, n);
}

/**
 * find_nth_and_andnot_bit - find N'th set bit in 2 memory regions,
 *			     excluding those set in 3rd region
 * @addr1: The 1st address to start the search at
 * @addr2: The 2nd address to start the search at
 * @addr3: The 3rd address to start the search at
 * @size: The maximum number of bits to search
 * @n: The number of set bit, which position is needed, counting from 0
 *
 * Returns the bit number of the N'th set bit.
 * If no such, returns @size.
 */
static __always_inline
unsigned long find_nth_and_andnot_bit(const unsigned long *addr1,
					const unsigned long *addr2,
					const unsigned long *addr3,
					unsigned long size, unsigned long n)
{
	if (n >= size)
		return size;

	if (small_const_nbits(size)) {
		unsigned long val =  *addr1 & *addr2 & (~*addr3) & GENMASK(size - 1, 0);

		return val ? fns(val, n) : size;
	}

	return __find_nth_and_andnot_bit(addr1, addr2, addr3, size, n);
}

#ifndef find_first_and_bit
/**
 * find_first_and_bit - find the first set bit in both memory regions
 * @addr1: The first address to base the search on
 * @addr2: The second address to base the search on
 * @size: The bitmap size in bits
 *
 * Returns the bit number for the next set bit
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_first_and_bit(const unsigned long *addr1,
				 const unsigned long *addr2,
				 unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr1 & *addr2 & GENMASK(size - 1, 0);

		return val ? __ffs(val) : size;
	}

	return _find_first_and_bit(addr1, addr2, size);
}
#endif

/**
 * find_first_and_and_bit - find the first set bit in 3 memory regions
 * @addr1: The first address to base the search on
 * @addr2: The second address to base the search on
 * @addr3: The third address to base the search on
 * @size: The bitmap size in bits
 *
 * Returns the bit number for the first set bit
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_first_and_and_bit(const unsigned long *addr1,
				     const unsigned long *addr2,
				     const unsigned long *addr3,
				     unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr1 & *addr2 & *addr3 & GENMASK(size - 1, 0);

		return val ? __ffs(val) : size;
	}

	return _find_first_and_and_bit(addr1, addr2, addr3, size);
}

#ifndef find_first_zero_bit
/**
 * find_first_zero_bit - find the first cleared bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum number of bits to search
 *
 * Returns the bit number of the first cleared bit.
 * If no bits are zero, returns @size.
 */
static inline
unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr | ~GENMASK(size - 1, 0);

		return val == ~0UL ? size : ffz(val);
	}

	return _find_first_zero_bit(addr, size);
}
#endif

#ifndef find_last_bit
/**
 * find_last_bit - find the last set bit in a memory region
 * @addr: The address to start the search at
 * @size: The number of bits to search
 *
 * Returns the bit number of the last set bit, or size.
 */
static inline
unsigned long find_last_bit(const unsigned long *addr, unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = *addr & GENMASK(size - 1, 0);

		return val ? __fls(val) : size;
	}

	return _find_last_bit(addr, size);
}
#endif

/**
 * find_next_and_bit_wrap - find the next set bit in both memory regions
 * @addr1: The first address to base the search on
 * @addr2: The second address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number for the next set bit, or first set bit up to @offset
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_next_and_bit_wrap(const unsigned long *addr1,
					const unsigned long *addr2,
					unsigned long size, unsigned long offset)
{
	unsigned long bit = find_next_and_bit(addr1, addr2, size, offset);

	if (bit < size || offset == 0)
		return bit;

	bit = find_first_and_bit(addr1, addr2, offset);
	return bit < offset ? bit : size;
}

/**
 * find_next_bit_wrap - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @size: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number for the next set bit, or first set bit up to @offset
 * If no bits are set, returns @size.
 */
static inline
unsigned long find_next_bit_wrap(const unsigned long *addr,
					unsigned long size, unsigned long offset)
{
	unsigned long bit = find_next_bit(addr, size, offset);

	if (bit < size || offset == 0)
		return bit;

	bit = find_first_bit(addr, offset);
	return bit < offset ? bit : size;
}

/*
 * Helper for for_each_set_bit_wrap(). Make sure you're doing right thing
 * before using it alone.
 */
static inline
unsigned long __for_each_wrap(const unsigned long *bitmap, unsigned long size,
				 unsigned long start, unsigned long n)
{
	unsigned long bit;

	/* If not wrapped around */
	if (n > start) {
		/* and have a bit, just return it. */
		bit = find_next_bit(bitmap, size, n);
		if (bit < size)
			return bit;

		/* Otherwise, wrap around and ... */
		n = 0;
	}

	/* Search the other part. */
	bit = find_next_bit(bitmap, start, n);
	return bit < start ? bit : size;
}

/**
 * find_next_clump8 - find next 8-bit clump with set bits in a memory region
 * @clump: location to store copy of found clump
 * @addr: address to base the search on
 * @size: bitmap size in number of bits
 * @offset: bit offset at which to start searching
 *
 * Returns the bit offset for the next set clump; the found clump value is
 * copied to the location pointed by @clump. If no bits are set, returns @size.
 */
extern unsigned long find_next_clump8(unsigned long *clump,
				      const unsigned long *addr,
				      unsigned long size, unsigned long offset);

#define find_first_clump8(clump, bits, size) \
	find_next_clump8((clump), (bits), (size), 0)

#if defined(__LITTLE_ENDIAN)

static inline unsigned long find_next_zero_bit_le(const void *addr,
		unsigned long size, unsigned long offset)
{
	return find_next_zero_bit(addr, size, offset);
}

static inline unsigned long find_next_bit_le(const void *addr,
		unsigned long size, unsigned long offset)
{
	return find_next_bit(addr, size, offset);
}

static inline unsigned long find_first_zero_bit_le(const void *addr,
		unsigned long size)
{
	return find_first_zero_bit(addr, size);
}

#elif defined(__BIG_ENDIAN)

#ifndef find_next_zero_bit_le
static inline
unsigned long find_next_zero_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val = *(const unsigned long *)addr;

		if (unlikely(offset >= size))
			return size;

		val = swab(val) | ~GENMASK(size - 1, offset);
		return val == ~0UL ? size : ffz(val);
	}

	return _find_next_zero_bit_le(addr, size, offset);
}
#endif

#ifndef find_first_zero_bit_le
static inline
unsigned long find_first_zero_bit_le(const void *addr, unsigned long size)
{
	if (small_const_nbits(size)) {
		unsigned long val = swab(*(const unsigned long *)addr) | ~GENMASK(size - 1, 0);

		return val == ~0UL ? size : ffz(val);
	}

	return _find_first_zero_bit_le(addr, size);
}
#endif

#ifndef find_next_bit_le
static inline
unsigned long find_next_bit_le(const void *addr, unsigned
		long size, unsigned long offset)
{
	if (small_const_nbits(size)) {
		unsigned long val = *(const unsigned long *)addr;

		if (unlikely(offset >= size))
			return size;

		val = swab(val) & GENMASK(size - 1, offset);
		return val ? __ffs(val) : size;
	}

	return _find_next_bit_le(addr, size, offset);
}
#endif

#else
#error "Please fix <asm/byteorder.h>"
#endif

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = 0; (bit) = find_next_bit((addr), (size), (bit)), (bit) < (size); (bit)++)

#define for_each_and_bit(bit, addr1, addr2, size) \
	for ((bit) = 0;									\
	     (bit) = find_next_and_bit((addr1), (addr2), (size), (bit)), (bit) < (size);\
	     (bit)++)

#define for_each_andnot_bit(bit, addr1, addr2, size) \
	for ((bit) = 0;									\
	     (bit) = find_next_andnot_bit((addr1), (addr2), (size), (bit)), (bit) < (size);\
	     (bit)++)

#define for_each_or_bit(bit, addr1, addr2, size) \
	for ((bit) = 0;									\
	     (bit) = find_next_or_bit((addr1), (addr2), (size), (bit)), (bit) < (size);\
	     (bit)++)

/* same as for_each_set_bit() but use bit as value to start with */
#define for_each_set_bit_from(bit, addr, size) \
	for (; (bit) = find_next_bit((addr), (size), (bit)), (bit) < (size); (bit)++)

#define for_each_clear_bit(bit, addr, size) \
	for ((bit) = 0;									\
	     (bit) = find_next_zero_bit((addr), (size), (bit)), (bit) < (size);		\
	     (bit)++)

/* same as for_each_clear_bit() but use bit as value to start with */
#define for_each_clear_bit_from(bit, addr, size) \
	for (; (bit) = find_next_zero_bit((addr), (size), (bit)), (bit) < (size); (bit)++)

/**
 * for_each_set_bitrange - iterate over all set bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first set bit)
 * @e: bit offset of end of current bitrange (first unset bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_set_bitrange(b, e, addr, size)			\
	for ((b) = 0;						\
	     (b) = find_next_bit((addr), (size), b),		\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1),	\
	     (b) < (size);					\
	     (b) = (e) + 1)

/**
 * for_each_set_bitrange_from - iterate over all set bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first set bit); must be initialized
 * @e: bit offset of end of current bitrange (first unset bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_set_bitrange_from(b, e, addr, size)		\
	for (;							\
	     (b) = find_next_bit((addr), (size), (b)),		\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1),	\
	     (b) < (size);					\
	     (b) = (e) + 1)

/**
 * for_each_clear_bitrange - iterate over all unset bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first unset bit)
 * @e: bit offset of end of current bitrange (first set bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_clear_bitrange(b, e, addr, size)		\
	for ((b) = 0;						\
	     (b) = find_next_zero_bit((addr), (size), (b)),	\
	     (e) = find_next_bit((addr), (size), (b) + 1),	\
	     (b) < (size);					\
	     (b) = (e) + 1)

/**
 * for_each_clear_bitrange_from - iterate over all unset bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first set bit); must be initialized
 * @e: bit offset of end of current bitrange (first unset bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_clear_bitrange_from(b, e, addr, size)		\
	for (;							\
	     (b) = find_next_zero_bit((addr), (size), (b)),	\
	     (e) = find_next_bit((addr), (size), (b) + 1),	\
	     (b) < (size);					\
	     (b) = (e) + 1)

/**
 * for_each_set_bit_wrap - iterate over all set bits starting from @start, and
 * wrapping around the end of bitmap.
 * @bit: offset for current iteration
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 * @start: Starting bit for bitmap traversing, wrapping around the bitmap end
 */
#define for_each_set_bit_wrap(bit, addr, size, start) \
	for ((bit) = find_next_bit_wrap((addr), (size), (start));		\
	     (bit) < (size);							\
	     (bit) = __for_each_wrap((addr), (size), (start), (bit) + 1))

/**
 * for_each_set_clump8 - iterate over bitmap for each 8-bit clump with set bits
 * @start: bit offset to start search and to store the current iteration offset
 * @clump: location to store copy of current 8-bit clump
 * @bits: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_set_clump8(start, clump, bits, size) \
	for ((start) = find_first_clump8(&(clump), (bits), (size)); \
	     (start) < (size); \
	     (start) = find_next_clump8(&(clump), (bits), (size), (start) + 8))

#endif /*__LINUX_FIND_H_ */
