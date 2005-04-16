/* $Id: bitops.h,v 1.67 2001/11/19 18:36:34 davem Exp $
 * bitops.h: Bit string operations on the Sparc.
 *
 * Copyright 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright 1996 Eddie C. Dost   (ecd@skynet.be)
 * Copyright 2001 Anton Blanchard (anton@samba.org)
 */

#ifndef _SPARC_BITOPS_H
#define _SPARC_BITOPS_H

#include <linux/compiler.h>
#include <asm/byteorder.h>

#ifdef __KERNEL__

/*
 * Set bit 'nr' in 32-bit quantity at address 'addr' where bit '0'
 * is in the highest of the four bytes and bit '31' is the high bit
 * within the first byte. Sparc is BIG-Endian. Unless noted otherwise
 * all bit-ops return 0 if bit was previously clear and != 0 otherwise.
 */
static inline int test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g5");
	register int tmp4 asm("g7");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___set_bit\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (mask), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3), "=r" (tmp4)
	: "0" (mask), "r" (ADDR)
	: "memory", "cc");

	return mask != 0;
}

static inline void set_bit(unsigned long nr, volatile unsigned long *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g5");
	register int tmp4 asm("g7");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___set_bit\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (mask), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3), "=r" (tmp4)
	: "0" (mask), "r" (ADDR)
	: "memory", "cc");
}

static inline int test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g5");
	register int tmp4 asm("g7");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___clear_bit\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (mask), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3), "=r" (tmp4)
	: "0" (mask), "r" (ADDR)
	: "memory", "cc");

	return mask != 0;
}

static inline void clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g5");
	register int tmp4 asm("g7");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___clear_bit\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (mask), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3), "=r" (tmp4)
	: "0" (mask), "r" (ADDR)
	: "memory", "cc");
}

static inline int test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g5");
	register int tmp4 asm("g7");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___change_bit\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (mask), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3), "=r" (tmp4)
	: "0" (mask), "r" (ADDR)
	: "memory", "cc");

	return mask != 0;
}

static inline void change_bit(unsigned long nr, volatile unsigned long *addr)
{
	register unsigned long mask asm("g2");
	register unsigned long *ADDR asm("g1");
	register int tmp1 asm("g3");
	register int tmp2 asm("g4");
	register int tmp3 asm("g5");
	register int tmp4 asm("g7");

	ADDR = ((unsigned long *) addr) + (nr >> 5);
	mask = 1 << (nr & 31);

	__asm__ __volatile__(
	"mov	%%o7, %%g4\n\t"
	"call	___change_bit\n\t"
	" add	%%o7, 8, %%o7\n"
	: "=&r" (mask), "=r" (tmp1), "=r" (tmp2), "=r" (tmp3), "=r" (tmp4)
	: "0" (mask), "r" (ADDR)
	: "memory", "cc");
}

/*
 * non-atomic versions
 */
static inline void __set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	*p |= mask;
}

static inline void __clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	*p &= ~mask;
}

static inline void __change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);

	*p ^= mask;
}

static inline int __test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	unsigned long old = *p;

	*p = old | mask;
	return (old & mask) != 0;
}

static inline int __test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	unsigned long old = *p;

	*p = old & ~mask;
	return (old & mask) != 0;
}

static inline int __test_and_change_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1UL << (nr & 0x1f);
	unsigned long *p = ((unsigned long *)addr) + (nr >> 5);
	unsigned long old = *p;

	*p = old ^ mask;
	return (old & mask) != 0;
}

#define smp_mb__before_clear_bit()	do { } while(0)
#define smp_mb__after_clear_bit()	do { } while(0)

/* The following routine need not be atomic. */
static inline int test_bit(int nr, __const__ volatile unsigned long *addr)
{
	return (1UL & (((unsigned long *)addr)[nr >> 5] >> (nr & 31))) != 0UL;
}

/* The easy/cheese version for now. */
static inline unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;

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
static inline int __ffs(unsigned long word)
{
	int num = 0;

	if ((word & 0xffff) == 0) {
		num += 16;
		word >>= 16;
	}
	if ((word & 0xff) == 0) {
		num += 8;
		word >>= 8;
	}
	if ((word & 0xf) == 0) {
		num += 4;
		word >>= 4;
	}
	if ((word & 0x3) == 0) {
		num += 2;
		word >>= 2;
	}
	if ((word & 0x1) == 0)
		num += 1;
	return num;
}

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
	if (unlikely(b[1]))
		return __ffs(b[1]) + 32;
	if (unlikely(b[2]))
		return __ffs(b[2]) + 64;
	if (b[3])
		return __ffs(b[3]) + 96;
	return __ffs(b[4]) + 128;
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
 * fls: find last (most-significant) bit set.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */
