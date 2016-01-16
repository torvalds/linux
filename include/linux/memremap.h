#ifndef _LINUX_MEMREMAP_H_
#define _LINUX_MEMREMAP_H_
#include <linux/mm.h>

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
 * @dev: host device of the mapping for debug
 */
struct dev_pagemap {
	struct vmem_altmap *altmap;
	const struct resource *res;
	struct device *dev;
};

#ifdef CONFIG_ZONE_DEVICE
void *devm_memremap_pages(struct device *dev, struct resource *res,
		struct vmem_altmap *altmap);
struct dev_pagemap *find_dev_pagemap(resource_size_t phys);
#else
static inline void *devm_memremap_pages(struct device *dev,
		struct resource *res, struct vmem_altmap *altmap)
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
#endif /* _LINUX_MEMREMAP_H_ */
