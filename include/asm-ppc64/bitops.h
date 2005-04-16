/*
 * PowerPC64 atomic bit operations.
 * Dave Engebretsen, Todd Inglett, Don Reed, Pat McCarthy, Peter Bergner,
 * Anton Blanchard
 *
 * Originally taken from the 32b PPC code.  Modified to use 64b values for
 * the various counters & memory references.
 *
 * Bitops are odd when viewed on big-endian systems.  They were designed
 * on little endian so the size of the bitset doesn't matter (low order bytes
 * come first) as long as the bit in question is valid.
 *
 * Bits are "tested" often using the C expression (val & (1<<nr)) so we do
 * our best to stay compatible with that.  The assumption is that val will
 * be unsigned long for such tests.  As such, we assume the bits are stored
 * as an array of unsigned long (the usual case is a single unsigned long,
 * of course).  Here's an example bitset with bit numbering:
 *
 *   |63..........0|127........64|195.......128|255.......196|
 *
 * This leads to a problem. If an int, short or char is passed as a bitset
 * it will be a bad memory reference since we want to store in chunks
 * of unsigned long (64 bits here) size.
 *
 * There are a few little-endian macros used mostly for filesystem bitmaps,
 * these work on similar bit arrays layouts, but byte-oriented:
 *
 *   |7...0|15...8|23...16|31...24|39...32|47...40|55...48|63...56|
 *
 * The main difference is that bit 3-5 in the bit number field needs to be
 * reversed compared to the big-endian bit fields. This can be achieved
 * by XOR with 0b111000 (0x38).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _PPC64_BITOPS_H
#define _PPC64_BITOPS_H

#ifdef __KERNEL__

#include <asm/memory.h>

/*
 * clear_bit doesn't imply a memory barrier
 */
#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	smp_mb()

static __inline__ int test_bit(unsigned long nr, __const__ volatile unsigned long *addr)
{
	return (1UL & (addr[nr >> 6] >> (nr & 63)));
}

static __inline__ void set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long old;
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	__asm__ __volatile__(
"1:	ldarx	%0,0,%3		# set_bit\n\
	or	%0,%0,%2\n\
	stdcx.	%0,0,%3\n\
	bne-	1b"
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");
}

static __inline__ void clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long old;
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	__asm__ __volatile__(
"1:	ldarx	%0,0,%3		# clear_bit\n\
	andc	%0,%0,%2\n\
	stdcx.	%0,0,%3\n\
	bne-	1b"
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");
}

static __inline__ void change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long old;
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	__asm__ __volatile__(
"1:	ldarx	%0,0,%3		# change_bit\n\
	xor	%0,%0,%2\n\
	stdcx.	%0,0,%3\n\
	bne-	1b"
	: "=&r" (old), "=m" (*p)
	: "r" (mask), "r" (p), "m" (*p)
	: "cc");
}

static __inline__ int test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long old, t;
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	ldarx	%0,0,%3		# test_and_set_bit\n\
	or	%1,%0,%2 \n\
	stdcx.	%1,0,%3 \n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (old), "=&r" (t)
	: "r" (mask), "r" (p)
	: "cc", "memory");

	return (old & mask) != 0;
}

static __inline__ int test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long old, t;
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	ldarx	%0,0,%3		# test_and_clear_bit\n\
	andc	%1,%0,%2\n\
	stdcx.	%1,0,%3\n\
	bne-	1b"
	ISYNC_ON_SMP
	: "=&r" (old), "=&r" (t)
	: "r" (mask), "r" (p)
	: "cc", "memory");

	return (old & mask) != 0;
}

static __inline__ int test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long old, t;
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	__asm__ __volatile__(
	EIEIO_ON_SMP
"1:	ldarx	%0,0,%3		# test_and_change_bit\n\
	xor	%1,%0,%2\n\
	stdcx.	%1,0,%3\n\
	bne-	1b"
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
"1:	ldarx	%0,0,%3		# set_bit\n\
	or	%0,%0,%2\n\
	stdcx.	%0,0,%3\n\
	bne-	1b"
	: "=&r" (old), "=m" (*addr)
	: "r" (mask), "r" (addr), "m" (*addr)
	: "cc");
}

/*
 * non-atomic versions
 */
static __inline__ void __set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	*p |= mask;
}

static __inline__ void __clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	*p &= ~mask;
}

static __inline__ void __change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);

	*p ^= mask;
}

static __inline__ int __test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);
	unsigned long old = *p;

	*p = old | mask;
	return (old & mask) != 0;
}

static __inline__ int __test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);
	unsigned long old = *p;

	*p = old & ~mask;
	return (old & mask) != 0;
}

static __inline__ int __test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x3f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 6);
	unsigned long old = *p;

	*p = old ^ mask;
	return (old & mask) != 0;
}

/*
 * Return the zero-based bit position (from RIGHT TO LEFT, 63 -> 0) of the
 * most significant (left-most) 1-bit in a double word.
 */
static __inline__ int __ilog2(unsigned long x)
{
	int lz;

	asm ("cntlzd %0,%1" : "=r" (lz) : "r" (x));
	return 63 - lz;
}

/*
 * Determines the bit position of the least significant (rightmost) 0 bit
 * in the specified double word. The returned bit position will be zero-based,
 * starting from the right side (63 - 0).
 */
static __inline__ unsigned long ffz(unsigned long x)
{
	/* no zero exists anywhere in the 8 byte area. */
	if ((x = ~x) == 0)
		return 64;

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
#define fls(x) generic_fls(x)

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */
#define hweight64(x) generic_hweight64(x)
#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

extern unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size, unsigned long offset);
#define find_first_zero_bit(addr, size) \
	find_next_zero_bit((addr), (size), 0)

extern unsigned long find_next_bit(const unsigned long *addr, unsigned long size, unsigned long offset);
#define find_first_bit(addr, size) \
	find_next_bit((addr), (size), 0)

extern unsigned long find_next_zero_le_bit(const unsigned long *addr, unsigned long size, unsigned long offset);
#define find_first_zero_le_bit(addr, size) \
	find_next_zero_le_bit((addr), (size), 0)

static __inline__ int test_le_bit(unsigned long nr, __const__ unsigned long * addr)
{
	__const__ unsigned char	*ADDR = (__const__ unsigned char *) addr;
	return (ADDR[nr >> 3] >> (nr & 7)) & 1;
}

#define test_and_clear_le_bit(nr, addr) \
	test_and_clear_bit((nr) ^ 0x38, (addr))
#define test_and_set_le_bit(nr, addr) \
	test_and_set_bit((nr) ^ 0x38, (addr))

/*
 * non-atomic versions
 */

#define __set_le_bit(nr, addr) \
	__set_bit((nr) ^ 0x38, (addr))
#define __clear_le_bit(nr, addr) \
	__clear_bit((nr) ^ 0x38, (addr))
#define __test_and_clear_le_bit(nr, addr) \
	__test_and_clear_bit((nr) ^ 0x38, (addr))
#define __test_and_set_le_bit(nr, addr) \
	__test_and_set_bit((nr) ^ 0x38, (addr))

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

#define minix_test_and_set_bit(nr,addr)		test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr)			set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr)	test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr)			test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size)	find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */
#endif /* _PPC64_BITOPS_H */
