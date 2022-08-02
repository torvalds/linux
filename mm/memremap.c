// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2015 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kasan.h>
#include <linux/memory_hotplug.h>
#include <linux/memremap.h>
#include <linux/pfn_t.h>
#include <linux/swap.h>
#include <linux/mmzone.h>
#include <linux/swapops.h>
#include <linux/types.h>
#include <linux/wait_bit.h>
#include <linux/xarray.h>
#include "internal.h"

static DEFINE_XARRAY(pgmap_array);

/*
 * The memremap() and memremap_pages() interfaces are alternately used
 * to map persistent memory namespaces. These interfaces place different
 * constraints on the alignment and size of the mapping (namespace).
 * memremap() can map individual PAGE_SIZE pages. memremap_pages() can
 * only map subsections (2MB), and at least one architecture (PowerPC)
 * the minimum mapping granularity of memremap_pages() is 16MB.
 *
 * The role of memremap_compat_align() is to communicate the minimum
 * arch supported alignment of a namespace such that it can freely
 * switch modes without violating the arch constraint. Namely, do not
 * allow a namespace to be PAGE_SIZE aligned since that namespace may be
 * reconfigured into a mode that requires SUBSECTION_SIZE alignment.
 */
#ifndef CONFIG_ARCH_HAS_MEMREMAP_COMPAT_ALIGN
unsigned long memremap_compat_align(void)
{
	return SUBSECTION_SIZE;
}
EXPORT_SYMBOL_GPL(memremap_compat_align);
#endif

#ifdef CONFIG_FS_DAX
DEFINE_STATIC_KEY_FALSE(devmap_managed_key);
EXPORT_SYMBOL(devmap_managed_key);

static void devmap_managed_enable_put(struct dev_pagemap *pgmap)
{
	if (pgmap->type == MEMORY_DEVICE_FS_DAX)
		static_branch_dec(&devmap_managed_key);
}

static void devmap_managed_enable_get(struct dev_pagemap *pgmap)
{
	if (pgmap->type == MEMORY_DEVICE_FS_DAX)
		static_branch_inc(&devmap_managed_key);
}
#else
static void devmap_managed_enable_get(struct dev_pagemap *pgmap)
{
}
static void devmap_managed_enable_put(struct dev_pagemap *pgmap)
{
}
#endif /* CONFIG_FS_DAX */

static void pgmap_array_delete(struct range *range)
{
	xa_store_range(&pgmap_array, PHYS_PFN(range->start), PHYS_PFN(range->end),
			NULL, GFP_KERNEL);
	synchronize_rcu();
}

static unsigned long pfn_first(struct dev_pagemap *pgmap, int range_id)
{
	struct range *range = &pgmap->ranges[range_id];
	unsigned long pfn = PHYS_PFN(range->start);

	if (range_id)
		return pfn;
	return pfn + vmem_altmap_offset(pgmap_altmap(pgmap));
}

bool pgmap_pfn_valid(struct dev_pagemap *pgmap, unsigned long pfn)
{
	int i;

	for (i = 0; i < pgmap->nr_range; i++) {
		struct range *range = &pgmap->ranges[i];

		if (pfn >= PHYS_PFN(range->start) &&
		    pfn <= PHYS_PFN(range->end))
			return pfn >= pfn_first(pgmap, i);
	}

	return false;
}

static unsigned long pfn_end(struct dev_pagemap *pgmap, int range_id)
{
	const struct range *range = &pgmap->ranges[range_id];

	return (range->start + range_len(range)) >> PAGE_SHIFT;
}

static unsigned long pfn_len(struct dev_pagemap *pgmap, unsigned long range_id)
{
	return (pfn_end(pgmap, range_id) -
		pfn_first(pgmap, range_id)) >> pgmap->vmemmap_shift;
}

