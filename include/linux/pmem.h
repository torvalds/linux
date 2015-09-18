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
static inline bool __arch_has_wmb_pmem(void)
{
	return false;
}

static inline void arch_wmb_pmem(void)
{
	BUG();
}

static inline void arch_memcpy_to_pmem(void __pmem *dst, const void *src,
		size_t n)
{
	BUG();
}

static inline size_t arch_copy_from_iter_pmem(void __pmem *addr, size_t bytes,
		struct iov_iter *i)
{
	BUG();
	return 0;
}

static inline void arch_clear_pmem(void __pmem *addr, size_t size)
{
	BUG();
}
#endif

/*
 * Architectures that define ARCH_HAS_PMEM_API must provide
 * implementations for arch_memcpy_to_pmem(), arch_wmb_pmem(),
 * arch_copy_from_iter_pmem(), arch_clear_pmem() and arch_has_wmb_pmem().
 */
static inline void memcpy_from_pmem(void *dst, void __pmem const *src, size_t size)
{
	memcpy(dst, (void __force const *) src, size);
}

static inline void memunmap_pmem(struct device *dev, void __pmem *addr)
{
	devm_memunmap(dev, (void __force *) addr);
}

static inline bool arch_has_pmem_api(void)
{
	return IS_ENABLED(CONFIG_ARCH_HAS_PMEM_API);
}

/**
 * arch_has_wmb_pmem - true if wmb_pmem() ensures durability
 *
 * For a given cpu implementation within an architecture it is possible
 * that wmb_pmem() resolves to a nop.  In the case this returns
 * false, pmem api users are unable to ensure durability and may want to
 * fall back to a different data consistency model, or otherwise notify
 * the user.
 */
static inline bool arch_has_wmb_pmem(void)
{
	return arch_has_pmem_api() && __arch_has_wmb_pmem();
}

/*
 * These defaults seek to offer decent performance and minimize the
 * window between i/o completion and writes being durable on media.
 * However, it is undefined / architecture specific whether
 * default_memremap_pmem + default_memcpy_to_pmem is sufficient for
 * making data durable relative to i/o completion.
 */
static inline void default_memcpy_to_pmem(void __pmem *dst, const void *src,
		size_t size)
{
	memcpy((void __force *) dst, src, size);
}

static inline size_t default_copy_from_iter_pmem(void __pmem *addr,
		size_t bytes, struct iov_iter *i)
{
	return copy_from_iter_nocache((void __force *)addr, bytes, i);
}

static inline void default_clear_pmem(void __pmem *addr, size_t size)
{
	if (size == PAGE_SIZE && ((unsigned long)addr & ~PAGE_MASK) == 0)
		clear_page((void __force *)addr);
	else
		memset((void __force *)addr, 0, size);
}

/**
 * memremap_pmem - map physical persistent memory for pmem api
 * @offset: physical address of persistent memory
 * @size: size of the mapping
 *
 * Establish a mapping of the architecture specific memory type expected
 * by memcpy_to_pmem() and wmb_pmem().  For example, it may be
 * the case that an uncacheable or writethrough mapping is sufficient,
 * or a writeback mapping provided memcpy_to_pmem() and
 * wmb_pmem() arrange for the data to be written through the
 * cache to persistent media.
 */
static inline void __pmem *memremap_pmem(struct device *dev,
		resource_size_t offset, unsigned long size)
{
	return (void __pmem *) devm_memremap(dev, offset, size,
			ARCH_MEMREMAP_PMEM);
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
 * must be followed by a wmb_pmem().
 */
static inline void memcpy_to_pmem(void __pmem *dst, const void *src, size_t n)
{
	if (arch_has_pmem_api())
		arch_memcpy_to_pmem(dst, src, n);
	else
		default_memcpy_to_pmem(dst, src, n);
}

/**
 * wmb_pmem - synchronize writes to persistent memory
 *
 * After a series of memcpy_to_pmem() operations this drains data from
 * cpu write buffers and any platform (memory controller) buffers to
 * ensure that written data is durable on persistent memory media.
 */
static inline void wmb_pmem(void)
{
	if (arch_has_wmb_pmem())
		arch_wmb_pmem();
	else
		wmb();
}

/**
 * copy_from_iter_pmem - copy data from an iterator to PMEM
 * @addr:	PMEM destination address
 * @bytes:	number of bytes to copy
 * @i:		iterator with source data
 *
 * Copy data from the iterator 'i' to the PMEM buffer starting at 'addr'.
 * This function requires explicit ordering with a wmb_pmem() call.
 */
static inline size_t copy_from_iter_pmem(void __pmem *addr, size_t bytes,
		struct iov_iter *i)
{
	if (arch_has_pmem_api())
		return arch_copy_from_iter_pmem(addr, bytes, i);
	return default_copy_from_iter_pmem(addr, bytes, i);
}

/**
 * clear_pmem - zero a PMEM memory range
 * @addr:	virtual start address
 * @size:	number of bytes to zero
 *
 * Write zeros into the memory range starting at 'addr' for 'size' bytes.
 * This function requires explicit ordering with a wmb_pmem() call.
 */
static inline void clear_pmem(void __pmem *addr, size_t size)
{
	if (arch_has_pmem_api())
		arch_clear_pmem(addr, size);
	else
		default_clear_pmem(addr, size);
}
#endif /* __PMEM_H__ */
