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

#ifdef CONFIG_ARCH_HAS_PMEM_API
#include <asm/cacheflush.h>
#else
static inline void arch_wmb_pmem(void)
{
	BUG();
}

static inline bool __arch_has_wmb_pmem(void)
{
	return false;
}

static inline void arch_memcpy_to_pmem(void __pmem *dst, const void *src,
		size_t n)
{
	BUG();
}
#endif

/*
 * Architectures that define ARCH_HAS_PMEM_API must provide
 * implementations for arch_memcpy_to_pmem(), arch_wmb_pmem(), and
 * __arch_has_wmb_pmem().
 */

static inline void memcpy_from_pmem(void *dst, void __pmem const *src, size_t size)
{
	memcpy(dst, (void __force const *) src, size);
}

static inline void memunmap_pmem(struct device *dev, void __pmem *addr)
{
	devm_memunmap(dev, (void __force *) addr);
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
	if (IS_ENABLED(CONFIG_ARCH_HAS_PMEM_API))
		return __arch_has_wmb_pmem();
	return false;
}

static inline bool arch_has_pmem_api(void)
{
	return IS_ENABLED(CONFIG_ARCH_HAS_PMEM_API) && arch_has_wmb_pmem();
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
#ifdef ARCH_MEMREMAP_PMEM
	return (void __pmem *) devm_memremap(dev, offset, size,
			ARCH_MEMREMAP_PMEM);
#else
	return (void __pmem *) devm_memremap(dev, offset, size,
			MEMREMAP_WT);
#endif
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
	if (arch_has_pmem_api())
		arch_wmb_pmem();
}
#endif /* __PMEM_H__ */
