/* bitops.h: bit operations for the Fujitsu FR-V CPUs
 *
 * For an explanation of how atomic ops work in this arch, see:
 *   Documentation/fujitsu/frv/atomic-ops.txt
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_BITOPS_H
#define _ASM_BITOPS_H

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/atomic.h>

#ifdef __KERNEL__

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;

	while (word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

static inline int test_and_clear_bit(int nr, volatile void *addr)
{
	volatile unsigned long *ptr = addr;
	unsigned long mask = 1UL << (nr & 31);
	ptr += nr >> 5;
	return (atomic_test_and_ANDNOT_mask(mask, ptr) & mask) != 0;
}

static inline int test_and_set_bit(int nr, volatile void *addr)
{
	volatile unsigned long *ptr = addr;
	unsigned long mask = 1UL << (nr & 31);
	ptr += nr >> 5;
	return (atomic_test_and_OR_mask(mask, ptr) & mask) != 0;
}

static inline int test_and_change_bit(int nr, volatile void *addr)
{
	volatile unsigned long *ptr = addr;
	unsigned long mask = 1UL << (nr & 31);
	ptr += nr >> 5;
	return (atomic_test_and_XOR_mask(mask, ptr) & mask) != 0;
}

static inline void clear_bit(int nr, volatile void *addr)
{
	test_and_clear_bit(nr, addr);
}

static inline void set_bit(int nr, volatile void *addr)
{
	test_and_set_bit(nr, addr);
}

static inline void change_bit(int nr, volatile void * addr)
{
	test_and_change_bit(nr, addr);
}

static inline void __clear_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	*a &= ~mask;
}

static inline void __set_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	*a |= mask;
}

static inline void __change_bit(int nr, volatile void *addr)
{
	volatile unsigned long *a = addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	*a ^= mask;
}

static inline int __test_and_clear_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask, retval;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	return retval;
}

static inline int __test_and_set_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask, retval;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	retval = (mask & *a) != 0;
	*a |= mask;
	return retval;
}

static inline int __test_and_change_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask, retval;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	retval = (mask & *a) != 0;
	*a ^= mask;
	return retval;
}

/*
 * This routine doesn't need to be atomic.
 */
static inline int __constant_test_bit(int nr, const volatile void * addr)
{
	return ((1UL << (nr & 31)) & (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}

static inline int __test_bit(int nr, const volatile void * addr)
{
	int 	* a = (int *) addr;
	int	mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *a) != 0);
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 __constant_test_bit((nr),(addr)) : \
 __test_bit((nr),(addr)))

extern int find_next_bit(const unsigned long *addr, int size, int offset);

#define find_first_bit(addr, size) find_next_bit(addr, size, 0)

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

static inline int find_next_zero_bit(const void *addr, int size, int offset)
{
	const unsigned long *p = ((const unsigned long *) addr) + (offset >> 5);
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
	tmp |= ~0UL >> size;
found_middle:
	return result + ffz(tmp);
}

#define ffs(x) generic_ffs(x)
#define __ffs(x) (ffs(x) - 1)

/*
 * fls: find last bit set.
 */
#define fls(x)						\
({							\
	int bit;					\
							\
	asm("scan %1,gr0,%0" : "=r"(bit) : "r"(x));	\
							\
	bit ? 33 - bit : bit;				\
})
#define fls64(x)   generic_fls64(x)

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is cleared.
 */
static inline int sched_find_first_bit(const unsigned long *b)
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
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#define ext2_set_bit(nr, addr)		test_and_set_bit  ((nr) ^ 0x18, (addr))
#define ext2_clear_bit(nr, addr)	test_and_clear_bit((nr) ^ 0x18, (addr))

#define ext2_set_bit_atomic(lock,nr,addr)	ext2_set_bit((nr), addr)
#define ext2_clear_bit_atomic(lock,nr,addr)	ext2_clear_bit((nr), addr)

static inline int ext2_test_bit(int nr, const volatile void * addr)
{
	const volatile unsigned char *ADDR = (const unsigned char *) addr;
	int mask;

	ADDR += nr >> 3;
	mask = 1 << (nr & 0x07);
	return ((mask & *ADDR) != 0);
}

#define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)

static inline unsigned long ext2_find_next_zero_bit(const void *addr,
						    unsigned long size,
						    unsigned long offset)
{
	const unsigned long *p = ((const unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		/* We hold the little endian value in tmp, but then the
		 * shift is illegal. So we could keep a big endian value
		 * in tmp, like this:
		 *
		 * tmp = __swab32(*(p++));
		 * tmp |= ~0UL >> (32-offset);
		 *
		 * but this would decrease preformance, so we change the
		 * shift:
		 */
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
	/* tmp is little endian, so we would have to swab the shift,
	 * see above. But then we have to swab tmp below for ffz, so
	 * we might as well do this here.
	 */
	return result + ffz(__swab32(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab32(tmp));
}

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr)		ext2_set_bit(nr,addr)
#define minix_set_bit(nr,addr)			ext2_set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr)	ext2_clear_bit(nr,addr)
#define minix_test_bit(nr,addr)			ext2_test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size)	ext2_find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */

#endif /* _ASM_BITOPS_H */
