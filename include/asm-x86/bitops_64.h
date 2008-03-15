#ifndef _X86_64_BITOPS_H
#define _X86_64_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

extern long find_first_zero_bit(const unsigned long *addr, unsigned long size);
extern long find_first_bit(const unsigned long *addr, unsigned long size);

/* return index of first bet set in val or max when no bit is set */
static inline long __scanbit(unsigned long val, unsigned long max)
{
	asm("bsfq %1,%0 ; cmovz %2,%0" : "=&r" (val) : "r" (val), "r" (max));
	return val;
}

#define find_first_bit(addr, size)					\
	((__builtin_constant_p((size)) && (size) <= BITS_PER_LONG	\
	  ? (__scanbit(*(unsigned long *)(addr), (size)))		\
	  : find_first_bit((addr), (size))))

#define find_first_zero_bit(addr, size)					\
	((__builtin_constant_p((size)) && (size) <= BITS_PER_LONG	\
	  ? (__scanbit(~*(unsigned long *)(addr), (size)))		\
	  : find_first_zero_bit((addr), (size))))

static inline void set_bit_string(unsigned long *bitmap, unsigned long i,
				  int len)
{
	unsigned long end = i + len;
	while (i < end) {
		__set_bit(i, bitmap);
		i++;
	}
}

#ifdef __KERNEL__

#include <asm-generic/bitops/sched.h>

#define ARCH_HAS_FAST_MULTIPLIER 1

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

#endif /* _X86_64_BITOPS_H */
