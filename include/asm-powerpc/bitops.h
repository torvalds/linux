/*
 * PowerPC atomic bit operations.
 *
 * Merged version by David Gibson <david@gibson.dropbear.id.au>.
 * Based on ppc64 versions by: Dave Engebretsen, Todd Inglett, Don
 * Reed, Pat McCarthy, Peter Bergner, Anton Blanchard.  They
 * originally took it from the ppc32 code.
 *
 * Within a word, bits are numbered LSB first.  Lot's of places make
 * this assumption by directly testing bits with (val & (1<<nr)).
 * This can cause confusion for large (> 1 word) bitmaps on a
 * big-endian system because, unlike little endian, the number of each
 * bit depends on the word size.
 *
 * The bitop functions are defined to work on unsigned longs, so for a
 * ppc64 system the bits end up numbered:
 *   |63..............0|127............64|191...........128|255...........196|
 * and on ppc32:
 *   |31.....0|63....31|95....64|127...96|159..128|191..160|223..192|255..224|
 *
 * There are a few little-endian macros used mostly for filesystem
 * bitmaps, these work on similar bit arrays layouts, but
 * byte-oriented:
 *   |7...0|15...8|23...16|31...24|39...32|47...40|55...48|63...56|
 *
 * The main difference is that bit 3-5 (64b) or 3-4 (32b) in the bit
 * number field needs to be reversed compared to the big-endian bit
 * fields. This can be achieved by XOR with 0x38 (64b) or 0x18 (32b).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_BITOPS_H
#define _ASM_POWERPC_BITOPS_H

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/atomic.h>
#include <asm/synch.h>

/*
 * clear_bit doesn't imply a memory barrier
 */
#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	smp_mb()

#define BITOP_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BITOP_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITOP_LE_SWIZZLE	((BITS_PER_LONG-1) & ~0x7)

#ifdef CONFIG_PPC64
#define LARXL		"ldarx"
#define STCXL		"stdcx."
#define CNTLZL		"cntlzd"
#else
#define LARXL		"lwarx"
#define STCXL		"stwcx."
#define CNTLZL		"cntlzw"
#endif

static __inline__ void set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long old;
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	__asm__ __volatile__(
"1:"	LARXL "	%0,0,%3	# set_bit\n"
	"or	%0,%0,%2\n"
	PPC405_ERR77(0,%3)
	STCXL "	%0,0,%3\n"
	"bne-	1b"
	: "=&r"(old), "=m"(*p)
	: "r"(mask), "r"(p), "m"(*p)
	: "cc" );
}

static __inline__ void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long old;
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	__asm__ __volatile__(
"1:"	LARXL "	%0,0,%3	# set_bit\n"
	"andc	%0,%0,%2\n"
	PPC405_ERR77(0,%3)
	STCXL "	%0,0,%3\n"
	"bne-	1b"
	: "=&r"(old), "=m"(*p)
	: "r"(mask), "r"(p), "m"(*p)
	: "cc" );
}

static __inline__ void change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long old;
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	__asm__ __volatile__(
"1:"	LARXL "	%0,0,%3	# set_bit\n"
	"xor	%0,%0,%2\n"
	PPC405_ERR77(0,%3)
	STCXL "	%0,0,%3\n"
	"bne-	1b"
	: "=&r"(old), "=m"(*p)
	: "r"(mask), "r"(p), "m"(*p)
	: "cc" );
}

static __inline__ int test_and_set_bit(unsigned long nr,
				       volatile unsigned long *addr)
{
	unsigned long old, t;
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:"	LARXL "	%0,0,%3		# test_and_set_bit\n"
	"or	%1,%0,%2 \n"
	PPC405_ERR77(0,%3)
	STCXL "	%1,0,%3 \n"
	"bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (old), "=&r" (t)
	: "r" (mask), "r" (p)
	: "cc", "memory");

	return (old & mask) != 0;
}

static __inline__ int test_and_clear_bit(unsigned long nr,
					 volatile unsigned long *addr)
{
	unsigned long old, t;
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:"	LARXL "	%0,0,%3		# test_and_clear_bit\n"
	"andc	%1,%0,%2 \n"
	PPC405_ERR77(0,%3)
	STCXL "	%1,0,%3 \n"
	"bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (old), "=&r" (t)
	: "r" (mask), "r" (p)
	: "cc", "memory");

	return (old & mask) != 0;
}

static __inline__ int test_and_change_bit(unsigned long nr,
					  volatile unsigned long *addr)
{
	unsigned long old, t;
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:"	LARXL "	%0,0,%3		# test_and_change_bit\n"
	"xor	%1,%0,%2 \n"
	PPC405_ERR77(0,%3)
	STCXL "	%1,0,%3 \n"
	"bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (old), "=&r" (t)
	: "r" (mask), "r" (p)
	: "cc", "memory");

	return (old & mask) != 0;
}

static __inline__ void set_bits(unsigned long mask, unsigned long *addr)
{
        unsigned long old;

	__asm__ __volatile__(
"1:"	LARXL "	%0,0,%3         # set_bit\n"
	"or	%0,%0,%2\n"
	STCXL "	%0,0,%3\n"
	"bne-	1b"
	: "=&r" (old), "=m" (*addr)
	: "r" (mask), "r" (addr), "m" (*addr)
	: "cc");
}

