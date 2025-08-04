/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef __DRM_GPUSVM_H__
#define __DRM_GPUSVM_H__

#include <linux/kref.h>
#include <linux/interval_tree.h>
#include <linux/mmu_notifier.h>

struct dev_pagemap_ops;
struct drm_device;
struct drm_gpusvm;
struct drm_gpusvm_notifier;
struct drm_gpusvm_ops;
struct drm_gpusvm_range;
struct drm_gpusvm_devmem;
struct drm_pagemap;
struct drm_pagemap_device_addr;

/**
 * struct drm_gpusvm_devmem_ops - Operations structure for GPU SVM device memory
 *
 * This structure defines the operations for GPU Shared Virtual Memory (SVM)
 * device memory. These operations are provided by the GPU driver to manage device memory
 * allocations and perform operations such as migration between device memory and system
 * RAM.
 */
struct drm_gpusvm_devmem_ops {
	/**
	 * @devmem_release: Release device memory allocation (optional)
	 * @devmem_allocation: device memory allocation
	 *
	 * Release device memory allocation and drop a reference to device
	 * memory allocation.
	 */
	void (*devmem_release)(struct drm_gpusvm_devmem *devmem_allocation);

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
	int (*populate_devmem_pfn)(struct drm_gpusvm_devmem *devmem_allocation,
				   unsigned long npages, unsigned long *pfn);

	/**
	 * @copy_to_devmem: Copy to device memory (required for migration)
	 * @pages: Pointer to array of device memory pages (destination)
	 * @dma_addr: Pointer to array of DMA addresses (source)
	 * @npages: Number of pages to copy
	 *
	 * Copy pages to device memory.
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*copy_to_devmem)(struct page **pages,
			      dma_addr_t *dma_addr,
			      unsigned long npages);

	/**
	 * @copy_to_ram: Copy to system RAM (required for migration)
	 * @pages: Pointer to array of device memory pages (source)
	 * @dma_addr: Pointer to array of DMA addresses (destination)
	 * @npages: Number of pages to copy
	 *
	 * Copy pages to system RAM.
	 *
	 * Return: 0 on success, a negative error code on failure.
	 */
	int (*copy_to_ram)(struct page **pages,
			   dma_addr_t *dma_addr,
			   unsigned long npages);
};

/**
 * struct drm_gpusvm_devmem - Structure representing a GPU SVM device memory allocation
 *
 * @dev: Pointer to the device structure which device memory allocation belongs to
 * @mm: Pointer to the mm_struct for the address space
 * @detached: device memory allocations is detached from device pages
 * @ops: Pointer to the operations structure for GPU SVM device memory
 * @dpagemap: The struct drm_pagemap of the pages this allocation belongs to.
 * @size: Size of device memory allocation
 * @timeslice_expiration: Timeslice expiration in jiffies
 */
struct drm_gpusvm_devmem {
	struct device *dev;
	struct mm_struct *mm;
	struct completion detached;
	const struct drm_gpusvm_devmem_ops *ops;
	struct drm_pagemap *dpagemap;
	size_t size;
	u64 timeslice_expiration;
};

/**
 * struct drm_gpusvm_ops - Operations structure for GPU SVM
 *
 * This structure defines the operations for GPU Shared Virtual Memory (SVM).
 * These operations are provided by the GPU driver to manage SVM ranges and
 * notifiers.
 */
struct drm_gpusvm_ops {
	/**
	 * @notifier_alloc: Allocate a GPU SVM notifier (optional)
	 *
	 * Allocate a GPU SVM notifier.
	 *
	 * Return: Pointer to the allocated GPU SVM notifier on success, NULL on failure.
	 */
	struct drm_gpusvm_notifier *(*notifier_alloc)(void);

	/**
	 * @notifier_free: Free a GPU SVM notifier (optional)
	 * @notifier: Pointer to the GPU SVM notifier to be freed
	 *
	 * Free a GPU SVM notifier.
	 */
	void (*notifier_free)(struct drm_gpusvm_notifier *notifier);

