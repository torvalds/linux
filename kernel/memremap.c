/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2015 Intel Corporation. All rights reserved. */
#include <linux/radix-tree.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/pfn_t.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/memory_hotplug.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/wait_bit.h>

static DEFINE_MUTEX(pgmap_lock);
static RADIX_TREE(pgmap_radix, GFP_KERNEL);
#define SECTION_MASK ~((1UL << PA_SECTION_SHIFT) - 1)
#define SECTION_SIZE (1UL << PA_SECTION_SHIFT)

static unsigned long order_at(struct resource *res, unsigned long pgoff)
{
	unsigned long phys_pgoff = PHYS_PFN(res->start) + pgoff;
	unsigned long nr_pages, mask;

	nr_pages = PHYS_PFN(resource_size(res));
	if (nr_pages == pgoff)
		return ULONG_MAX;

	/*
	 * What is the largest aligned power-of-2 range available from
	 * this resource pgoff to the end of the resource range,
	 * considering the alignment of the current pgoff?
	 */
	mask = phys_pgoff | rounddown_pow_of_two(nr_pages - pgoff);
	if (!mask)
		return ULONG_MAX;

	return find_first_bit(&mask, BITS_PER_LONG);
}

#define foreach_order_pgoff(res, order, pgoff) \
	for (pgoff = 0, order = order_at((res), pgoff); order < ULONG_MAX; \
			pgoff += 1UL << order, order = order_at((res), pgoff))

#if IS_ENABLED(CONFIG_DEVICE_PRIVATE)
int device_private_entry_fault(struct vm_area_struct *vma,
		       unsigned long addr,
		       swp_entry_t entry,
		       unsigned int flags,
		       pmd_t *pmdp)
{
	struct page *page = device_private_entry_to_page(entry);

	/*
	 * The page_fault() callback must migrate page back to system memory
	 * so that CPU can access it. This might fail for various reasons
	 * (device issue, device was unsafely unplugged, ...). When such
	 * error conditions happen, the callback must return VM_FAULT_SIGBUS.
	 *
	 * Note that because memory cgroup charges are accounted to the device
	 * memory, this should never fail because of memory restrictions (but
	 * allocation of regular system page might still fail because we are
	 * out of memory).
	 *
	 * There is a more in-depth description of what that callback can and
	 * cannot do, in include/linux/memremap.h
	 */
	return page->pgmap->page_fault(vma, addr, page, flags, pmdp);
}
EXPORT_SYMBOL(device_private_entry_fault);
#endif /* CONFIG_DEVICE_PRIVATE */

static void pgmap_radix_release(struct resource *res, unsigned long end_pgoff)
{
	unsigned long pgoff, order;

	mutex_lock(&pgmap_lock);
	foreach_order_pgoff(res, order, pgoff) {
		if (pgoff >= end_pgoff)
			break;
		radix_tree_delete(&pgmap_radix, PHYS_PFN(res->start) + pgoff);
	}
	mutex_unlock(&pgmap_lock);

	synchronize_rcu();
}

static unsigned long pfn_first(struct dev_pagemap *pgmap)
{
	const struct resource *res = &pgmap->res;
	struct vmem_altmap *altmap = &pgmap->altmap;
	unsigned long pfn;

	pfn = res->start >> PAGE_SHIFT;
	if (pgmap->altmap_valid)
		pfn += vmem_altmap_offset(altmap);
	return pfn;
}

static unsigned long pfn_end(struct dev_pagemap *pgmap)
{
	const struct resource *res = &pgmap->res;

	return (res->start + resource_size(res)) >> PAGE_SHIFT;
}

static unsigned long pfn_next(unsigned long pfn)
{
	if (pfn % 1024 == 0)
		cond_resched();
	return pfn + 1;
}

#define for_each_device_pfn(pfn, map) \
	for (pfn = pfn_first(map); pfn < pfn_end(map); pfn = pfn_next(pfn))

static void devm_memremap_pages_release(void *data)
{
	struct dev_pagemap *pgmap = data;
	struct device *dev = pgmap->dev;
	struct resource *res = &pgmap->res;
	resource_size_t align_start, align_size;
	unsigned long pfn;

	for_each_device_pfn(pfn, pgmap)
		put_page(pfn_to_page(pfn));

	if (percpu_ref_tryget_live(pgmap->ref)) {
		dev_WARN(dev, "%s: page mapping is still live!\n", __func__);
		percpu_ref_put(pgmap->ref);
	}

	/* pages are dead and unused, undo the arch mapping */
	align_start = res->start & ~(SECTION_SIZE - 1);
	align_size = ALIGN(res->start + resource_size(res), SECTION_SIZE)
		- align_start;

	mem_hotplug_begin();
	arch_remove_memory(align_start, align_size, pgmap->altmap_valid ?
			&pgmap->altmap : NULL);
	mem_hotplug_done();

	untrack_pfn(NULL, PHYS_PFN(align_start), align_size);
	pgmap_radix_release(res, -1);
	dev_WARN_ONCE(dev, pgmap->altmap.alloc,
		      "%s: failed to free all reserved pages\n", __func__);
}

