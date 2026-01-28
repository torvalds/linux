/* SPDX-License-Identifier: MIT */
#ifndef _DRM_PAGEMAP_H_
#define _DRM_PAGEMAP_H_

#include <linux/dma-direction.h>
#include <linux/hmm.h>
#include <linux/types.h>

#define NR_PAGES(order) (1U << (order))

struct dma_fence;
struct drm_pagemap;
struct drm_pagemap_cache;
struct drm_pagemap_dev_hold;
struct drm_pagemap_zdd;
struct device;

/**
 * enum drm_interconnect_protocol - Used to identify an interconnect protocol.
 *
 * @DRM_INTERCONNECT_SYSTEM: DMA map is system pages
 * @DRM_INTERCONNECT_DRIVER: DMA map is driver defined
 */
enum drm_interconnect_protocol {
	DRM_INTERCONNECT_SYSTEM,
	DRM_INTERCONNECT_DRIVER,
	/* A driver can add private values beyond DRM_INTERCONNECT_DRIVER */
};

/**
 * struct drm_pagemap_addr - Address representation.
 * @addr: The dma address or driver-defined address for driver private interconnects.
 * @proto: The interconnect protocol.
 * @order: The page order of the device mapping. (Size is PAGE_SIZE << order).
 * @dir: The DMA direction.
 *
 * Note: There is room for improvement here. We should be able to pack into
 * 64 bits.
 */
struct drm_pagemap_addr {
	dma_addr_t addr;
	u64 proto : 54;
	u64 order : 8;
	u64 dir : 2;
};

/**
 * drm_pagemap_addr_encode() - Encode a dma address with metadata
 * @addr: The dma address or driver-defined address for driver private interconnects.
 * @proto: The interconnect protocol.
 * @order: The page order of the dma mapping. (Size is PAGE_SIZE << order).
 * @dir: The DMA direction.
 *
 * Return: A struct drm_pagemap_addr encoding the above information.
 */
static inline struct drm_pagemap_addr
drm_pagemap_addr_encode(dma_addr_t addr,
			enum drm_interconnect_protocol proto,
			unsigned int order,
			enum dma_data_direction dir)
{
	return (struct drm_pagemap_addr) {
		.addr = addr,
		.proto = proto,
		.order = order,
		.dir = dir,
	};
}

/**
 * struct drm_pagemap_ops: Ops for a drm-pagemap.
 */
struct drm_pagemap_ops {
	/**
	 * @device_map: Map for device access or provide a virtual address suitable for
	 *
	 * @dpagemap: The struct drm_pagemap for the page.
	 * @dev: The device mapper.
	 * @page: The page to map.
	 * @order: The page order of the device mapping. (Size is PAGE_SIZE << order).
	 * @dir: The transfer direction.
	 */
	struct drm_pagemap_addr (*device_map)(struct drm_pagemap *dpagemap,
					      struct device *dev,
					      struct page *page,
					      unsigned int order,
					      enum dma_data_direction dir);

	/**
	 * @device_unmap: Unmap a device address previously obtained using @device_map.
	 *
	 * @dpagemap: The struct drm_pagemap for the mapping.
	 * @dev: The device unmapper.
	 * @addr: The device address obtained when mapping.
	 */
	void (*device_unmap)(struct drm_pagemap *dpagemap,
			     struct device *dev,
			     struct drm_pagemap_addr addr);

	/**
	 * @populate_mm: Populate part of the mm with @dpagemap memory,
	 * migrating existing data.
	 * @dpagemap: The struct drm_pagemap managing the memory.
	 * @start: The virtual start address in @mm
	 * @end: The virtual end address in @mm
	 * @mm: Pointer to a live mm. The caller must have an mmget()
	 * reference.
	 *
	 * The caller will have the mm lock at least in read mode.
	 * Note that there is no guarantee that the memory is resident
	 * after the function returns, it's best effort only.
	 * When the mm is not using the memory anymore,
	 * it will be released. The struct drm_pagemap might have a
	 * mechanism in place to reclaim the memory and the data will
	 * then be migrated. Typically to system memory.
	 * The implementation should hold sufficient runtime power-
	 * references while pages are used in an address space and
	 * should ideally guard against hardware device unbind in
	 * a way such that device pages are migrated back to system
	 * followed by device page removal. The implementation should
	 * return -ENODEV after device removal.
	 *
	 * Return: 0 if successful. Negative error code on error.
	 */
	int (*populate_mm)(struct drm_pagemap *dpagemap,
			   unsigned long start, unsigned long end,
			   struct mm_struct *mm,
			   unsigned long timeslice_ms);
	/**
	 * @destroy: Destroy the drm_pagemap and associated resources.
	 * @dpagemap: The drm_pagemap to destroy.
	 * @is_atomic_or_reclaim: The function may be called from
	 * atomic- or reclaim context.
	 *
	 * The implementation should take care not to attempt to
	 * destroy resources that may already have been destroyed
	 * using devm_ callbacks, since this function may be called
	 * after the underlying struct device has been unbound.
	 * If the implementation defers the execution to a work item
	 * to avoid locking issues, then it must make sure the work
	 * items are flushed before module exit. If the destroy call
	 * happens after the provider's pci_remove() callback has
	 * been executed, a module reference and drm device reference is
	 * held across the destroy callback.
	 */
	void (*destroy)(struct drm_pagemap *dpagemap,
			bool is_atomic_or_reclaim);
};