static void pageunmap_range(struct dev_pagemap *pgmap, int range_id)
{
	struct range *range = &pgmap->ranges[range_id];
	struct page *first_page;

	/* make sure to access a memmap that was actually initialized */
	first_page = pfn_to_page(pfn_first(pgmap, range_id));

	/* pages are dead and unused, undo the arch mapping */
	mem_hotplug_begin();
	remove_pfn_range_from_zone(page_zone(first_page), PHYS_PFN(range->start),
				   PHYS_PFN(range_len(range)));
	if (pgmap->type == MEMORY_DEVICE_PRIVATE) {
		__remove_pages(PHYS_PFN(range->start),
			       PHYS_PFN(range_len(range)), NULL);
	} else {
		arch_remove_memory(range->start, range_len(range),
				pgmap_altmap(pgmap));
		kasan_remove_zero_shadow(__va(range->start), range_len(range));
	}
	mem_hotplug_done();

	untrack_pfn(NULL, PHYS_PFN(range->start), range_len(range));
	pgmap_array_delete(range);
}

void memunmap_pages(struct dev_pagemap *pgmap)
{
	int i;

	percpu_ref_kill(&pgmap->ref);
	for (i = 0; i < pgmap->nr_range; i++)
		percpu_ref_put_many(&pgmap->ref, pfn_len(pgmap, i));
	wait_for_completion(&pgmap->done);
	percpu_ref_exit(&pgmap->ref);

	for (i = 0; i < pgmap->nr_range; i++)
		pageunmap_range(pgmap, i);

	WARN_ONCE(pgmap->altmap.alloc, "failed to free all reserved pages\n");
	devmap_managed_enable_put(pgmap);
}
EXPORT_SYMBOL_GPL(memunmap_pages);

static void devm_memremap_pages_release(void *data)
{
	memunmap_pages(data);
}

static void dev_pagemap_percpu_release(struct percpu_ref *ref)
{
	struct dev_pagemap *pgmap = container_of(ref, struct dev_pagemap, ref);

	complete(&pgmap->done);
}

static int pagemap_range(struct dev_pagemap *pgmap, struct mhp_params *params,
		int range_id, int nid)
{
	const bool is_private = pgmap->type == MEMORY_DEVICE_PRIVATE;
	struct range *range = &pgmap->ranges[range_id];
	struct dev_pagemap *conflict_pgmap;
	int error, is_ram;

	if (WARN_ONCE(pgmap_altmap(pgmap) && range_id > 0,
				"altmap not supported for multiple ranges\n"))
		return -EINVAL;

	conflict_pgmap = get_dev_pagemap(PHYS_PFN(range->start), NULL);
	if (conflict_pgmap) {
		WARN(1, "Conflicting mapping in same section\n");
		put_dev_pagemap(conflict_pgmap);
		return -ENOMEM;
	}

	conflict_pgmap = get_dev_pagemap(PHYS_PFN(range->end), NULL);
	if (conflict_pgmap) {
		WARN(1, "Conflicting mapping in same section\n");
		put_dev_pagemap(conflict_pgmap);
		return -ENOMEM;
	}

	is_ram = region_intersects(range->start, range_len(range),
		IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE);

	if (is_ram != REGION_DISJOINT) {
		WARN_ONCE(1, "attempted on %s region %#llx-%#llx\n",
				is_ram == REGION_MIXED ? "mixed" : "ram",
				range->start, range->end);
		return -ENXIO;
	}

	error = xa_err(xa_store_range(&pgmap_array, PHYS_PFN(range->start),
				PHYS_PFN(range->end), pgmap, GFP_KERNEL));
	if (error)
		return error;

	if (nid < 0)
		nid = numa_mem_id();

	error = track_pfn_remap(NULL, &params->pgprot, PHYS_PFN(range->start), 0,
			range_len(range));
	if (error)
		goto err_pfn_remap;

	if (!mhp_range_allowed(range->start, range_len(range), !is_private)) {
		error = -EINVAL;
		goto err_kasan;
	}

	mem_hotplug_begin();

	/*
	 * For device private memory we call add_pages() as we only need to
	 * allocate and initialize struct page for the device memory. More-
	 * over the device memory is un-accessible thus we do not want to
	 * create a linear mapping for the memory like arch_add_memory()
	 * would do.
	 *
	 * For all other device memory types, which are accessible by
	 * the CPU, we do want the linear mapping and thus use
	 * arch_add_memory().
	 */
	if (is_private) {
		error = add_pages(nid, PHYS_PFN(range->start),
				PHYS_PFN(range_len(range)), params);
	} else {
		error = kasan_add_zero_shadow(__va(range->start), range_len(range));
		if (error) {
			mem_hotplug_done();
			goto err_kasan;
		}

		error = arch_add_memory(nid, range->start, range_len(range),
					params);
	}

	if (!error) {
		struct zone *zone;

		zone = &NODE_DATA(nid)->node_zones[ZONE_DEVICE];
		move_pfn_range_to_zone(zone, PHYS_PFN(range->start),
				PHYS_PFN(range_len(range)), params->altmap,
				MIGRATE_MOVABLE);
	}

	mem_hotplug_done();
	if (error)
		goto err_add_memory;

	/*
	 * Initialization of the pages has been deferred until now in order
	 * to allow us to do the work while not holding the hotplug lock.
	 */
	memmap_init_zone_device(&NODE_DATA(nid)->node_zones[ZONE_DEVICE],
				PHYS_PFN(range->start),
				PHYS_PFN(range_len(range)), pgmap);
	percpu_ref_get_many(&pgmap->ref, pfn_len(pgmap, range_id));
	return 0;

err_add_memory:
	if (!is_private)
		kasan_remove_zero_shadow(__va(range->start), range_len(range));
err_kasan:
	untrack_pfn(NULL, PHYS_PFN(range->start), range_len(range));
err_pfn_remap:
	pgmap_array_delete(range);
	return error;
}