	/**
	 * @range_alloc: Allocate a GPU SVM range (optional)
	 * @gpusvm: Pointer to the GPU SVM
	 *
	 * Allocate a GPU SVM range.
	 *
	 * Return: Pointer to the allocated GPU SVM range on success, NULL on failure.
	 */
	struct drm_gpusvm_range *(*range_alloc)(struct drm_gpusvm *gpusvm);

	/**
	 * @range_free: Free a GPU SVM range (optional)
	 * @range: Pointer to the GPU SVM range to be freed
	 *
	 * Free a GPU SVM range.
	 */
	void (*range_free)(struct drm_gpusvm_range *range);

	/**
	 * @invalidate: Invalidate GPU SVM notifier (required)
	 * @gpusvm: Pointer to the GPU SVM
	 * @notifier: Pointer to the GPU SVM notifier
	 * @mmu_range: Pointer to the mmu_notifier_range structure
	 *
	 * Invalidate the GPU page tables. It can safely walk the notifier range
	 * RB tree/list in this function. Called while holding the notifier lock.
	 */
	void (*invalidate)(struct drm_gpusvm *gpusvm,
			   struct drm_gpusvm_notifier *notifier,
			   const struct mmu_notifier_range *mmu_range);
};

/**
 * struct drm_gpusvm_notifier - Structure representing a GPU SVM notifier
 *
 * @gpusvm: Pointer to the GPU SVM structure
 * @notifier: MMU interval notifier
 * @itree: Interval tree node for the notifier (inserted in GPU SVM)
 * @entry: List entry to fast interval tree traversal
 * @root: Cached root node of the RB tree containing ranges
 * @range_list: List head containing of ranges in the same order they appear in
 *              interval tree. This is useful to keep iterating ranges while
 *              doing modifications to RB tree.
 * @flags: Flags for notifier
 * @flags.removed: Flag indicating whether the MMU interval notifier has been
 *                 removed
 *
 * This structure represents a GPU SVM notifier.
 */
struct drm_gpusvm_notifier {
	struct drm_gpusvm *gpusvm;
	struct mmu_interval_notifier notifier;
	struct interval_tree_node itree;
	struct list_head entry;
	struct rb_root_cached root;
	struct list_head range_list;
	struct {
		u32 removed : 1;
	} flags;
};

/**
 * struct drm_gpusvm_range_flags - Structure representing a GPU SVM range flags
 *
 * @migrate_devmem: Flag indicating whether the range can be migrated to device memory
 * @unmapped: Flag indicating if the range has been unmapped
 * @partial_unmap: Flag indicating if the range has been partially unmapped
 * @has_devmem_pages: Flag indicating if the range has devmem pages
 * @has_dma_mapping: Flag indicating if the range has a DMA mapping
 * @__flags: Flags for range in u16 form (used for READ_ONCE)
 */
struct drm_gpusvm_range_flags {
	union {
		struct {
			/* All flags below must be set upon creation */
			u16 migrate_devmem : 1;
			/* All flags below must be set / cleared under notifier lock */
			u16 unmapped : 1;
			u16 partial_unmap : 1;
			u16 has_devmem_pages : 1;
			u16 has_dma_mapping : 1;
		};
		u16 __flags;
	};
};

/**
 * struct drm_gpusvm_range - Structure representing a GPU SVM range
 *
 * @gpusvm: Pointer to the GPU SVM structure
 * @notifier: Pointer to the GPU SVM notifier
 * @refcount: Reference count for the range
 * @itree: Interval tree node for the range (inserted in GPU SVM notifier)
 * @entry: List entry to fast interval tree traversal
 * @notifier_seq: Notifier sequence number of the range's pages
 * @dma_addr: Device address array
 * @dpagemap: The struct drm_pagemap of the device pages we're dma-mapping.
 *            Note this is assuming only one drm_pagemap per range is allowed.
 * @flags: Flags for range
 *
 * This structure represents a GPU SVM range used for tracking memory ranges
 * mapped in a DRM device.
 */
