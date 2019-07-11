/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMREMAP_H_
#define _LINUX_MEMREMAP_H_
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

/*
 * Specialize ZONE_DEVICE memory into multiple types each having differents
 * usage.
 *
 * MEMORY_DEVICE_PRIVATE:
 * Device memory that is not directly addressable by the CPU: CPU can neither
 * read nor write private memory. In this case, we do still have struct pages
 * backing the device memory. Doing so simplifies the implementation, but it is
 * important to remember that there are certain points at which the struct page
 * must be treated as an opaque object, rather than a "normal" struct page.
 *
 * A more complete discussion of unaddressable memory may be found in
 * include/linux/hmm.h and Documentation/vm/hmm.rst.
 *
 * MEMORY_DEVICE_PUBLIC:
 * Device memory that is cache coherent from device and CPU point of view. This
 * is use on platform that have an advance system bus (like CAPI or CCIX). A
 * driver can hotplug the device memory using ZONE_DEVICE and with that memory
 * type. Any page of a process can be migrated to such memory. However no one
 * should be allow to pin such memory so that it can always be evicted.
 *
 * MEMORY_DEVICE_FS_DAX:
 * Host memory that has similar access semantics as System RAM i.e. DMA
 * coherent and supports page pinning. In support of coordinating page
 * pinning vs other operations MEMORY_DEVICE_FS_DAX arranges for a
 * wakeup event whenever a page is unpinned and becomes idle. This
 * wakeup is used to coordinate physical address space management (ex:
 * fs truncate/hole punch) vs pinned pages (ex: device dma).
 *
 * MEMORY_DEVICE_PCI_P2PDMA:
 * Device memory residing in a PCI BAR intended for use with Peer-to-Peer
 * transactions.
 */
enum memory_type {
	MEMORY_DEVICE_PRIVATE = 1,
	MEMORY_DEVICE_PUBLIC,
	MEMORY_DEVICE_FS_DAX,
	MEMORY_DEVICE_PCI_P2PDMA,
};

/*
 * Additional notes about MEMORY_DEVICE_PRIVATE may be found in
 * include/linux/hmm.h and Documentation/vm/hmm.rst. There is also a brief
 * explanation in include/linux/memory_hotplug.h.
 *
 * The page_free() callback is called once the page refcount reaches 1
 * (ZONE_DEVICE pages never reach 0 refcount unless there is a refcount bug.
 * This allows the device driver to implement its own memory management.)
 */
typedef void (*dev_page_free_t)(struct page *page, void *data);

/**
 * struct dev_pagemap - metadata for ZONE_DEVICE mappings
 * @page_free: free page callback when page refcount reaches 1
 * @altmap: pre-allocated/reserved memory for vmemmap allocations
 * @res: physical address range covered by @ref
 * @ref: reference count that pins the devm_memremap_pages() mapping
 * @kill: callback to transition @ref to the dead state
 * @dev: host device of the mapping for debug
 * @data: private data pointer for page_free()
 * @type: memory type: see MEMORY_* in memory_hotplug.h
 */
struct dev_pagemap {
	dev_page_free_t page_free;
	struct vmem_altmap altmap;
	bool altmap_valid;
	struct resource res;
	struct percpu_ref *ref;
	void (*kill)(struct percpu_ref *ref);
	struct device *dev;
	void *data;
	enum memory_type type;
	u64 pci_p2pdma_bus_offset;
};

#ifdef CONFIG_ZONE_DEVICE
void *devm_memremap_pages(struct device *dev, struct dev_pagemap *pgmap);
struct dev_pagemap *get_dev_pagemap(unsigned long pfn,
		struct dev_pagemap *pgmap);

unsigned long vmem_altmap_offset(struct vmem_altmap *altmap);
void vmem_altmap_free(struct vmem_altmap *altmap, unsigned long nr_pfns);
#else
static inline void *devm_memremap_pages(struct device *dev,
		struct dev_pagemap *pgmap)
{
	/*
	 * Fail attempts to call devm_memremap_pages() without
	 * ZONE_DEVICE support enabled, this requires callers to fall
	 * back to plain devm_memremap() based on config
	 */
	WARN_ON_ONCE(1);
	return ERR_PTR(-ENXIO);
}

static inline struct dev_pagemap *get_dev_pagemap(unsigned long pfn,
		struct dev_pagemap *pgmap)
{
	return NULL;
}

static inline unsigned long vmem_altmap_offset(struct vmem_altmap *altmap)
{
	return 0;
}

static inline void vmem_altmap_free(struct vmem_altmap *altmap,
		unsigned long nr_pfns)
{
}
#endif /* CONFIG_ZONE_DEVICE */

static inline void put_dev_pagemap(struct dev_pagemap *pgmap)
{
	if (pgmap)
		percpu_ref_put(pgmap->ref);
}
#endif /* _LINUX_MEMREMAP_H_ */