/**
 * devm_memremap_pages - remap and provide memmap backing for the given resource
 * @dev: hosting device for @res
 * @pgmap: pointer to a struct dev_pgmap
 *
 * Notes:
 * 1/ At a minimum the res, ref and type members of @pgmap must be initialized
 *    by the caller before passing it to this function
 *
 * 2/ The altmap field may optionally be initialized, in which case altmap_valid
 *    must be set to true
 *
 * 3/ pgmap.ref must be 'live' on entry and 'dead' before devm_memunmap_pages()
 *    time (or devm release event). The expected order of events is that ref has
 *    been through percpu_ref_kill() before devm_memremap_pages_release(). The
 *    wait for the completion of all references being dropped and
 *    percpu_ref_exit() must occur after devm_memremap_pages_release().
 *
 * 4/ res is expected to be a host memory range that could feasibly be
 *    treated as a "System RAM" range, i.e. not a device mmio range, but
 *    this is not enforced.
 */
void *devm_memremap_pages(struct device *dev, struct dev_pagemap *pgmap)
{
	resource_size_t align_start, align_size, align_end;
	struct vmem_altmap *altmap = pgmap->altmap_valid ?
			&pgmap->altmap : NULL;
	struct resource *res = &pgmap->res;
	unsigned long pfn, pgoff, order;
	pgprot_t pgprot = PAGE_KERNEL;
	int error, nid, is_ram;

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

	if (!pgmap->ref)
		return ERR_PTR(-EINVAL);

	pgmap->dev = dev;

	mutex_lock(&pgmap_lock);
	error = 0;
	align_end = align_start + align_size - 1;

	foreach_order_pgoff(res, order, pgoff) {
		error = __radix_tree_insert(&pgmap_radix,
				PHYS_PFN(res->start) + pgoff, order, pgmap);
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
	error = arch_add_memory(nid, align_start, align_size, altmap, false);
	if (!error)
		move_pfn_range_to_zone(&NODE_DATA(nid)->node_zones[ZONE_DEVICE],
					align_start >> PAGE_SHIFT,
					align_size >> PAGE_SHIFT, altmap);
	mem_hotplug_done();
	if (error)
		goto err_add_memory;

	for_each_device_pfn(pfn, pgmap) {
		struct page *page = pfn_to_page(pfn);

		/*
		 * ZONE_DEVICE pages union ->lru with a ->pgmap back
		 * pointer.  It is a bug if a ZONE_DEVICE page is ever
		 * freed or placed on a driver-private list.  Seed the
		 * storage with LIST_POISON* values.
		 */
		list_del(&page->lru);
		page->pgmap = pgmap;
		percpu_ref_get(pgmap->ref);
	}

	devm_add_action(dev, devm_memremap_pages_release, pgmap);

	return __va(res->start);

 err_add_memory:
	untrack_pfn(NULL, PHYS_PFN(align_start), align_size);
 err_pfn_remap:
 err_radix:
	pgmap_radix_release(res, pgoff);
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

/**
 * get_dev_pagemap() - take a new live reference on the dev_pagemap for @pfn
 * @pfn: page frame number to lookup page_map
 * @pgmap: optional known pgmap that already has a reference
 *
 * If @pgmap is non-NULL and covers @pfn it will be returned as-is.  If @pgmap
 * is non-NULL but does not cover @pfn the reference to it will be released.
 */
struct dev_pagemap *get_dev_pagemap(unsigned long pfn,
		struct dev_pagemap *pgmap)
{
	resource_size_t phys = PFN_PHYS(pfn);

	/*
	 * In the cached case we're already holding a live reference.
	 */
	if (pgmap) {
		if (phys >= pgmap->res.start && phys <= pgmap->res.end)
			return pgmap;
		put_dev_pagemap(pgmap);
	}

	/* fall back to slow path lookup */
	rcu_read_lock();
	pgmap = radix_tree_lookup(&pgmap_radix, PHYS_PFN(phys));
	if (pgmap && !percpu_ref_tryget_live(pgmap->ref))
		pgmap = NULL;
	rcu_read_unlock();

	return pgmap;
}
EXPORT_SYMBOL_GPL(get_dev_pagemap);

#ifdef CONFIG_DEV_PAGEMAP_OPS
DEFINE_STATIC_KEY_FALSE(devmap_managed_key);
EXPORT_SYMBOL_GPL(devmap_managed_key);
static atomic_t devmap_enable;

/*
 * Toggle the static key for ->page_free() callbacks when dev_pagemap
 * pages go idle.
 */
void dev_pagemap_get_ops(void)
{
	if (atomic_inc_return(&devmap_enable) == 1)
		static_branch_enable(&devmap_managed_key);
}
EXPORT_SYMBOL_GPL(dev_pagemap_get_ops);

void dev_pagemap_put_ops(void)
{
	if (atomic_dec_and_test(&devmap_enable))
		static_branch_disable(&devmap_managed_key);
}
EXPORT_SYMBOL_GPL(dev_pagemap_put_ops);

void __put_devmap_managed_page(struct page *page)
{
	int count = page_ref_dec_return(page);

	/*
	 * If refcount is 1 then page is freed and refcount is stable as nobody
	 * holds a reference on the page.
	 */
	if (count == 1) {
		/* Clear Active bit in case of parallel mark_page_accessed */
		__ClearPageActive(page);
		__ClearPageWaiters(page);

		mem_cgroup_uncharge(page);

		page->pgmap->page_free(page, page->pgmap->data);
	} else if (!count)
		__put_page(page);
}
EXPORT_SYMBOL_GPL(__put_devmap_managed_page);
#endif /* CONFIG_DEV_PAGEMAP_OPS */
