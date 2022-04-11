/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PERF_CACHELINE_H
#define PERF_CACHELINE_H

#include <linux/compiler.h>

int __pure cacheline_size(void);

static inline u64 cl_address(u64 address)
{
	/* return the cacheline of the address */
	return (address & ~(cacheline_size() - 1));
}

static inline u64 cl_offset(u64 address)
{
	/* return the cacheline of the address */
	return (address & (cacheline_size() - 1));
}

#endif // PERF_CACHELINE_H
