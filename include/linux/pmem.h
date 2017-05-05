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

static inline size_t arch_copy_from_iter_pmem(void *addr, size_t bytes,
		struct iov_iter *i)
{
	BUG();
	return 0;
}

static inline void arch_clear_pmem(void *addr, size_t size)
{
	BUG();
}

static inline void arch_wb_cache_pmem(void *addr, size_t size)
{
	BUG();
}

static inline void arch_invalidate_pmem(void *addr, size_t size)
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

/**
 * copy_from_iter_pmem - copy data from an iterator to PMEM
 * @addr:	PMEM destination address
 * @bytes:	number of bytes to copy
 * @i:		iterator with source data
 *
 * Copy data from the iterator 'i' to the PMEM buffer starting at 'addr'.
 * See blkdev_issue_flush() note for memcpy_to_pmem().
 */
static inline size_t copy_from_iter_pmem(void *addr, size_t bytes,
		struct iov_iter *i)
{
	if (arch_has_pmem_api())
		return arch_copy_from_iter_pmem(addr, bytes, i);
	return copy_from_iter_nocache(addr, bytes, i);
}

/**
 * clear_pmem - zero a PMEM memory range
 * @addr:	virtual start address
 * @size:	number of bytes to zero
 *
 * Write zeros into the memory range starting at 'addr' for 'size' bytes.
 * See blkdev_issue_flush() note for memcpy_to_pmem().
 */
static inline void clear_pmem(void *addr, size_t size)
{
	if (arch_has_pmem_api())
		arch_clear_pmem(addr, size);
	else
		memset(addr, 0, size);
}

/**
 * invalidate_pmem - flush a pmem range from the cache hierarchy
 * @addr:	virtual start address
 * @size:	bytes to invalidate (internally aligned to cache line size)
 *
 * For platforms that support clearing poison this flushes any poisoned
 * ranges out of the cache
 */
static inline void invalidate_pmem(void *addr, size_t size)
{
	if (arch_has_pmem_api())
		arch_invalidate_pmem(addr, size);
}

/**
 * wb_cache_pmem - write back processor cache for PMEM memory range
 * @addr:	virtual start address
 * @size:	number of bytes to write back
 *
 * Write back the processor cache range starting at 'addr' for 'size' bytes.
 * See blkdev_issue_flush() note for memcpy_to_pmem().
 */
static inline void wb_cache_pmem(void *addr, size_t size)
{
	if (arch_has_pmem_api())
		arch_wb_cache_pmem(addr, size);
}
#endif /* __PMEM_H__ */
