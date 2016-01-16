#ifndef _LINUX_MEMREMAP_H_
#define _LINUX_MEMREMAP_H_
#include <linux/mm.h>

struct resource;
struct device;
/**
 * struct dev_pagemap - metadata for ZONE_DEVICE mappings
 * @dev: host device of the mapping for debug
 */
struct dev_pagemap {
	/* TODO: vmem_altmap and percpu_ref count */
	struct device *dev;
};

#ifdef CONFIG_ZONE_DEVICE
void *devm_memremap_pages(struct device *dev, struct resource *res);
struct dev_pagemap *find_dev_pagemap(resource_size_t phys);
#else
static inline void *devm_memremap_pages(struct device *dev,
		struct resource *res)
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
