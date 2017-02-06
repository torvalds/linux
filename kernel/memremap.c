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
#include <linux/radix-tree.h>
#include <linux/memremap.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/pfn_t.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/memory_hotplug.h>

#ifndef ioremap_cache
/* temporary while we convert existing ioremap_cache users to memremap */
__weak void __iomem *ioremap_cache(resource_size_t offset, unsigned long size)
{
	return ioremap(offset, size);
}
#endif

#ifndef arch_memremap_wb
static void *arch_memremap_wb(resource_size_t offset, unsigned long size)
{
	return (__force void *)ioremap_cache(offset, size);
}
#endif

static void *try_ram_remap(resource_size_t offset, size_t size)
{
	unsigned long pfn = PHYS_PFN(offset);

	/* In the simple case just return the existing linear address */
	if (pfn_valid(pfn) && !PageHighMem(pfn_to_page(pfn)))
		return __va(offset);
	return NULL; /* fallback to arch_memremap_wb */
}

/**
 * memremap() - remap an iomem_resource as cacheable memory
 * @offset: iomem resource start address
 * @size: size of remap
 * @flags: any of MEMREMAP_WB, MEMREMAP_WT and MEMREMAP_WC
 *
 * memremap() is "ioremap" for cases where it is known that the resource
 * being mapped does not have i/o side effects and the __iomem
 * annotation is not applicable. In the case of multiple flags, the different
 * mapping types will be attempted in the order listed below until one of
 * them succeeds.
 *
 * MEMREMAP_WB - matches the default mapping for System RAM on
 * the architecture.  This is usually a read-allocate write-back cache.
 * Morever, if MEMREMAP_WB is specified and the requested remap region is RAM
 * memremap() will bypass establishing a new mapping and instead return
 * a pointer into the direct map.
 *
 * MEMREMAP_WT - establish a mapping whereby writes either bypass the
 * cache or are written through to memory and never exist in a
 * cache-dirty state with respect to program visibility.  Attempts to
 * map System RAM with this mapping type will fail.
 *
 * MEMREMAP_WC - establish a writecombine mapping, whereby writes may
 * be coalesced together (e.g. in the CPU's write buffers), but is otherwise
 * uncached. Attempts to map System RAM with this mapping type will fail.
 */
