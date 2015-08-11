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
#include <linux/device.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/mm.h>

#ifndef ioremap_cache
/* temporary while we convert existing ioremap_cache users to memremap */
__weak void __iomem *ioremap_cache(resource_size_t offset, unsigned long size)
{
	return ioremap(offset, size);
}
#endif

/**
 * memremap() - remap an iomem_resource as cacheable memory
 * @offset: iomem resource start address
 * @size: size of remap
 * @flags: either MEMREMAP_WB or MEMREMAP_WT
 *
 * memremap() is "ioremap" for cases where it is known that the resource
 * being mapped does not have i/o side effects and the __iomem
 * annotation is not applicable.
 *
 * MEMREMAP_WB - matches the default mapping for "System RAM" on
 * the architecture.  This is usually a read-allocate write-back cache.
 * Morever, if MEMREMAP_WB is specified and the requested remap region is RAM
 * memremap() will bypass establishing a new mapping and instead return
 * a pointer into the direct map.
 *
 * MEMREMAP_WT - establish a mapping whereby writes either bypass the
 * cache or are written through to memory and never exist in a
 * cache-dirty state with respect to program visibility.  Attempts to
 * map "System RAM" with this mapping type will fail.
 */
void *memremap(resource_size_t offset, size_t size, unsigned long flags)
{
	int is_ram = region_intersects(offset, size, "System RAM");
	void *addr = NULL;

	if (is_ram == REGION_MIXED) {
		WARN_ONCE(1, "memremap attempted on mixed range %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

	/* Try all mapping types requested until one returns non-NULL */
	if (flags & MEMREMAP_WB) {
		flags &= ~MEMREMAP_WB;
		/*
		 * MEMREMAP_WB is special in that it can be satisifed
		 * from the direct map.  Some archs depend on the
		 * capability of memremap() to autodetect cases where
		 * the requested range is potentially in "System RAM"
		 */
		if (is_ram == REGION_INTERSECTS)
			addr = __va(offset);
		else
			addr = ioremap_cache(offset, size);
	}

	/*
	 * If we don't have a mapping yet and more request flags are
	 * pending then we will be attempting to establish a new virtual
	 * address mapping.  Enforce that this mapping is not aliasing
	 * "System RAM"
	 */
	if (!addr && is_ram == REGION_INTERSECTS && flags) {
		WARN_ONCE(1, "memremap attempted on ram %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

	if (!addr && (flags & MEMREMAP_WT)) {
		flags &= ~MEMREMAP_WT;
		addr = ioremap_wt(offset, size);
	}

	return addr;
}
EXPORT_SYMBOL(memremap);

void memunmap(void *addr)
{
	if (is_vmalloc_addr(addr))
		iounmap((void __iomem *) addr);
}
EXPORT_SYMBOL(memunmap);

static void devm_memremap_release(struct device *dev, void *res)
{
	memunmap(res);
}

static int devm_memremap_match(struct device *dev, void *res, void *match_data)
{
	return *(void **)res == match_data;
}

void *devm_memremap(struct device *dev, resource_size_t offset,
		size_t size, unsigned long flags)
{
	void **ptr, *addr;

	ptr = devres_alloc(devm_memremap_release, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return NULL;

	addr = memremap(offset, size, flags);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else
		devres_free(ptr);

	return addr;
}
EXPORT_SYMBOL(devm_memremap);

void devm_memunmap(struct device *dev, void *addr)
{
	WARN_ON(devres_destroy(dev, devm_memremap_release, devm_memremap_match,
			       addr));
	memunmap(addr);
}
EXPORT_SYMBOL(devm_memunmap);