/*
 * Not device managed version of dev_memremap_pages, undone by
 * memunmap_pages().  Please use dev_memremap_pages if you have a struct
 * device available.
 */
void *memremap_pages(struct dev_pagemap *pgmap, int nid)
{
	struct mhp_params params = {
		.altmap = pgmap_altmap(pgmap),
		.pgmap = pgmap,
		.pgprot = PAGE_KERNEL,
	};
	const int nr_range = pgmap->nr_range;
	int error, i;

	if (WARN_ONCE(!nr_range, "nr_range must be specified\n"))
		return ERR_PTR(-EINVAL);

	switch (pgmap->type) {
	case MEMORY_DEVICE_PRIVATE:
		if (!IS_ENABLED(CONFIG_DEVICE_PRIVATE)) {
			WARN(1, "Device private memory not supported\n");
			return ERR_PTR(-EINVAL);
		}
		if (!pgmap->ops || !pgmap->ops->migrate_to_ram) {
			WARN(1, "Missing migrate_to_ram method\n");
			return ERR_PTR(-EINVAL);
		}
		if (!pgmap->ops->page_free) {
			WARN(1, "Missing page_free method\n");
			return ERR_PTR(-EINVAL);
		}
		if (!pgmap->owner) {
			WARN(1, "Missing owner\n");
			return ERR_PTR(-EINVAL);
		}
		break;
	case MEMORY_DEVICE_FS_DAX:
		if (IS_ENABLED(CONFIG_FS_DAX_LIMITED)) {
			WARN(1, "File system DAX not supported\n");
			return ERR_PTR(-EINVAL);
		}
		break;
	case MEMORY_DEVICE_GENERIC:
		break;
	case MEMORY_DEVICE_PCI_P2PDMA:
		params.pgprot = pgprot_noncached(params.pgprot);
		break;
	default:
		WARN(1, "Invalid pgmap type %d\n", pgmap->type);
		break;
	}

	init_completion(&pgmap->done);
	error = percpu_ref_init(&pgmap->ref, dev_pagemap_percpu_release, 0,
				GFP_KERNEL);
	if (error)
		return ERR_PTR(error);

	devmap_managed_enable_get(pgmap);

	/*
	 * Clear the pgmap nr_range as it will be incremented for each
	 * successfully processed range. This communicates how many
	 * regions to unwind in the abort case.
	 */
	pgmap->nr_range = 0;
	error = 0;
	for (i = 0; i < nr_range; i++) {
		error = pagemap_range(pgmap, &params, i, nid);
		if (error)
			break;
		pgmap->nr_range++;
	}

	if (i < nr_range) {
		memunmap_pages(pgmap);
		pgmap->nr_range = nr_range;
		return ERR_PTR(error);
	}

	return __va(pgmap->ranges[0].start);
}
EXPORT_SYMBOL_GPL(memremap_pages);

