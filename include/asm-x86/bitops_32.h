#ifndef _I386_BITOPS_H
#define _I386_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

/**
 * find_first_zero_bit - find the first zero bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the first zero bit, not the number of the byte
 * containing a bit.
 */
static inline int find_first_zero_bit(const unsigned long *addr, unsigned size)
{
	int d0, d1, d2;
	int res;

	if (!size)
		return 0;
	/* This looks at memory.
	 * Mark it volatile to tell gcc not to move it around
	 */
	asm volatile("movl $-1,%%eax\n\t"
		     "xorl %%edx,%%edx\n\t"
		     "repe; scasl\n\t"
		     "je 1f\n\t"
		     "xorl -4(%%edi),%%eax\n\t"
		     "subl $4,%%edi\n\t"
		     "bsfl %%eax,%%edx\n"
		     "1:\tsubl %%ebx,%%edi\n\t"
		     "shll $3,%%edi\n\t"
		     "addl %%edi,%%edx"
		     : "=d" (res), "=&c" (d0), "=&D" (d1), "=&a" (d2)
		     : "1" ((size + 31) >> 5), "2" (addr),
		       "b" (addr) : "memory");
	return res;
}

/**
 * find_next_zero_bit - find the first zero bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bit number to start searching at
 * @size: The maximum size to search
 */
int find_next_zero_bit(const unsigned long *addr, int size, int offset);

/**
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static inline unsigned long __ffs(unsigned long word)
{
	__asm__("bsfl %1,%0"
		:"=r" (word)
		:"rm" (word));
	return word;
}

/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit number of the first set bit, not the number of the byte
 * containing a bit.
 */
static inline unsigned find_first_bit(const unsigned long *addr, unsigned size)
{
	unsigned x = 0;

	while (x < size) {
		unsigned long val = *addr++;
		if (val)
			return __ffs(val) + x;
		x += sizeof(*addr) << 3;
	}
	return x;
}

/**
 * find_next_bit - find the first set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bit number to start searching at
 * @size: The maximum size to search
 */
int find_next_bit(const unsigned long *addr, int size, int offset);

/**
 * ffz - find first zero in word.
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
static inline unsigned long ffz(unsigned long word)
{
	__asm__("bsfl %1,%0"
		:"=r" (word)
		:"r" (~word));
	return word;
}

#ifdef __KERNEL__

#include <asm-generic/bitops/sched.h>

/**
 * ffs - find first bit set
 * @x: the word to search
 *
 * This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz() (man ffs).
 */
static inline int ffs(int x)
{
	int r;

	__asm__("bsfl %1,%0\n\t"
		"jnz 1f\n\t"
		"movl $-1,%0\n"
		"1:" : "=r" (r) : "rm" (x));
	return r+1;
}

/**
 * fls - find last bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs().
 */
static inline int fls(int x)
{
	int r;

	__asm__("bsrl %1,%0\n\t"
		"jnz 1f\n\t"
		"movl $-1,%0\n"
		"1:" : "=r" (r) : "rm" (x));
	return r+1;
}

#include <asm-generic/bitops/hweight.h>

#endif /* __KERNEL__ */

#include <asm-generic/bitops/fls64.h>

#ifdef __KERNEL__

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(lock, nr, addr)			\
	test_and_set_bit((nr), (unsigned long *)(addr))
#define ext2_clear_bit_atomic(lock, nr, addr)			\
	test_and_clear_bit((nr), (unsigned long *)(addr))

#include <asm-generic/bitops/minix.h>

#endif /* __KERNEL__ */

#endif /* _I386_BITOPS_H */