/**
 * struct drm_pagemap: Additional information for a struct dev_pagemap
 * used for device p2p handshaking.
 * @ops: The struct drm_pagemap_ops.
 * @ref: Reference count.
 * @drm: The struct drm device owning the device-private memory.
 * @pagemap: Pointer to the underlying dev_pagemap.
 * @dev_hold: Pointer to a struct drm_pagemap_dev_hold for
 * device referencing.
 * @cache: Back-pointer to the &struct drm_pagemap_cache used for this
 * &struct drm_pagemap. May be NULL if no cache is used.
 * @shrink_link: Link into the shrinker's list of drm_pagemaps. Only
 * used if also using a pagemap cache.
 */
struct drm_pagemap {
	const struct drm_pagemap_ops *ops;
	struct kref ref;
	struct drm_device *drm;
	struct dev_pagemap *pagemap;
	struct drm_pagemap_dev_hold *dev_hold;
	struct drm_pagemap_cache *cache;
	struct list_head shrink_link;
};

struct drm_pagemap_devmem;

/**
 * struct drm_pagemap_devmem_ops - Operations structure for GPU SVM device memory
 *
 * This structure defines the operations for GPU Shared Virtual Memory (SVM)
 * device memory. These operations are provided by the GPU driver to manage device memory
 * allocations and perform operations such as migration between device memory and system
 * RAM.
 */
struct drm_pagemap_devmem_ops {
	/**
	 * @devmem_release: Release device memory allocation (optional)
	 * @devmem_allocation: device memory allocation
	 *
	 * Release device memory allocation and drop a reference to device
	 * memory allocation.
	 */
	void (*devmem_release)(struct drm_pagemap_devmem *devmem_allocation);