struct drm_gpusvm_range {
	struct drm_gpusvm *gpusvm;
	struct drm_gpusvm_notifier *notifier;
	struct kref refcount;
	struct interval_tree_node itree;
	struct list_head entry;
	unsigned long notifier_seq;
	struct drm_pagemap_device_addr *dma_addr;
	struct drm_pagemap *dpagemap;
	struct drm_gpusvm_range_flags flags;
};

/**
 * struct drm_gpusvm - GPU SVM structure
 *
 * @name: Name of the GPU SVM
 * @drm: Pointer to the DRM device structure
 * @mm: Pointer to the mm_struct for the address space
 * @device_private_page_owner: Device private pages owner
 * @mm_start: Start address of GPU SVM
 * @mm_range: Range of the GPU SVM
 * @notifier_size: Size of individual notifiers
 * @ops: Pointer to the operations structure for GPU SVM
 * @chunk_sizes: Pointer to the array of chunk sizes used in range allocation.
 *               Entries should be powers of 2 in descending order.
 * @num_chunks: Number of chunks
 * @notifier_lock: Read-write semaphore for protecting notifier operations
 * @root: Cached root node of the Red-Black tree containing GPU SVM notifiers
 * @notifier_list: list head containing of notifiers in the same order they
 *                 appear in interval tree. This is useful to keep iterating
 *                 notifiers while doing modifications to RB tree.
 *
 * This structure represents a GPU SVM (Shared Virtual Memory) used for tracking
 * memory ranges mapped in a DRM (Direct Rendering Manager) device.
 *
 * No reference counting is provided, as this is expected to be embedded in the
 * driver VM structure along with the struct drm_gpuvm, which handles reference
 * counting.
 */
struct drm_gpusvm {
	const char *name;
	struct drm_device *drm;
	struct mm_struct *mm;
	void *device_private_page_owner;
	unsigned long mm_start;
	unsigned long mm_range;
	unsigned long notifier_size;
	const struct drm_gpusvm_ops *ops;
	const unsigned long *chunk_sizes;
	int num_chunks;
	struct rw_semaphore notifier_lock;
	struct rb_root_cached root;
	struct list_head notifier_list;
#ifdef CONFIG_LOCKDEP
	/**
	 * @lock_dep_map: Annotates drm_gpusvm_range_find_or_insert and
	 * drm_gpusvm_range_remove with a driver provided lock.
	 */
	struct lockdep_map *lock_dep_map;
#endif
};

/**
 * struct drm_gpusvm_ctx - DRM GPU SVM context
 *
 * @check_pages_threshold: Check CPU pages for present if chunk is less than or
 *                         equal to threshold. If not present, reduce chunk
 *                         size.
 * @timeslice_ms: The timeslice MS which in minimum time a piece of memory
 *		  remains with either exclusive GPU or CPU access.
 * @in_notifier: entering from a MMU notifier
 * @read_only: operating on read-only memory
 * @devmem_possible: possible to use device memory
 * @devmem_only: use only device memory
 *
 * Context that is DRM GPUSVM is operating in (i.e. user arguments).
 */
struct drm_gpusvm_ctx {
	unsigned long check_pages_threshold;
	unsigned long timeslice_ms;
	unsigned int in_notifier :1;
	unsigned int read_only :1;
	unsigned int devmem_possible :1;
	unsigned int devmem_only :1;
};

int drm_gpusvm_init(struct drm_gpusvm *gpusvm,
		    const char *name, struct drm_device *drm,
		    struct mm_struct *mm, void *device_private_page_owner,
		    unsigned long mm_start, unsigned long mm_range,
		    unsigned long notifier_size,
		    const struct drm_gpusvm_ops *ops,
		    const unsigned long *chunk_sizes, int num_chunks);

