#ifndef _I386_BITOPS_H
#define _I386_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

#ifndef CONFIG_GENERIC_FIND_FIRST_BIT
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
#endif

#ifdef __KERNEL__

#include <asm-generic/bitops/sched.h>

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
