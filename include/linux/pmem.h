/*
 * Copyright(c) 2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __PMEM_H__
#define __PMEM_H__

#include <linux/io.h>
#include <linux/uio.h>

#ifdef CONFIG_ARCH_HAS_PMEM_API
#define ARCH_MEMREMAP_PMEM MEMREMAP_WB
#include <asm/pmem.h>
#else
#define ARCH_MEMREMAP_PMEM MEMREMAP_WT
/*
 * These are simply here to enable compilation, all call sites gate
 * calling these symbols with arch_has_pmem_api() and redirect to the
 * implementation in asm/pmem.h.
 */
static inline void arch_memcpy_to_pmem(void *dst, const void *src, size_t n)
{
	BUG();
}
#endif

static inline bool arch_has_pmem_api(void)
{
	return IS_ENABLED(CONFIG_ARCH_HAS_PMEM_API);
}

/**
 * memcpy_to_pmem - copy data to persistent memory
 * @dst: destination buffer for the copy
 * @src: source buffer for the copy
 * @n: length of the copy in bytes
 *
 * Perform a memory copy that results in the destination of the copy
 * being effectively evicted from, or never written to, the processor
 * cache hierarchy after the copy completes.  After memcpy_to_pmem()
 * data may still reside in cpu or platform buffers, so this operation
 * must be followed by a blkdev_issue_flush() on the pmem block device.
 */
static inline void memcpy_to_pmem(void *dst, const void *src, size_t n)
{
	if (arch_has_pmem_api())
		arch_memcpy_to_pmem(dst, src, n);
	else
		memcpy(dst, src, n);
}
#endif /* __PMEM_H__ */