void drm_gpusvm_fini(struct drm_gpusvm *gpusvm);

void drm_gpusvm_free(struct drm_gpusvm *gpusvm);

struct drm_gpusvm_range *
drm_gpusvm_range_find_or_insert(struct drm_gpusvm *gpusvm,
				unsigned long fault_addr,
				unsigned long gpuva_start,
				unsigned long gpuva_end,
				const struct drm_gpusvm_ctx *ctx);

void drm_gpusvm_range_remove(struct drm_gpusvm *gpusvm,
			     struct drm_gpusvm_range *range);

int drm_gpusvm_range_evict(struct drm_gpusvm *gpusvm,
			   struct drm_gpusvm_range *range);

struct drm_gpusvm_range *
drm_gpusvm_range_get(struct drm_gpusvm_range *range);

void drm_gpusvm_range_put(struct drm_gpusvm_range *range);

bool drm_gpusvm_range_pages_valid(struct drm_gpusvm *gpusvm,
				  struct drm_gpusvm_range *range);

int drm_gpusvm_range_get_pages(struct drm_gpusvm *gpusvm,
			       struct drm_gpusvm_range *range,
			       const struct drm_gpusvm_ctx *ctx);

void drm_gpusvm_range_unmap_pages(struct drm_gpusvm *gpusvm,
				  struct drm_gpusvm_range *range,
				  const struct drm_gpusvm_ctx *ctx);

int drm_gpusvm_migrate_to_devmem(struct drm_gpusvm *gpusvm,
				 struct drm_gpusvm_range *range,
				 struct drm_gpusvm_devmem *devmem_allocation,
				 const struct drm_gpusvm_ctx *ctx);

int drm_gpusvm_evict_to_ram(struct drm_gpusvm_devmem *devmem_allocation);

const struct dev_pagemap_ops *drm_gpusvm_pagemap_ops_get(void);

bool drm_gpusvm_has_mapping(struct drm_gpusvm *gpusvm, unsigned long start,
			    unsigned long end);

struct drm_gpusvm_range *
drm_gpusvm_range_find(struct drm_gpusvm_notifier *notifier, unsigned long start,
		      unsigned long end);

void drm_gpusvm_range_set_unmapped(struct drm_gpusvm_range *range,
				   const struct mmu_notifier_range *mmu_range);

void drm_gpusvm_devmem_init(struct drm_gpusvm_devmem *devmem_allocation,
			    struct device *dev, struct mm_struct *mm,
			    const struct drm_gpusvm_devmem_ops *ops,
			    struct drm_pagemap *dpagemap, size_t size);

#ifdef CONFIG_LOCKDEP
/**
 * drm_gpusvm_driver_set_lock() - Set the lock protecting accesses to GPU SVM
 * @gpusvm: Pointer to the GPU SVM structure.
 * @lock: the lock used to protect the gpuva list. The locking primitive
 * must contain a dep_map field.
 *
 * Call this to annotate drm_gpusvm_range_find_or_insert and
 * drm_gpusvm_range_remove.
 */
#define drm_gpusvm_driver_set_lock(gpusvm, lock) \
	do { \
		if (!WARN((gpusvm)->lock_dep_map, \
			  "GPUSVM range lock should be set only once."))\
			(gpusvm)->lock_dep_map = &(lock)->dep_map;	\
	} while (0)
#else
#define drm_gpusvm_driver_set_lock(gpusvm, lock) do {} while (0)
#endif

/**
 * drm_gpusvm_notifier_lock() - Lock GPU SVM notifier
 * @gpusvm__: Pointer to the GPU SVM structure.
 *
 * Abstract client usage GPU SVM notifier lock, take lock
 */
#define drm_gpusvm_notifier_lock(gpusvm__)	\
	down_read(&(gpusvm__)->notifier_lock)

