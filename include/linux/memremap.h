/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMREMAP_H_
#define _LINUX_MEMREMAP_H_
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/percpu-refcount.h>

#include <asm/pgtable.h>

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

#ifdef CONFIG_ZONE_DEVICE
struct vmem_altmap *to_vmem_altmap(unsigned long memmap_start);
#else
static inline struct vmem_altmap *to_vmem_altmap(unsigned long memmap_start)
{
	return NULL;
}
#endif

/*
 * Specialize ZONE_DEVICE memory into multiple types each having differents
 * usage.
 *
 * MEMORY_DEVICE_HOST:
 * Persistent device memory (pmem): struct page might be allocated in different
 * memory and architecture might want to perform special actions. It is similar
 * to regular memory, in that the CPU can access it transparently. However,
 * it is likely to have different bandwidth and latency than regular memory.
 * See Documentation/nvdimm/nvdimm.txt for more information.
 *
 * MEMORY_DEVICE_PRIVATE:
 * Device memory that is not directly addressable by the CPU: CPU can neither
 * read nor write private memory. In this case, we do still have struct pages
 * backing the device memory. Doing so simplifies the implementation, but it is
 * important to remember that there are certain points at which the struct page
 * must be treated as an opaque object, rather than a "normal" struct page.
 *
 * A more complete discussion of unaddressable memory may be found in
 * include/linux/hmm.h and Documentation/vm/hmm.txt.
 *
 * MEMORY_DEVICE_PUBLIC:
 * Device memory that is cache coherent from device and CPU point of view. This
 * is use on platform that have an advance system bus (like CAPI or CCIX). A
 * driver can hotplug the device memory using ZONE_DEVICE and with that memory
 * type. Any page of a process can be migrated to such memory. However no one
 * should be allow to pin such memory so that it can always be evicted.
 */
enum memory_type {
	MEMORY_DEVICE_HOST = 0,
	MEMORY_DEVICE_PRIVATE,
	MEMORY_DEVICE_PUBLIC,
};

/*
 * For MEMORY_DEVICE_PRIVATE we use ZONE_DEVICE and extend it with two
 * callbacks:
 *   page_fault()
 *   page_free()
 *
 * Additional notes about MEMORY_DEVICE_PRIVATE may be found in
 * include/linux/hmm.h and Documentation/vm/hmm.txt. There is also a brief
 * explanation in include/linux/memory_hotplug.h.
 *
 * The page_fault() callback must migrate page back, from device memory to
 * system memory, so that the CPU can access it. This might fail for various
 * reasons (device issues,  device have been unplugged, ...). When such error
 * conditions happen, the page_fault() callback must return VM_FAULT_SIGBUS and
 * set the CPU page table entry to "poisoned".
 *
 * Note that because memory cgroup charges are transferred to the device memory,
 * this should never fail due to memory restrictions. However, allocation
 * of a regular system page might still fail because we are out of memory. If
 * that happens, the page_fault() callback must return VM_FAULT_OOM.
 *
 * The page_fault() callback can also try to migrate back multiple pages in one
 * chunk, as an optimization. It must, however, prioritize the faulting address
 * over all the others.
 *
 *
 * The page_free() callback is called once the page refcount reaches 1
 * (ZONE_DEVICE pages never reach 0 refcount unless there is a refcount bug.
 * This allows the device driver to implement its own memory management.)
 *
 * For MEMORY_DEVICE_PUBLIC only the page_free() callback matter.
 */
typedef int (*dev_page_fault_t)(struct vm_area_struct *vma,
				unsigned long addr,
				const struct page *page,
				unsigned int flags,
				pmd_t *pmdp);
typedef void (*dev_page_free_t)(struct page *page, void *data);

/**
 * struct dev_pagemap - metadata for ZONE_DEVICE mappings
 * @page_fault: callback when CPU fault on an unaddressable device page
 * @page_free: free page callback when page refcount reaches 1
 * @altmap: pre-allocated/reserved memory for vmemmap allocations
 * @res: physical address range covered by @ref
 * @ref: reference count that pins the devm_memremap_pages() mapping
 * @dev: host device of the mapping for debug
 * @data: private data pointer for page_free()
 * @type: memory type: see MEMORY_* in memory_hotplug.h
 */
struct dev_pagemap {
	dev_page_fault_t page_fault;
	dev_page_free_t page_free;
	struct vmem_altmap *altmap;
	const struct resource *res;
	struct percpu_ref *ref;
	struct device *dev;
	void *data;
	enum memory_type type;
};

#ifdef CONFIG_ZONE_DEVICE
void *devm_memremap_pages(struct device *dev, struct resource *res,
		struct percpu_ref *ref, struct vmem_altmap *altmap);
struct dev_pagemap *find_dev_pagemap(resource_size_t phys);

static inline bool is_zone_device_page(const struct page *page);
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

#if defined(CONFIG_DEVICE_PRIVATE) || defined(CONFIG_DEVICE_PUBLIC)
static inline bool is_device_private_page(const struct page *page)
{
	return is_zone_device_page(page) &&
		page->pgmap->type == MEMORY_DEVICE_PRIVATE;
}

static inline bool is_device_public_page(const struct page *page)
{
	return is_zone_device_page(page) &&
		page->pgmap->type == MEMORY_DEVICE_PUBLIC;
}
#endif /* CONFIG_DEVICE_PRIVATE || CONFIG_DEVICE_PUBLIC */

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