/* Non-atomic versions */
static __inline__ int test_bit(unsigned long nr,
			       __const__ volatile unsigned long *addr)
{
	return 1UL & (addr[BITOP_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

static __inline__ void __set_bit(unsigned long nr,
				 volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	*p  |= mask;
}

static __inline__ void __clear_bit(unsigned long nr,
				   volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	*p &= ~mask;
}

static __inline__ void __change_bit(unsigned long nr,
				    volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);

	*p ^= mask;
}

static __inline__ int __test_and_set_bit(unsigned long nr,
					 volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);
	unsigned long old = *p;

	*p = old | mask;
	return (old & mask) != 0;
}

static __inline__ int __test_and_clear_bit(unsigned long nr,
					   volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);
	unsigned long old = *p;

	*p = old & ~mask;
	return (old & mask) != 0;
}

static __inline__ int __test_and_change_bit(unsigned long nr,
					    volatile unsigned long *addr)
{
	unsigned long mask = BITOP_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BITOP_WORD(nr);
	unsigned long old = *p;

	*p = old ^ mask;
	return (old & mask) != 0;
}

/*
 * Return the zero-based bit position (LE, not IBM bit numbering) of
 * the most significant 1-bit in a double word.
 */
static __inline__ int __ilog2(unsigned long x)
{
	int lz;

	asm (CNTLZL " %0,%1" : "=r" (lz) : "r" (x));
	return BITS_PER_LONG - 1 - lz;
}

/*
 * Determines the bit position of the least significant 0 bit in the
 * specified double word. The returned bit position will be
 * zero-based, starting from the right side (63/31 - 0).
 */
static __inline__ unsigned long ffz(unsigned long x)
{
	/* no zero exists anywhere in the 8 byte area. */
	if ((x = ~x) == 0)
		return BITS_PER_LONG;

	/*
	 * Calculate the bit position of the least signficant '1' bit in x
	 * (since x has been changed this will actually be the least signficant
	 * '0' bit in * the original x).  Note: (x & -x) gives us a mask that
	 * is the least significant * (RIGHT-most) 1-bit of the value in x.
	 */
	return __ilog2(x & -x);
}

static __inline__ int __ffs(unsigned long x)
{
	return __ilog2(x & -x);
}

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */
static __inline__ int ffs(int x)
{
	unsigned long i = (unsigned long)x;
	return __ilog2(i & -i) + 1;
}

/*
 * fls: find last (most-significant) bit set.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */
static __inline__ int fls(unsigned int x)
{
	int lz;

	asm ("cntlzw %0,%1" : "=r" (lz) : "r" (x));
	return 32 - lz;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */
#define hweight64(x) generic_hweight64(x)
#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#define find_first_zero_bit(addr, size) find_next_zero_bit((addr), (size), 0)
unsigned long find_next_zero_bit(const unsigned long *addr,
				 unsigned long size, unsigned long offset);
/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit-number of the first set bit, not the number of the byte
 * containing a bit.
 */
#define find_first_bit(addr, size) find_next_bit((addr), (size), 0)
unsigned long find_next_bit(const unsigned long *addr,
			    unsigned long size, unsigned long offset);

/* Little-endian versions */

static __inline__ int test_le_bit(unsigned long nr,
				  __const__ unsigned long *addr)
{
	__const__ unsigned char	*tmp = (__const__ unsigned char *) addr;
	return (tmp[nr >> 3] >> (nr & 7)) & 1;
}

#define __set_le_bit(nr, addr) \
	__set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define __clear_le_bit(nr, addr) \
	__clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#define test_and_set_le_bit(nr, addr) \
	test_and_set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define test_and_clear_le_bit(nr, addr) \
	test_and_clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#define __test_and_set_le_bit(nr, addr) \
	__test_and_set_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))
#define __test_and_clear_le_bit(nr, addr) \
	__test_and_clear_bit((nr) ^ BITOP_LE_SWIZZLE, (addr))

#define find_first_zero_le_bit(addr, size) find_next_zero_le_bit((addr), (size), 0)
unsigned long find_next_zero_le_bit(const unsigned long *addr,
				    unsigned long size, unsigned long offset);

/* Bitmap functions for the ext2 filesystem */

#define ext2_set_bit(nr,addr) \
	__test_and_set_le_bit((nr), (unsigned long*)addr)
#define ext2_clear_bit(nr, addr) \
	__test_and_clear_le_bit((nr), (unsigned long*)addr)

#define ext2_set_bit_atomic(lock, nr, addr) \
	test_and_set_le_bit((nr), (unsigned long*)addr)
#define ext2_clear_bit_atomic(lock, nr, addr) \
	test_and_clear_le_bit((nr), (unsigned long*)addr)

#define ext2_test_bit(nr, addr)      test_le_bit((nr),(unsigned long*)addr)

#define ext2_find_first_zero_bit(addr, size) \
	find_first_zero_le_bit((unsigned long*)addr, size)
#define ext2_find_next_zero_bit(addr, size, off) \
	find_next_zero_le_bit((unsigned long*)addr, size, off)

/* Bitmap functions for the minix filesystem.  */

#define minix_test_and_set_bit(nr,addr) \
	__test_and_set_le_bit(nr, (unsigned long *)addr)
#define minix_set_bit(nr,addr) \
	__set_le_bit(nr, (unsigned long *)addr)
#define minix_test_and_clear_bit(nr,addr) \
	__test_and_clear_le_bit(nr, (unsigned long *)addr)
#define minix_test_bit(nr,addr) \
	test_le_bit(nr, (unsigned long *)addr)

#define minix_find_first_zero_bit(addr,size) \
	find_first_zero_le_bit((unsigned long *)addr, size)

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is cleared.
 */
static inline int sched_find_first_bit(const unsigned long *b)
{
#ifdef CONFIG_PPC64
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 64;
	return __ffs(b[2]) + 128;
#else
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 32;
	if (unlikely(b[2]))
		return __ffs(b[2]) + 64;
	if (b[3])
		return __ffs(b[3]) + 96;
	return __ffs(b[4]) + 128;
#endif
}

#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_BITOPS_H */
