/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_FIND_H_
#define __LINUX_FIND_H_

#ifndef __LINUX_BITMAP_H
#error only <linux/bitmap.h> can be included directly
#endif

#include <linux/bitops.h>

extern unsigned long _find_next_bit(const unsigned long *addr1,
		const unsigned long *addr2, unsigned long nbits,
		unsigned long start, unsigned long invert, unsigned long le);
extern unsigned long _find_first_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_first_and_bit(const unsigned long *addr1,
					 const unsigned long *addr2, unsigned long size);
extern unsigned long _find_first_zero_bit(const unsigned long *addr, unsigned long size);
extern unsigned long _find_last_bit(const unsigned long *addr, unsigned long size);

#ifndef find_next_bit
/**
 * find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
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

	return _find_next_bit(addr, NULL, size, offset, 0UL, 0);
}
#endif

#ifndef find_next_and_bit
/**
 * find_next_and_bit - find the next set bit in both memory regions
 * @addr1: The first address to base the search on
 * @addr2: The second address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
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

	return _find_next_bit(addr1, addr2, size, offset, 0UL, 0);
}
#endif

#ifndef find_next_zero_bit
/**
 * find_next_zero_bit - find the next cleared bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
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

	return _find_next_bit(addr, NULL, size, offset, ~0UL, 0);
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

	return _find_next_bit(addr, NULL, size, offset, ~0UL, 1);
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

	return _find_next_bit(addr, NULL, size, offset, 0UL, 1);
}
#endif

#ifndef find_first_zero_bit_le
#define find_first_zero_bit_le(addr, size) \
	find_next_zero_bit_le((addr), (size), 0)
#endif

#else
#error "Please fix <asm/byteorder.h>"
#endif

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), 0);		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

/* same as for_each_set_bit() but use bit as value to start with */
#define for_each_set_bit_from(bit, addr, size) \
	for ((bit) = find_next_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define for_each_clear_bit(bit, addr, size) \
	for ((bit) = find_next_zero_bit((addr), (size), 0);	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/* same as for_each_clear_bit() but use bit as value to start with */
#define for_each_clear_bit_from(bit, addr, size) \
	for ((bit) = find_next_zero_bit((addr), (size), (bit));	\
	     (bit) < (size);					\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

/**
 * for_each_set_bitrange - iterate over all set bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first set bit)
 * @e: bit offset of end of current bitrange (first unset bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_set_bitrange(b, e, addr, size)			\
	for ((b) = find_next_bit((addr), (size), 0),		\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1))

/**
 * for_each_set_bitrange_from - iterate over all set bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first set bit); must be initialized
 * @e: bit offset of end of current bitrange (first unset bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_set_bitrange_from(b, e, addr, size)		\
	for ((b) = find_next_bit((addr), (size), (b)),		\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_zero_bit((addr), (size), (b) + 1))

/**
 * for_each_clear_bitrange - iterate over all unset bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first unset bit)
 * @e: bit offset of end of current bitrange (first set bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_clear_bitrange(b, e, addr, size)		\
	for ((b) = find_next_zero_bit((addr), (size), 0),	\
	     (e) = find_next_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_zero_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_bit((addr), (size), (b) + 1))

/**
 * for_each_clear_bitrange_from - iterate over all unset bit ranges [b; e)
 * @b: bit offset of start of current bitrange (first set bit); must be initialized
 * @e: bit offset of end of current bitrange (first unset bit)
 * @addr: bitmap address to base the search on
 * @size: bitmap size in number of bits
 */
#define for_each_clear_bitrange_from(b, e, addr, size)		\
	for ((b) = find_next_zero_bit((addr), (size), (b)),	\
	     (e) = find_next_bit((addr), (size), (b) + 1);	\
	     (b) < (size);					\
	     (b) = find_next_zero_bit((addr), (size), (e) + 1),	\
	     (e) = find_next_bit((addr), (size), (b) + 1))

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
