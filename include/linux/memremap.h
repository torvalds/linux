#ifndef _LINUX_MEMREMAP_H_
#define _LINUX_MEMREMAP_H_
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/percpu-refcount.h>

struct resource;
struct device;

/**
 * struct vmem_altmap - pre-allocated storage for vmemmap_populate
 * @base_pfn: base of the entire dev_pagemap mapping
 * @reserve: pages mapped, but reserved for driver use (relative to @base)
 * @free: free pages set aside in the mapping for memmap storage
 * @align: pages reserved to meet allocation alignments
 * @alloc: track pages consumed, private to vmemmap_populate()
 */
struct vmem_altmap {
	const unsigned long base_pfn;
	const unsigned long reserve;
	unsigned long free;
	unsigned long align;
	unsigned long alloc;
};

unsigned long vmem_altmap_offset(struct vmem_altmap *altmap);
void vmem_altmap_free(struct vmem_altmap *altmap, unsigned long nr_pfns);

#if defined(CONFIG_SPARSEMEM_VMEMMAP) && defined(CONFIG_ZONE_DEVICE)
struct vmem_altmap *to_vmem_altmap(unsigned long memmap_start);
#else
static inline struct vmem_altmap *to_vmem_altmap(unsigned long memmap_start)
{
	return NULL;
}
#endif

/**
 * struct dev_pagemap - metadata for ZONE_DEVICE mappings
 * @altmap: pre-allocated/reserved memory for vmemmap allocations
 * @res: physical address range covered by @ref
 * @ref: reference count that pins the devm_memremap_pages() mapping
 * @dev: host device of the mapping for debug
 */
struct dev_pagemap {
	struct vmem_altmap *altmap;
	const struct resource *res;
	struct percpu_ref *ref;
	struct device *dev;
};

#ifdef CONFIG_ZONE_DEVICE
void *devm_memremap_pages(struct device *dev, struct resource *res,
		struct percpu_ref *ref, struct vmem_altmap *altmap);
struct dev_pagemap *find_dev_pagemap(resource_size_t phys);
#else
static inline void *devm_memremap_pages(struct device *dev,
		struct resource *res, struct percpu_ref *ref,
		struct vmem_altmap *altmap)
{
	/*
	 * Fail attempts to call devm_memremap_pages() without
	 * ZONE_DEVICE support enabled, this requires callers to fall
	 * back to plain devm_memremap() based on config
	 */
	WARN_ON_ONCE(1);
	return ERR_PTR(-ENXIO);
}

static inline struct dev_pagemap *find_dev_pagemap(resource_size_t phys)
{
	return NULL;
}
#endif

/**
 * get_dev_pagemap() - take a new live reference on the dev_pagemap for @pfn
 * @pfn: page frame number to lookup page_map
 * @pgmap: optional known pgmap that already has a reference
 *
 * @pgmap allows the overhead of a lookup to be bypassed when @pfn lands in the
 * same mapping.
 */
static inline struct dev_pagemap *get_dev_pagemap(unsigned long pfn,
		struct dev_pagemap *pgmap)
{
	const struct resource *res = pgmap ? pgmap->res : NULL;
	resource_size_t phys = PFN_PHYS(pfn);

	/*
	 * In the cached case we're already holding a live reference so
	 * we can simply do a blind increment
	 */
	if (res && phys >= res->start && phys <= res->end) {
		percpu_ref_get(pgmap->ref);
		return pgmap;
	}

	/* fall back to slow path lookup */
	rcu_read_lock();
	pgmap = find_dev_pagemap(phys);
	if (pgmap && !percpu_ref_tryget_live(pgmap->ref))
		pgmap = NULL;
	rcu_read_unlock();

	return pgmap;
}

static inline void put_dev_pagemap(struct dev_pagemap *pgmap)
{
	if (pgmap)
		percpu_ref_put(pgmap->ref);
}
#endif /* _LINUX_MEMREMAP_H_ */
