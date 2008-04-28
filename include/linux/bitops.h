#ifndef _LINUX_BITOPS_H
#define _LINUX_BITOPS_H
#include <asm/types.h>

#ifdef	__KERNEL__
#define BIT(nr)			(1UL << (nr))
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_LONG)
#define BITS_PER_BYTE		8
#endif

/*
 * Include this here because some architectures need generic_ffs/fls in
 * scope
 */
#include <asm/bitops.h>

#define for_each_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size)); \
	     (bit) < (size); \
	     (bit) = find_next_bit((addr), (size), (bit) + 1))


static __inline__ int get_bitmask_order(unsigned int count)
{
	int order;
	
	order = fls(count);
	return order;	/* We could be slightly more clever with -1 here... */
}

static __inline__ int get_count_order(unsigned int count)
{
	int order;
	
	order = fls(count) - 1;
	if (count & (count - 1))
		order++;
	return order;
}

static inline unsigned long hweight_long(unsigned long w)
{
	return sizeof(w) == 4 ? hweight32(w) : hweight64(w);
}

/**
 * rol32 - rotate a 32-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u32 rol32(__u32 word, unsigned int shift)
{
	return (word << shift) | (word >> (32 - shift));
}

/**
 * ror32 - rotate a 32-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u32 ror32(__u32 word, unsigned int shift)
{
	return (word >> shift) | (word << (32 - shift));
}

/**
 * rol16 - rotate a 16-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u16 rol16(__u16 word, unsigned int shift)
{
	return (word << shift) | (word >> (16 - shift));
}

/**
 * ror16 - rotate a 16-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u16 ror16(__u16 word, unsigned int shift)
{
	return (word >> shift) | (word << (16 - shift));
}

/**
 * rol8 - rotate an 8-bit value left
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u8 rol8(__u8 word, unsigned int shift)
{
	return (word << shift) | (word >> (8 - shift));
}

/**
 * ror8 - rotate an 8-bit value right
 * @word: value to rotate
 * @shift: bits to roll
 */
static inline __u8 ror8(__u8 word, unsigned int shift)
{
	return (word >> shift) | (word << (8 - shift));
}

static inline unsigned fls_long(unsigned long l)
{
	if (sizeof(l) == 4)
		return fls(l);
	return fls64(l);
}

#ifdef __KERNEL__
#ifdef CONFIG_GENERIC_FIND_FIRST_BIT
extern unsigned long __find_first_bit(const unsigned long *addr,
		unsigned long size);

/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the first set bit.
 */
static __always_inline unsigned long
find_first_bit(const unsigned long *addr, unsigned long size)
{
	/* Avoid a function call if the bitmap size is a constant */
	/* and not bigger than BITS_PER_LONG. */

	/* insert a sentinel so that __ffs returns size if there */
	/* are no set bits in the bitmap */
	if (__builtin_constant_p(size) && (size < BITS_PER_LONG))
		return __ffs((*addr) | (1ul << size));

	/* the result of __ffs(0) is undefined, so it needs to be */
	/* handled separately */
	if (__builtin_constant_p(size) && (size == BITS_PER_LONG))
		return ((*addr) == 0) ? BITS_PER_LONG : __ffs(*addr);

	/* size is not constant or too big */
	return __find_first_bit(addr, size);
}

extern unsigned long __find_first_zero_bit(const unsigned long *addr,
		unsigned long size);

/**
 * find_first_zero_bit - find the first cleared bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the first cleared bit.
 */
static __always_inline unsigned long
find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	/* Avoid a function call if the bitmap size is a constant */
	/* and not bigger than BITS_PER_LONG. */

	/* insert a sentinel so that __ffs returns size if there */
	/* are no set bits in the bitmap */
	if (__builtin_constant_p(size) && (size < BITS_PER_LONG)) {
		return __ffs(~(*addr) | (1ul << size));
	}

	/* the result of __ffs(0) is undefined, so it needs to be */
	/* handled separately */
	if (__builtin_constant_p(size) && (size == BITS_PER_LONG))
		return (~(*addr) == 0) ? BITS_PER_LONG : __ffs(~(*addr));

	/* size is not constant or too big */
	return __find_first_zero_bit(addr, size);
}
#endif /* CONFIG_GENERIC_FIND_FIRST_BIT */

#ifdef CONFIG_GENERIC_FIND_NEXT_BIT
extern unsigned long __find_next_bit(const unsigned long *addr,
		unsigned long size, unsigned long offset);

/**
 * find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 */
static __always_inline unsigned long
find_next_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset)
{
	unsigned long value;

	/* Avoid a function call if the bitmap size is a constant */
	/* and not bigger than BITS_PER_LONG. */

	/* insert a sentinel so that __ffs returns size if there */
	/* are no set bits in the bitmap */
	if (__builtin_constant_p(size) && (size < BITS_PER_LONG)) {
		value = (*addr) & ((~0ul) << offset);
		value |= (1ul << size);
		return __ffs(value);
	}

	/* the result of __ffs(0) is undefined, so it needs to be */
	/* handled separately */
	if (__builtin_constant_p(size) && (size == BITS_PER_LONG)) {
		value = (*addr) & ((~0ul) << offset);
		return (value == 0) ? BITS_PER_LONG : __ffs(value);
	}

	/* size is not constant or too big */
	return __find_next_bit(addr, size, offset);
}

extern unsigned long __find_next_zero_bit(const unsigned long *addr,
		unsigned long size, unsigned long offset);

/**
 * find_next_zero_bit - find the next cleared bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The bitmap size in bits
 */
static __always_inline unsigned long
find_next_zero_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset)
{
	unsigned long value;

	/* Avoid a function call if the bitmap size is a constant */
	/* and not bigger than BITS_PER_LONG. */

	/* insert a sentinel so that __ffs returns size if there */
	/* are no set bits in the bitmap */
	if (__builtin_constant_p(size) && (size < BITS_PER_LONG)) {
		value = (~(*addr)) & ((~0ul) << offset);
		value |= (1ul << size);
		return __ffs(value);
	}

	/* the result of __ffs(0) is undefined, so it needs to be */
	/* handled separately */
	if (__builtin_constant_p(size) && (size == BITS_PER_LONG)) {
		value = (~(*addr)) & ((~0ul) << offset);
		return (value == 0) ? BITS_PER_LONG : __ffs(value);
	}

	/* size is not constant or too big */
	return __find_next_zero_bit(addr, size, offset);
}
#endif /* CONFIG_GENERIC_FIND_NEXT_BIT */
#endif /* __KERNEL__ */
#endif