void *memremap(resource_size_t offset, size_t size, unsigned long flags)
{
	int is_ram = region_intersects(offset, size,
				       IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE);
	void *addr = NULL;

	if (!flags)
		return NULL;

	if (is_ram == REGION_MIXED) {
		WARN_ONCE(1, "memremap attempted on mixed range %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

	/* Try all mapping types requested until one returns non-NULL */
	if (flags & MEMREMAP_WB) {
		/*
		 * MEMREMAP_WB is special in that it can be satisifed
		 * from the direct map.  Some archs depend on the
		 * capability of memremap() to autodetect cases where
		 * the requested range is potentially in System RAM.
		 */
		if (is_ram == REGION_INTERSECTS)
			addr = try_ram_remap(offset, size);
		if (!addr)
			addr = arch_memremap_wb(offset, size);
	}

	/*
	 * If we don't have a mapping yet and other request flags are
	 * present then we will be attempting to establish a new virtual
	 * address mapping.  Enforce that this mapping is not aliasing
	 * System RAM.
	 */
	if (!addr && is_ram == REGION_INTERSECTS && flags != MEMREMAP_WB) {
		WARN_ONCE(1, "memremap attempted on ram %pa size: %#lx\n",
				&offset, (unsigned long) size);
		return NULL;
	}

	if (!addr && (flags & MEMREMAP_WT))
		addr = ioremap_wt(offset, size);

	if (!addr && (flags & MEMREMAP_WC))
		addr = ioremap_wc(offset, size);

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
	memunmap(*(void **)res);
}

static int devm_memremap_match(struct device *dev, void *res, void *match_data)
{
	return *(void **)res == match_data;
}

void *devm_memremap(struct device *dev, resource_size_t offset,
		size_t size, unsigned long flags)
{
	void **ptr, *addr;

	ptr = devres_alloc_node(devm_memremap_release, sizeof(*ptr), GFP_KERNEL,
			dev_to_node(dev));
	if (!ptr)
		return ERR_PTR(-ENOMEM);

	addr = memremap(offset, size, flags);
	if (addr) {
		*ptr = addr;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
		return ERR_PTR(-ENXIO);
	}

	return addr;
}
EXPORT_SYMBOL(devm_memremap);

void devm_memunmap(struct device *dev, void *addr)
{
	WARN_ON(devres_release(dev, devm_memremap_release,
				devm_memremap_match, addr));
}
EXPORT_SYMBOL(devm_memunmap);

#ifdef CONFIG_ZONE_DEVICE
static DEFINE_MUTEX(pgmap_lock);
static RADIX_TREE(pgmap_radix, GFP_KERNEL);
#define SECTION_MASK ~((1UL << PA_SECTION_SHIFT) - 1)
#define SECTION_SIZE (1UL << PA_SECTION_SHIFT)

struct page_map {
	struct resource res;
	struct percpu_ref *ref;
	struct dev_pagemap pgmap;
	struct vmem_altmap altmap;
};

void get_zone_device_page(struct page *page)
{
	percpu_ref_get(page->pgmap->ref);
}
EXPORT_SYMBOL(get_zone_device_page);

void put_zone_device_page(struct page *page)
{
	put_dev_pagemap(page->pgmap);
}
EXPORT_SYMBOL(put_zone_device_page);

static void pgmap_radix_release(struct resource *res)
{
	resource_size_t key, align_start, align_size, align_end;

	align_start = res->start & ~(SECTION_SIZE - 1);
	align_size = ALIGN(resource_size(res), SECTION_SIZE);
	align_end = align_start + align_size - 1;

	mutex_lock(&pgmap_lock);
	for (key = res->start; key <= res->end; key += SECTION_SIZE)
		radix_tree_delete(&pgmap_radix, key >> PA_SECTION_SHIFT);
	mutex_unlock(&pgmap_lock);
}

static unsigned long pfn_first(struct page_map *page_map)
{
	struct dev_pagemap *pgmap = &page_map->pgmap;
	const struct resource *res = &page_map->res;
	struct vmem_altmap *altmap = pgmap->altmap;
	unsigned long pfn;

	pfn = res->start >> PAGE_SHIFT;
	if (altmap)
		pfn += vmem_altmap_offset(altmap);
	return pfn;
}

static unsigned long pfn_end(struct page_map *page_map)
{
	const struct resource *res = &page_map->res;

	return (res->start + resource_size(res)) >> PAGE_SHIFT;
}

#define for_each_device_pfn(pfn, map) \
	for (pfn = pfn_first(map); pfn < pfn_end(map); pfn++)

static void devm_memremap_pages_release(struct device *dev, void *data)
{
	struct page_map *page_map = data;
	struct resource *res = &page_map->res;
	resource_size_t align_start, align_size;
	struct dev_pagemap *pgmap = &page_map->pgmap;

	if (percpu_ref_tryget_live(pgmap->ref)) {
		dev_WARN(dev, "%s: page mapping is still live!\n", __func__);
		percpu_ref_put(pgmap->ref);
	}

	/* pages are dead and unused, undo the arch mapping */
	align_start = res->start & ~(SECTION_SIZE - 1);
	align_size = ALIGN(resource_size(res), SECTION_SIZE);
	mem_hotplug_begin();
	arch_remove_memory(align_start, align_size);
	mem_hotplug_done();
	untrack_pfn(NULL, PHYS_PFN(align_start), align_size);
	pgmap_radix_release(res);
	dev_WARN_ONCE(dev, pgmap->altmap && pgmap->altmap->alloc,
			"%s: failed to free all reserved pages\n", __func__);
}

/* assumes rcu_read_lock() held at entry */
struct dev_pagemap *find_dev_pagemap(resource_size_t phys)
{
	struct page_map *page_map;

	WARN_ON_ONCE(!rcu_read_lock_held());

	page_map = radix_tree_lookup(&pgmap_radix, phys >> PA_SECTION_SHIFT);
	return page_map ? &page_map->pgmap : NULL;
}

/**
 * devm_memremap_pages - remap and provide memmap backing for the given resource
 * @dev: hosting device for @res
 * @res: "host memory" address range
 * @ref: a live per-cpu reference count
 * @altmap: optional descriptor for allocating the memmap from @res
 *
 * Notes:
 * 1/ @ref must be 'live' on entry and 'dead' before devm_memunmap_pages() time
 *    (or devm release event).
 *
 * 2/ @res is expected to be a host memory range that could feasibly be
 *    treated as a "System RAM" range, i.e. not a device mmio range, but
 *    this is not enforced.
 */
void *devm_memremap_pages(struct device *dev, struct resource *res,
		struct percpu_ref *ref, struct vmem_altmap *altmap)
{
	resource_size_t key, align_start, align_size, align_end;
	pgprot_t pgprot = PAGE_KERNEL;
	struct dev_pagemap *pgmap;
	struct page_map *page_map;
	int error, nid, is_ram;
	unsigned long pfn;

	align_start = res->start & ~(SECTION_SIZE - 1);
	align_size = ALIGN(res->start + resource_size(res), SECTION_SIZE)
		- align_start;
	is_ram = region_intersects(align_start, align_size,
		IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE);

	if (is_ram == REGION_MIXED) {
		WARN_ONCE(1, "%s attempted on mixed region %pr\n",
				__func__, res);
		return ERR_PTR(-ENXIO);
	}

	if (is_ram == REGION_INTERSECTS)
		return __va(res->start);

	if (!ref)
		return ERR_PTR(-EINVAL);

	page_map = devres_alloc_node(devm_memremap_pages_release,
			sizeof(*page_map), GFP_KERNEL, dev_to_node(dev));
	if (!page_map)
		return ERR_PTR(-ENOMEM);
	pgmap = &page_map->pgmap;

	memcpy(&page_map->res, res, sizeof(*res));

	pgmap->dev = dev;
	if (altmap) {
		memcpy(&page_map->altmap, altmap, sizeof(*altmap));
		pgmap->altmap = &page_map->altmap;
	}
	pgmap->ref = ref;
	pgmap->res = &page_map->res;

	mutex_lock(&pgmap_lock);
	error = 0;
	align_end = align_start + align_size - 1;
	for (key = align_start; key <= align_end; key += SECTION_SIZE) {
		struct dev_pagemap *dup;

		rcu_read_lock();
		dup = find_dev_pagemap(key);
		rcu_read_unlock();
		if (dup) {
			dev_err(dev, "%s: %pr collides with mapping for %s\n",
					__func__, res, dev_name(dup->dev));
			error = -EBUSY;
			break;
		}
		error = radix_tree_insert(&pgmap_radix, key >> PA_SECTION_SHIFT,
				page_map);
		if (error) {
			dev_err(dev, "%s: failed: %d\n", __func__, error);
			break;
		}
	}
	mutex_unlock(&pgmap_lock);
	if (error)
		goto err_radix;

	nid = dev_to_node(dev);
	if (nid < 0)
		nid = numa_mem_id();

	error = track_pfn_remap(NULL, &pgprot, PHYS_PFN(align_start), 0,
			align_size);
	if (error)
		goto err_pfn_remap;

	mem_hotplug_begin();
	error = arch_add_memory(nid, align_start, align_size, true);
	mem_hotplug_done();
	if (error)
		goto err_add_memory;

	for_each_device_pfn(pfn, page_map) {
		struct page *page = pfn_to_page(pfn);

		/*
		 * ZONE_DEVICE pages union ->lru with a ->pgmap back
		 * pointer.  It is a bug if a ZONE_DEVICE page is ever
		 * freed or placed on a driver-private list.  Seed the
		 * storage with LIST_POISON* values.
		 */
		list_del(&page->lru);
		page->pgmap = pgmap;
	}
	devres_add(dev, page_map);
	return __va(res->start);

 err_add_memory:
	untrack_pfn(NULL, PHYS_PFN(align_start), align_size);
 err_pfn_remap:
 err_radix:
	pgmap_radix_release(res);
	devres_free(page_map);
	return ERR_PTR(error);
}
EXPORT_SYMBOL(devm_memremap_pages);

unsigned long vmem_altmap_offset(struct vmem_altmap *altmap)
{
	/* number of pfns from base where pfn_to_page() is valid */
	return altmap->reserve + altmap->free;
}

void vmem_altmap_free(struct vmem_altmap *altmap, unsigned long nr_pfns)
{
	altmap->alloc -= nr_pfns;
}

struct vmem_altmap *to_vmem_altmap(unsigned long memmap_start)
{
	/*
	 * 'memmap_start' is the virtual address for the first "struct
	 * page" in this range of the vmemmap array.  In the case of
	 * CONFIG_SPARSEMEM_VMEMMAP a page_to_pfn conversion is simple
	 * pointer arithmetic, so we can perform this to_vmem_altmap()
	 * conversion without concern for the initialization state of
	 * the struct page fields.
	 */
	struct page *page = (struct page *) memmap_start;
	struct dev_pagemap *pgmap;

	/*
	 * Unconditionally retrieve a dev_pagemap associated with the
	 * given physical address, this is only for use in the
	 * arch_{add|remove}_memory() for setting up and tearing down
	 * the memmap.
	 */
	rcu_read_lock();
	pgmap = find_dev_pagemap(__pfn_to_phys(page_to_pfn(page)));
	rcu_read_unlock();

	return pgmap ? pgmap->altmap : NULL;
}
#endif /* CONFIG_ZONE_DEVICE */
