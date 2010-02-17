#ifndef _PERF_LINUX_BITOPS_H_
#define _PERF_LINUX_BITOPS_H_

#define __KERNEL__

#define CONFIG_GENERIC_FIND_NEXT_BIT
#define CONFIG_GENERIC_FIND_FIRST_BIT
#include "../../../../include/linux/bitops.h"

#undef __KERNEL__

static inline void set_bit(int nr, unsigned long *addr)
{
	addr[nr / BITS_PER_LONG] |= 1UL << (nr % BITS_PER_LONG);
}

static __always_inline int test_bit(unsigned int nr, const unsigned long *addr)
{
	return ((1UL << (nr % BITS_PER_LONG)) &
		(((unsigned long *)addr)[nr / BITS_PER_LONG])) != 0;
}

unsigned long generic_find_next_zero_le_bit(const unsigned long *addr, unsigned
		long size, unsigned long offset);

unsigned long generic_find_next_le_bit(const unsigned long *addr, unsigned
		long size, unsigned long offset);

#endif
