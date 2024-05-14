/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_GETORDER_H
#define __ASM_GENERIC_GETORDER_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <linux/log2.h>

/**
 * get_order - Determine the allocation order of a memory size
 * @size: The size for which to get the order
 *
 * Determine the allocation order of a particular sized block of memory.  This
 * is on a logarithmic scale, where:
 *
 *	0 -> 2^0 * PAGE_SIZE and below
 *	1 -> 2^1 * PAGE_SIZE to 2^0 * PAGE_SIZE + 1
 *	2 -> 2^2 * PAGE_SIZE to 2^1 * PAGE_SIZE + 1
 *	3 -> 2^3 * PAGE_SIZE to 2^2 * PAGE_SIZE + 1
 *	4 -> 2^4 * PAGE_SIZE to 2^3 * PAGE_SIZE + 1
 *	...
 *
 * The order returned is used to find the smallest allocation granule required
 * to hold an object of the specified size.
 *
 * The result is undefined if the size is 0.
 */
static __always_inline __attribute_const__ int get_order(unsigned long size)
{
	if (__builtin_constant_p(size)) {
		if (!size)
			return BITS_PER_LONG - PAGE_SHIFT;

		if (size < (1UL << PAGE_SHIFT))
			return 0;

		return ilog2((size) - 1) - PAGE_SHIFT + 1;
	}

	size--;
	size >>= PAGE_SHIFT;
#if BITS_PER_LONG == 32
	return fls(size);
#else
	return fls64(size);
#endif
}

#endif	/* __ASSEMBLY__ */

#endif	/* __ASM_GENERIC_GETORDER_H */