#define fls(x) generic_fls(x)

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */
#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

/*
 * find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */
static inline unsigned long find_next_zero_bit(const unsigned long *addr,
    unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)        /* Are any bits zero? */
		return result + size; /* Nope. */
found_middle:
	return result + ffz(tmp);
}

/*
 * Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */
#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

/**
 * find_next_bit - find the first set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 *
 * Scheduler induced bitop, do not use.
 */
static inline int find_next_bit(const unsigned long *addr, int size, int offset)
{
	const unsigned long *p = addr + (offset >> 5);
	int num = offset & ~0x1f;
	unsigned long word;

	word = *p++;
	word &= ~((1 << (offset & 0x1f)) - 1);
	while (num < size) {
		if (word != 0) {
			return __ffs(word) + num;
		}
		word = *p++;
		num += 0x20;
	}
	return num;
}

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

/*
 */
static inline int test_le_bit(int nr, __const__ unsigned long * addr)
{
	__const__ unsigned char *ADDR = (__const__ unsigned char *) addr;
	return (ADDR[nr >> 3] >> (nr & 7)) & 1;
}

/*
 * non-atomic versions
 */
static inline void __set_le_bit(int nr, unsigned long *addr)
{
	unsigned char *ADDR = (unsigned char *)addr;

	ADDR += nr >> 3;
	*ADDR |= 1 << (nr & 0x07);
}

static inline void __clear_le_bit(int nr, unsigned long *addr)
{
	unsigned char *ADDR = (unsigned char *)addr;

	ADDR += nr >> 3;
	*ADDR &= ~(1 << (nr & 0x07));
}

static inline int __test_and_set_le_bit(int nr, unsigned long *addr)
{
	int mask, retval;
	unsigned char *ADDR = (unsigned char *)addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	retval = (mask & *ADDR) != 0;
	*ADDR |= mask;
	return retval;
}

static inline int __test_and_clear_le_bit(int nr, unsigned long *addr)
{
	int mask, retval;
	unsigned char *ADDR = (unsigned char *)addr;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	retval = (mask & *ADDR) != 0;
	*ADDR &= ~mask;
	return retval;
}

static inline unsigned long find_next_zero_le_bit(const unsigned long *addr,
    unsigned long size, unsigned long offset)
{
	const unsigned long *p = addr + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		tmp = *(p++);
		tmp |= __swab32(~0UL >> (32-offset));
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	tmp = __swab32(tmp) | (~0UL << size);
	if (tmp == ~0UL)        /* Are any bits zero? */
		return result + size; /* Nope. */
	return result + ffz(tmp);

found_middle:
	return result + ffz(__swab32(tmp));
}

#define find_first_zero_le_bit(addr, size) \
        find_next_zero_le_bit((addr), (size), 0)

#define ext2_set_bit(nr,addr)	\
	__test_and_set_le_bit((nr),(unsigned long *)(addr))
#define ext2_clear_bit(nr,addr)	\
	__test_and_clear_le_bit((nr),(unsigned long *)(addr))

#define ext2_set_bit_atomic(lock, nr, addr)		\
	({						\
		int ret;				\
		spin_lock(lock);			\
		ret = ext2_set_bit((nr), (unsigned long *)(addr)); \
		spin_unlock(lock);			\
		ret;					\
	})

#define ext2_clear_bit_atomic(lock, nr, addr)		\
	({						\
		int ret;				\
		spin_lock(lock);			\
		ret = ext2_clear_bit((nr), (unsigned long *)(addr)); \
		spin_unlock(lock);			\
		ret;					\
	})

#define ext2_test_bit(nr,addr)	\
	test_le_bit((nr),(unsigned long *)(addr))
#define ext2_find_first_zero_bit(addr, size) \
	find_first_zero_le_bit((unsigned long *)(addr), (size))
#define ext2_find_next_zero_bit(addr, size, off) \
	find_next_zero_le_bit((unsigned long *)(addr), (size), (off))

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr)	\
	test_and_set_bit((nr),(unsigned long *)(addr))
#define minix_set_bit(nr,addr)		\
	set_bit((nr),(unsigned long *)(addr))
#define minix_test_and_clear_bit(nr,addr) \
	test_and_clear_bit((nr),(unsigned long *)(addr))
#define minix_test_bit(nr,addr)		\
	test_bit((nr),(unsigned long *)(addr))
#define minix_find_first_zero_bit(addr,size) \
	find_first_zero_bit((unsigned long *)(addr),(size))

#endif /* __KERNEL__ */

#endif /* defined(_SPARC_BITOPS_H) */
