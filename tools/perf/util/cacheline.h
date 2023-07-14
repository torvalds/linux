/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_CACHELINE_H
#define PERF_CACHELINE_H

#include <linux/compiler.h>

int __pure cacheline_size(void);


/*
 * Some architectures have 'Adjacent Cacheline Prefetch' feature,
 * which performs like the cacheline size being doubled.
 */
static inline u64 cl_address(u64 address, bool double_cl)
{
	u64 size = cacheline_size();

	if (double_cl)
		size *= 2;

	/* return the cacheline of the address */
	return (address & ~(size - 1));
}

static inline u64 cl_offset(u64 address, bool double_cl)
{
	u64 size = cacheline_size();

	if (double_cl)
		size *= 2;

	/* return the offset inside cacheline */
	return (address & (size - 1));
}

#endif // PERF_CACHELINE_H
