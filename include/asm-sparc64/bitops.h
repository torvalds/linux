/* $Id: bitops.h,v 1.39 2002/01/30 01:40:00 davem Exp $
 * bitops.h: Bit string operations on the V9.
 *
 * Copyright 1996, 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC64_BITOPS_H
#define _SPARC64_BITOPS_H

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>

extern int test_and_set_bit(unsigned long nr, volatile unsigned long *addr);
extern int test_and_clear_bit(unsigned long nr, volatile unsigned long *addr);
extern int test_and_change_bit(unsigned long nr, volatile unsigned long *addr);
extern void set_bit(unsigned long nr, volatile unsigned long *addr);
extern void clear_bit(unsigned long nr, volatile unsigned long *addr);
extern void change_bit(unsigned long nr, volatile unsigned long *addr);

/* "non-atomic" versions... */

static inline void __set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long *m = ((unsigned long *)addr) + (nr >> 6);

	*m |= (1UL << (nr & 63));
}

static inline void __clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long *m = ((unsigned long *)addr) + (nr >> 6);

	*m &= ~(1UL << (nr & 63));
}

static inline void __change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long *m = ((unsigned long *)addr) + (nr >> 6);

	*m ^= (1UL << (nr & 63));
}

static inline int __test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long *m = ((unsigned long *)addr) + (nr >> 6);
	unsigned long old = *m;
	unsigned long mask = (1UL << (nr & 63));

	*m = (old | mask);
	return ((old & mask) != 0);
}

static inline int __test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long *m = ((unsigned long *)addr) + (nr >> 6);
	unsigned long old = *m;
	unsigned long mask = (1UL << (nr & 63));

	*m = (old & ~mask);
	return ((old & mask) != 0);
}

static inline int __test_and_change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long *m = ((unsigned long *)addr) + (nr >> 6);
	unsigned long old = *m;
	unsigned long mask = (1UL << (nr & 63));

	*m = (old ^ mask);
	return ((old & mask) != 0);
}

#ifdef CONFIG_SMP
#define smp_mb__before_clear_bit()	membar_storeload_loadload()
#define smp_mb__after_clear_bit()	membar_storeload_storestore()
#else
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()
#endif

static inline int test_bit(int nr, __const__ volatile unsigned long *addr)
{
	return (1UL & (addr[nr >> 6] >> (nr & 63))) != 0UL;
}

/* The easy/cheese version for now. */
static inline unsigned long ffz(unsigned long word)
{
	unsigned long result;

	result = 0;
	while(word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word)
{
	unsigned long result = 0;

	while (!(word & 1UL)) {
		result++;
		word >>= 1;
	}
	return result;
}

/*
 * fls: find last bit set.
 */

#define fls(x) generic_fls(x)

#ifdef __KERNEL__

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is cleared.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(((unsigned int)b[1])))
		return __ffs(b[1]) + 64;
	if (b[1] >> 32)
		return __ffs(b[1] >> 32) + 96;
	return __ffs(b[2]) + 128;
}

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */
static inline int ffs(int x)
{
	if (!x)
		return 0;
	return __ffs((unsigned long)x) + 1;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#ifdef ULTRA_HAS_POPULATION_COUNT

static inline unsigned int hweight64(unsigned long w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w));
	return res;
}

static inline unsigned int hweight32(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xffffffff));
	return res;
}

static inline unsigned int hweight16(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xffff));
	return res;
}

static inline unsigned int hweight8(unsigned int w)
{
	unsigned int res;

	__asm__ ("popc %1,%0" : "=r" (res) : "r" (w & 0xff));
	return res;
}

#else

#define hweight64(x) generic_hweight64(x)
#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#endif
#endif /* __KERNEL__ */

/**
 * find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 */
extern unsigned long find_next_bit(const unsigned long *, unsigned long,
					unsigned long);

/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit-number of the first set bit, not the number of the byte
 * containing a bit.
 */
#define find_first_bit(addr, size) \
	find_next_bit((addr), (size), 0)

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

extern unsigned long find_next_zero_bit(const unsigned long *,
					unsigned long, unsigned long);

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

#define test_and_set_le_bit(nr,addr)	\
	test_and_set_bit((nr) ^ 0x38, (addr))
#define test_and_clear_le_bit(nr,addr)	\
	test_and_clear_bit((nr) ^ 0x38, (addr))

static inline int test_le_bit(int nr, __const__ unsigned long * addr)
{
	int			mask;
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#define find_first_zero_le_bit(addr, size) \
        find_next_zero_le_bit((addr), (size), 0)

extern unsigned long find_next_zero_le_bit(unsigned long *, unsigned long, unsigned long);

#ifdef __KERNEL__

#define __set_le_bit(nr, addr) \
	__set_bit((nr) ^ 0x38, (addr))
#define __clear_le_bit(nr, addr) \
	__clear_bit((nr) ^ 0x38, (addr))
#define __test_and_clear_le_bit(nr, addr) \
	__test_and_clear_bit((nr) ^ 0x38, (addr))
#define __test_and_set_le_bit(nr, addr) \
	__test_and_set_bit((nr) ^ 0x38, (addr))

#define ext2_set_bit(nr,addr)	\
	__test_and_set_le_bit((nr),(unsigned long *)(addr))
#define ext2_set_bit_atomic(lock,nr,addr) \
	test_and_set_le_bit((nr),(unsigned long *)(addr))
#define ext2_clear_bit(nr,addr)	\
	__test_and_clear_le_bit((nr),(unsigned long *)(addr))
#define ext2_clear_bit_atomic(lock,nr,addr) \
	test_and_clear_le_bit((nr),(unsigned long *)(addr))
#define ext2_test_bit(nr,addr)	\
	test_le_bit((nr),(unsigned long *)(addr))
#define ext2_find_first_zero_bit(addr, size) \
	find_first_zero_le_bit((unsigned long *)(addr), (size))
#define ext2_find_next_zero_bit(addr, size, off) \
	find_next_zero_le_bit((unsigned long *)(addr), (size), (off))

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr)	\
	test_and_set_bit((nr),(unsigned long *)(addr))
#define minix_set_bit(nr,addr)	\
	set_bit((nr),(unsigned long *)(addr))
#define minix_test_and_clear_bit(nr,addr) \
	test_and_clear_bit((nr),(unsigned long *)(addr))
#define minix_test_bit(nr,addr)	\
	test_bit((nr),(unsigned long *)(addr))
#define minix_find_first_zero_bit(addr,size) \
	find_first_zero_bit((unsigned long *)(addr),(size))

#endif /* __KERNEL__ */

#endif /* defined(_SPARC64_BITOPS_H) */