/**
 * drm_gpusvm_notifier_unlock() - Unlock GPU SVM notifier
 * @gpusvm__: Pointer to the GPU SVM structure.
 *
 * Abstract client usage GPU SVM notifier lock, drop lock
 */
#define drm_gpusvm_notifier_unlock(gpusvm__)	\
	up_read(&(gpusvm__)->notifier_lock)

/**
 * drm_gpusvm_range_start() - GPU SVM range start address
 * @range: Pointer to the GPU SVM range
 *
 * Return: GPU SVM range start address
 */
static inline unsigned long
drm_gpusvm_range_start(struct drm_gpusvm_range *range)
{
	return range->itree.start;
}

/**
 * drm_gpusvm_range_end() - GPU SVM range end address
 * @range: Pointer to the GPU SVM range
 *
 * Return: GPU SVM range end address
 */
static inline unsigned long
drm_gpusvm_range_end(struct drm_gpusvm_range *range)
{
	return range->itree.last + 1;
}

/**
 * drm_gpusvm_range_size() - GPU SVM range size
 * @range: Pointer to the GPU SVM range
 *
 * Return: GPU SVM range size
 */
static inline unsigned long
drm_gpusvm_range_size(struct drm_gpusvm_range *range)
{
	return drm_gpusvm_range_end(range) - drm_gpusvm_range_start(range);
}

/**
 * drm_gpusvm_notifier_start() - GPU SVM notifier start address
 * @notifier: Pointer to the GPU SVM notifier
 *
 * Return: GPU SVM notifier start address
 */
static inline unsigned long
drm_gpusvm_notifier_start(struct drm_gpusvm_notifier *notifier)
{
	return notifier->itree.start;
}

/**
 * drm_gpusvm_notifier_end() - GPU SVM notifier end address
 * @notifier: Pointer to the GPU SVM notifier
 *
 * Return: GPU SVM notifier end address
 */
static inline unsigned long
drm_gpusvm_notifier_end(struct drm_gpusvm_notifier *notifier)
{
	return notifier->itree.last + 1;
}

/**
 * drm_gpusvm_notifier_size() - GPU SVM notifier size
 * @notifier: Pointer to the GPU SVM notifier
 *
 * Return: GPU SVM notifier size
 */
static inline unsigned long
drm_gpusvm_notifier_size(struct drm_gpusvm_notifier *notifier)
{
	return drm_gpusvm_notifier_end(notifier) -
		drm_gpusvm_notifier_start(notifier);
}

/**
 * __drm_gpusvm_range_next() - Get the next GPU SVM range in the list
 * @range: a pointer to the current GPU SVM range
 *
 * Return: A pointer to the next drm_gpusvm_range if available, or NULL if the
 *         current range is the last one or if the input range is NULL.
 */
static inline struct drm_gpusvm_range *
__drm_gpusvm_range_next(struct drm_gpusvm_range *range)
{
	if (range && !list_is_last(&range->entry,
				   &range->notifier->range_list))
		return list_next_entry(range, entry);

	return NULL;
}

/**
 * drm_gpusvm_for_each_range() - Iterate over GPU SVM ranges in a notifier
 * @range__: Iterator variable for the ranges. If set, it indicates the start of
 *	     the iterator. If NULL, call drm_gpusvm_range_find() to get the range.
 * @notifier__: Pointer to the GPU SVM notifier
 * @start__: Start address of the range
 * @end__: End address of the range
 *
 * This macro is used to iterate over GPU SVM ranges in a notifier. It is safe
 * to use while holding the driver SVM lock or the notifier lock.
 */
#define drm_gpusvm_for_each_range(range__, notifier__, start__, end__)	\
	for ((range__) = (range__) ?:					\
	     drm_gpusvm_range_find((notifier__), (start__), (end__));	\
	     (range__) && (drm_gpusvm_range_start(range__) < (end__));	\
	     (range__) = __drm_gpusvm_range_next(range__))

#endif /* __DRM_GPUSVM_H__ */
