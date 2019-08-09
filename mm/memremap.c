/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2015 Intel Corporation. All rights reserved. */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kasan.h>
#include <linux/memory_hotplug.h>
#include <linux/mm.h>
#include <linux/pfn_t.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/types.h>
#include <linux/wait_bit.h>
#include <linux/xarray.h>

static DEFINE_XARRAY(pgmap_array);
#define SECTION_MASK ~((1UL << PA_SECTION_SHIFT) - 1)
#define SECTION_SIZE (1UL << PA_SECTION_SHIFT)

#ifdef CONFIG_DEV_PAGEMAP_OPS
DEFINE_STATIC_KEY_FALSE(devmap_managed_key);
EXPORT_SYMBOL(devmap_managed_key);
static atomic_t devmap_managed_enable;

static void devmap_managed_enable_put(void *data)
{
	if (atomic_dec_and_test(&devmap_managed_enable))
		static_branch_disable(&devmap_managed_key);
}

static int devmap_managed_enable_get(struct device *dev, struct dev_pagemap *pgmap)
{
	if (!pgmap->ops || !pgmap->ops->page_free) {
		WARN(1, "Missing page_free method\n");
		return -EINVAL;
	}

	if (atomic_inc_return(&devmap_managed_enable) == 1)
		static_branch_enable(&devmap_managed_key);
	return devm_add_action_or_reset(dev, devmap_managed_enable_put, NULL);
}
#else
static int devmap_managed_enable_get(struct device *dev, struct dev_pagemap *pgmap)
{
	return -EINVAL;
}
#endif /* CONFIG_DEV_PAGEMAP_OPS */

static void pgmap_array_delete(struct resource *res)
{
	xa_store_range(&pgmap_array, PHYS_PFN(res->start), PHYS_PFN(res->end),
			NULL, GFP_KERNEL);
	synchronize_rcu();
}