	/**
	 * @populate_devmem_pfn: Populate device memory PFN (required for migration)
	 * @devmem_allocation: device memory allocation
	 * @npages: Number of pages to populate
	 * @pfn: Array of page frame numbers to populate
	 *
	 * Populate device memory page frame numbers (PFN).
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*populate_devmem_pfn)(struct drm_pagemap_devmem *devmem_allocation,
				   unsigned long npages, unsigned long *pfn);

	/**
	 * @copy_to_devmem: Copy to device memory (required for migration)
	 * @pages: Pointer to array of device memory pages (destination)
	 * @pagemap_addr: Pointer to array of DMA information (source)
	 * @npages: Number of pages to copy
	 * @pre_migrate_fence: dma-fence to wait for before migration start.
	 * May be NULL.
	 *
	 * Copy pages to device memory. If the order of a @pagemap_addr entry
	 * is greater than 0, the entry is populated but subsequent entries
	 * within the range of that order are not populated.
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*copy_to_devmem)(struct page **pages,
			      struct drm_pagemap_addr *pagemap_addr,
			      unsigned long npages,
			      struct dma_fence *pre_migrate_fence);

	/**
	 * @copy_to_ram: Copy to system RAM (required for migration)
	 * @pages: Pointer to array of device memory pages (source)
	 * @pagemap_addr: Pointer to array of DMA information (destination)
	 * @npages: Number of pages to copy
	 * @pre_migrate_fence: dma-fence to wait for before migration start.
	 * May be NULL.
	 *
	 * Copy pages to system RAM. If the order of a @pagemap_addr entry
	 * is greater than 0, the entry is populated but subsequent entries
	 * within the range of that order are not populated.
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*copy_to_ram)(struct page **pages,
			   struct drm_pagemap_addr *pagemap_addr,
			   unsigned long npages,
			   struct dma_fence *pre_migrate_fence);
};

#if IS_ENABLED(CONFIG_ZONE_DEVICE)

int drm_pagemap_init(struct drm_pagemap *dpagemap,
		     struct dev_pagemap *pagemap,
		     struct drm_device *drm,
		     const struct drm_pagemap_ops *ops);

struct drm_pagemap *drm_pagemap_create(struct drm_device *drm,
				       struct dev_pagemap *pagemap,
				       const struct drm_pagemap_ops *ops);

struct drm_pagemap *drm_pagemap_page_to_dpagemap(struct page *page);

void drm_pagemap_put(struct drm_pagemap *dpagemap);

#else

static inline struct drm_pagemap *drm_pagemap_page_to_dpagemap(struct page *page)
{
	return NULL;
}

static inline void drm_pagemap_put(struct drm_pagemap *dpagemap)
{
}

#endif /* IS_ENABLED(CONFIG_ZONE_DEVICE) */

/**
 * drm_pagemap_get() - Obtain a reference on a struct drm_pagemap
 * @dpagemap: Pointer to the struct drm_pagemap, or NULL.
 *
 * Return: Pointer to the struct drm_pagemap, or NULL.
 */
static inline struct drm_pagemap *
drm_pagemap_get(struct drm_pagemap *dpagemap)
{
	if (likely(dpagemap))
		kref_get(&dpagemap->ref);

	return dpagemap;
}

/**
 * drm_pagemap_get_unless_zero() - Obtain a reference on a struct drm_pagemap
 * unless the current reference count is zero.
 * @dpagemap: Pointer to the drm_pagemap or NULL.
 *
 * Return: A pointer to @dpagemap if the reference count was successfully
 * incremented. NULL if @dpagemap was NULL, or its refcount was 0.
 */
static inline struct drm_pagemap * __must_check
drm_pagemap_get_unless_zero(struct drm_pagemap *dpagemap)
{
	return (dpagemap && kref_get_unless_zero(&dpagemap->ref)) ? dpagemap : NULL;
}

/**
 * struct drm_pagemap_devmem - Structure representing a GPU SVM device memory allocation
 *
 * @dev: Pointer to the device structure which device memory allocation belongs to
 * @mm: Pointer to the mm_struct for the address space
 * @detached: device memory allocations is detached from device pages
 * @ops: Pointer to the operations structure for GPU SVM device memory
 * @dpagemap: The struct drm_pagemap of the pages this allocation belongs to.
 * @size: Size of device memory allocation
 * @timeslice_expiration: Timeslice expiration in jiffies
 * @pre_migrate_fence: Fence to wait for or pipeline behind before migration starts.
 * (May be NULL).
 */
struct drm_pagemap_devmem {
	struct device *dev;
	struct mm_struct *mm;
	struct completion detached;
	const struct drm_pagemap_devmem_ops *ops;
	struct drm_pagemap *dpagemap;
	size_t size;
	u64 timeslice_expiration;
	struct dma_fence *pre_migrate_fence;
};

/**
 * struct drm_pagemap_migrate_details - Details to govern migration.
 * @timeslice_ms: The time requested for the migrated pagemap pages to
 * be present in @mm before being allowed to be migrated back.
 * @can_migrate_same_pagemap: Whether the copy function as indicated by
 * the @source_peer_migrates flag, can migrate device pages within a
 * single drm_pagemap.
 * @source_peer_migrates: Whether on p2p migration, The source drm_pagemap
 * should use the copy_to_ram() callback rather than the destination
 * drm_pagemap should use the copy_to_devmem() callback.
 */
struct drm_pagemap_migrate_details {
	unsigned long timeslice_ms;
	u32 can_migrate_same_pagemap : 1;
	u32 source_peer_migrates : 1;
};

#if IS_ENABLED(CONFIG_ZONE_DEVICE)

int drm_pagemap_migrate_to_devmem(struct drm_pagemap_devmem *devmem_allocation,
				  struct mm_struct *mm,
				  unsigned long start, unsigned long end,
				  const struct drm_pagemap_migrate_details *mdetails);

int drm_pagemap_evict_to_ram(struct drm_pagemap_devmem *devmem_allocation);

const struct dev_pagemap_ops *drm_pagemap_pagemap_ops_get(void);

void drm_pagemap_devmem_init(struct drm_pagemap_devmem *devmem_allocation,
			     struct device *dev, struct mm_struct *mm,
			     const struct drm_pagemap_devmem_ops *ops,
			     struct drm_pagemap *dpagemap, size_t size,
			     struct dma_fence *pre_migrate_fence);

int drm_pagemap_populate_mm(struct drm_pagemap *dpagemap,
			    unsigned long start, unsigned long end,
			    struct mm_struct *mm,
			    unsigned long timeslice_ms);

void drm_pagemap_destroy(struct drm_pagemap *dpagemap, bool is_atomic_or_reclaim);

int drm_pagemap_reinit(struct drm_pagemap *dpagemap);

#endif /* IS_ENABLED(CONFIG_ZONE_DEVICE) */

#endif