/**
 * devm_memremap_pages - remap and provide memmap backing for the given resource
 * @dev: hosting device for @res
 * @pgmap: pointer to a struct dev_pagemap
 *
 * Notes:
 * 1/ At a minimum the res and type members of @pgmap must be initialized
 *    by the caller before passing it to this function
 *
 * 2/ The altmap field may optionally be initialized, in which case
 *    PGMAP_ALTMAP_VALID must be set in pgmap->flags.
 *
 * 3/ The ref field may optionally be provided, in which pgmap->ref must be
 *    'live' on entry and will be killed and reaped at
 *    devm_memremap_pages_release() time, or if this routine fails.
 *
 * 4/ range is expected to be a host memory range that could feasibly be
 *    treated as a "System RAM" range, i.e. not a device mmio range, but
 *    this is not enforced.
 */
void *devm_memremap_pages(struct device *dev, struct dev_pagemap *pgmap)
{
	int error;
	void *ret;

	ret = memremap_pages(pgmap, dev_to_node(dev));
	if (IS_ERR(ret))
		return ret;

	error = devm_add_action_or_reset(dev, devm_memremap_pages_release,
			pgmap);
	if (error)
		return ERR_PTR(error);
	return ret;
}
EXPORT_SYMBOL_GPL(devm_memremap_pages);

void devm_memunmap_pages(struct device *dev, struct dev_pagemap *pgmap)
{
	devm_release_action(dev, devm_memremap_pages_release, pgmap);
}
EXPORT_SYMBOL_GPL(devm_memunmap_pages);

unsigned long vmem_altmap_offset(struct vmem_altmap *altmap)
{
	/* number of pfns from base where pfn_to_page() is valid */
	if (altmap)
		return altmap->reserve + altmap->free;
	return 0;
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
		if (phys >= pgmap->range.start && phys <= pgmap->range.end)
			return pgmap;
		put_dev_pagemap(pgmap);
	}

	/* fall back to slow path lookup */
	rcu_read_lock();
	pgmap = xa_load(&pgmap_array, PHYS_PFN(phys));
	if (pgmap && !percpu_ref_tryget_live(&pgmap->ref))
		pgmap = NULL;
	rcu_read_unlock();

	return pgmap;
}
EXPORT_SYMBOL_GPL(get_dev_pagemap);

void free_zone_device_page(struct page *page)
{
	if (WARN_ON_ONCE(!page->pgmap->ops || !page->pgmap->ops->page_free))
		return;

	mem_cgroup_uncharge(page_folio(page));

	/*
	 * Note: we don't expect anonymous compound pages yet. Once supported
	 * and we could PTE-map them similar to THP, we'd have to clear
	 * PG_anon_exclusive on all tail pages.
	 */
	VM_BUG_ON_PAGE(PageAnon(page) && PageCompound(page), page);
	if (PageAnon(page))
		__ClearPageAnonExclusive(page);

	/*
	 * When a device managed page is freed, the page->mapping field
	 * may still contain a (stale) mapping value. For example, the
	 * lower bits of page->mapping may still identify the page as an
	 * anonymous page. Ultimately, this entire field is just stale
	 * and wrong, and it will cause errors if not cleared.  One
	 * example is:
	 *
	 *  migrate_vma_pages()
	 *    migrate_vma_insert_page()
	 *      page_add_new_anon_rmap()
	 *        __page_set_anon_rmap()
	 *          ...checks page->mapping, via PageAnon(page) call,
	 *            and incorrectly concludes that the page is an
	 *            anonymous page. Therefore, it incorrectly,
	 *            silently fails to set up the new anon rmap.
	 *
	 * For other types of ZONE_DEVICE pages, migration is either
	 * handled differently or not done at all, so there is no need
	 * to clear page->mapping.
	 */
	page->mapping = NULL;
	page->pgmap->ops->page_free(page);

	/*
	 * Reset the page count to 1 to prepare for handing out the page again.
	 */
	set_page_count(page, 1);
}

#ifdef CONFIG_FS_DAX
bool __put_devmap_managed_page(struct page *page)
{
	if (page->pgmap->type != MEMORY_DEVICE_FS_DAX)
		return false;

	/*
	 * fsdax page refcounts are 1-based, rather than 0-based: if
	 * refcount is 1, then the page is free and the refcount is
	 * stable because nobody holds a reference on the page.
	 */
	if (page_ref_dec_return(page) == 1)
		wake_up_var(&page->_refcount);
	return true;
}
EXPORT_SYMBOL(__put_devmap_managed_page);
#endif /* CONFIG_FS_DAX */