static unsigned long pfn_first(struct dev_pagemap *pgmap)
{
	return PHYS_PFN(pgmap->res.start) +
		vmem_altmap_offset(pgmap_altmap(pgmap));
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

static void dev_pagemap_kill(struct dev_pagemap *pgmap)
{
	if (pgmap->ops && pgmap->ops->kill)
		pgmap->ops->kill(pgmap);
	else
		percpu_ref_kill(pgmap->ref);
}

static void dev_pagemap_cleanup(struct dev_pagemap *pgmap)
{
	if (pgmap->ops && pgmap->ops->cleanup) {
		pgmap->ops->cleanup(pgmap);
	} else {
		wait_for_completion(&pgmap->done);
		percpu_ref_exit(pgmap->ref);
	}
}

static void devm_memremap_pages_release(void *data)
{
	struct dev_pagemap *pgmap = data;
	struct device *dev = pgmap->dev;
	struct resource *res = &pgmap->res;
	unsigned long pfn;
	int nid;

	dev_pagemap_kill(pgmap);
	for_each_device_pfn(pfn, pgmap)
		put_page(pfn_to_page(pfn));
	dev_pagemap_cleanup(pgmap);

	/* pages are dead and unused, undo the arch mapping */
	nid = page_to_nid(pfn_to_page(PHYS_PFN(res->start)));

	mem_hotplug_begin();
	if (pgmap->type == MEMORY_DEVICE_PRIVATE) {
		pfn = PHYS_PFN(res->start);
		__remove_pages(page_zone(pfn_to_page(pfn)), pfn,
				 PHYS_PFN(resource_size(res)), NULL);
	} else {
		arch_remove_memory(nid, res->start, resource_size(res),
				pgmap_altmap(pgmap));
		kasan_remove_zero_shadow(__va(res->start), resource_size(res));
	}
	mem_hotplug_done();

	untrack_pfn(NULL, PHYS_PFN(res->start), resource_size(res));
	pgmap_array_delete(res);
	dev_WARN_ONCE(dev, pgmap->altmap.alloc,
		      "%s: failed to free all reserved pages\n", __func__);
}

static void dev_pagemap_percpu_release(struct percpu_ref *ref)
{
	struct dev_pagemap *pgmap =
		container_of(ref, struct dev_pagemap, internal_ref);

	complete(&pgmap->done);
}

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
 * 4/ res is expected to be a host memory range that could feasibly be
 *    treated as a "System RAM" range, i.e. not a device mmio range, but
 *    this is not enforced.
 */
void *devm_memremap_pages(struct device *dev, struct dev_pagemap *pgmap)
{
	struct resource *res = &pgmap->res;
	struct dev_pagemap *conflict_pgmap;
	struct mhp_restrictions restrictions = {
		/*
		 * We do not want any optional features only our own memmap
		 */
		.altmap = pgmap_altmap(pgmap),
	};
	pgprot_t pgprot = PAGE_KERNEL;
	int error, nid, is_ram;
	bool need_devmap_managed = true;

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
		break;
	case MEMORY_DEVICE_FS_DAX:
		if (!IS_ENABLED(CONFIG_ZONE_DEVICE) ||
		    IS_ENABLED(CONFIG_FS_DAX_LIMITED)) {
			WARN(1, "File system DAX not supported\n");
			return ERR_PTR(-EINVAL);
		}
		break;
	case MEMORY_DEVICE_DEVDAX:
	case MEMORY_DEVICE_PCI_P2PDMA:
		need_devmap_managed = false;
		break;
	default:
		WARN(1, "Invalid pgmap type %d\n", pgmap->type);
		break;
	}

	if (!pgmap->ref) {
		if (pgmap->ops && (pgmap->ops->kill || pgmap->ops->cleanup))
			return ERR_PTR(-EINVAL);

		init_completion(&pgmap->done);
		error = percpu_ref_init(&pgmap->internal_ref,
				dev_pagemap_percpu_release, 0, GFP_KERNEL);
		if (error)
			return ERR_PTR(error);
		pgmap->ref = &pgmap->internal_ref;
	} else {
		if (!pgmap->ops || !pgmap->ops->kill || !pgmap->ops->cleanup) {
			WARN(1, "Missing reference count teardown definition\n");
			return ERR_PTR(-EINVAL);
		}
	}

	if (need_devmap_managed) {
		error = devmap_managed_enable_get(dev, pgmap);
		if (error)
			return ERR_PTR(error);
	}

	conflict_pgmap = get_dev_pagemap(PHYS_PFN(res->start), NULL);
	if (conflict_pgmap) {
		dev_WARN(dev, "Conflicting mapping in same section\n");
		put_dev_pagemap(conflict_pgmap);
		error = -ENOMEM;
		goto err_array;
	}

	conflict_pgmap = get_dev_pagemap(PHYS_PFN(res->end), NULL);
	if (conflict_pgmap) {
		dev_WARN(dev, "Conflicting mapping in same section\n");
		put_dev_pagemap(conflict_pgmap);
		error = -ENOMEM;
		goto err_array;
	}

	is_ram = region_intersects(res->start, resource_size(res),
		IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE);

	if (is_ram != REGION_DISJOINT) {
		WARN_ONCE(1, "%s attempted on %s region %pr\n", __func__,
				is_ram == REGION_MIXED ? "mixed" : "ram", res);
		error = -ENXIO;
		goto err_array;
	}

	pgmap->dev = dev;

	error = xa_err(xa_store_range(&pgmap_array, PHYS_PFN(res->start),
				PHYS_PFN(res->end), pgmap, GFP_KERNEL));
	if (error)
		goto err_array;

	nid = dev_to_node(dev);
	if (nid < 0)
		nid = numa_mem_id();

	error = track_pfn_remap(NULL, &pgprot, PHYS_PFN(res->start), 0,
			resource_size(res));
	if (error)
		goto err_pfn_remap;

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
	if (pgmap->type == MEMORY_DEVICE_PRIVATE) {
		error = add_pages(nid, PHYS_PFN(res->start),
				PHYS_PFN(resource_size(res)), &restrictions);
	} else {
		error = kasan_add_zero_shadow(__va(res->start), resource_size(res));
		if (error) {
			mem_hotplug_done();
			goto err_kasan;
		}

		error = arch_add_memory(nid, res->start, resource_size(res),
					&restrictions);
	}

	if (!error) {
		struct zone *zone;

		zone = &NODE_DATA(nid)->node_zones[ZONE_DEVICE];
		move_pfn_range_to_zone(zone, PHYS_PFN(res->start),
				PHYS_PFN(resource_size(res)), restrictions.altmap);
	}

	mem_hotplug_done();
	if (error)
		goto err_add_memory;

	/*
	 * Initialization of the pages has been deferred until now in order
	 * to allow us to do the work while not holding the hotplug lock.
	 */
	memmap_init_zone_device(&NODE_DATA(nid)->node_zones[ZONE_DEVICE],
				PHYS_PFN(res->start),
				PHYS_PFN(resource_size(res)), pgmap);
	percpu_ref_get_many(pgmap->ref, pfn_end(pgmap) - pfn_first(pgmap));

	error = devm_add_action_or_reset(dev, devm_memremap_pages_release,
			pgmap);
	if (error)
		return ERR_PTR(error);

	return __va(res->start);

 err_add_memory:
	kasan_remove_zero_shadow(__va(res->start), resource_size(res));
 err_kasan:
	untrack_pfn(NULL, PHYS_PFN(res->start), resource_size(res));
 err_pfn_remap:
	pgmap_array_delete(res);
 err_array:
	dev_pagemap_kill(pgmap);
	dev_pagemap_cleanup(pgmap);
	return ERR_PTR(error);
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
		if (phys >= pgmap->res.start && phys <= pgmap->res.end)
			return pgmap;
		put_dev_pagemap(pgmap);
	}

	/* fall back to slow path lookup */
	rcu_read_lock();
	pgmap = xa_load(&pgmap_array, PHYS_PFN(phys));
	if (pgmap && !percpu_ref_tryget_live(pgmap->ref))
		pgmap = NULL;
	rcu_read_unlock();

	return pgmap;
}
EXPORT_SYMBOL_GPL(get_dev_pagemap);

#ifdef CONFIG_DEV_PAGEMAP_OPS
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

		page->pgmap->ops->page_free(page);
	} else if (!count)
		__put_page(page);
}
EXPORT_SYMBOL(__put_devmap_managed_page);
#endif /* CONFIG_DEV_PAGEMAP_OPS */
