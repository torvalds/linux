#ifndef _ASM_GENERIC_PAGE_H
#define _ASM_GENERIC_PAGE_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/log2.h>

/*
 * non-const pure 2^n version of get_order
 * - the arch may override these in asm/bitops.h if they can be implemented
 *   more efficiently than using the arch log2 routines
 * - we use the non-const log2() instead if the arch has defined one suitable
 */
#ifndef ARCH_HAS_GET_ORDER
static inline __attribute__((const))
int __get_order(unsigned long size, int page_shift)
{
#if BITS_PER_LONG == 32 && defined(ARCH_HAS_ILOG2_U32)
	int order = __ilog2_u32(size) - page_shift;
	return order >= 0 ? order : 0;
#elif BITS_PER_LONG == 64 && defined(ARCH_HAS_ILOG2_U64)
	int order = __ilog2_u64(size) - page_shift;
	return order >= 0 ? order : 0;
#else
	int order;

	size = (size - 1) >> (page_shift - 1);
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);
	return order;
#endif
}
#endif

/**
 * get_order - calculate log2(pages) to hold a block of the specified size
 * @n - size
 *
 * calculate allocation order based on the current page size
 * - this can be used to initialise global variables from constant data
 */
#define get_order(n)							\
(									\
	__builtin_constant_p(n) ?					\
	((n < (1UL << PAGE_SHIFT)) ? 0 : ilog2(n) - PAGE_SHIFT) :	\
	__get_order(n, PAGE_SHIFT)					\
 )

#endif	/* __ASSEMBLY__ */
#endif	/* __KERNEL__ */

#endif	/* _ASM_GENERIC_PAGE_H */
