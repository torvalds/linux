/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SWIOTLB_H
#define __LINUX_SWIOTLB_H

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <linux/spinlock.h>

struct device;
struct page;
struct scatterlist;

#define SWIOTLB_VERBOSE	(1 << 0) /* verbose initialization */
#define SWIOTLB_FORCE	(1 << 1) /* force bounce buffering */
#define SWIOTLB_ANY	(1 << 2) /* allow any memory for the buffer */

/*
 * Maximum allowable number of contiguous slabs to map,
 * must be a power of 2.  What is the appropriate value ?
 * The complexity of {map,unmap}_single is linearly dependent on this value.
 */
#define IO_TLB_SEGSIZE	128

/*
 * log of the size of each IO TLB slab.  The number of slabs is command line
 * controllable.
 */
#define IO_TLB_SHIFT 11
#define IO_TLB_SIZE (1 << IO_TLB_SHIFT)

/* default to 64MB */
#define IO_TLB_DEFAULT_SIZE (64UL<<20)

unsigned long swiotlb_size_or_default(void);
void __init swiotlb_init_remap(bool addressing_limit, unsigned int flags,
	int (*remap)(void *tlb, unsigned long nslabs));
int swiotlb_init_late(size_t size, gfp_t gfp_mask,
	int (*remap)(void *tlb, unsigned long nslabs));
extern void __init swiotlb_update_mem_attributes(void);

phys_addr_t swiotlb_tbl_map_single(struct device *hwdev, phys_addr_t phys,
		size_t mapping_size, size_t alloc_size,
		unsigned int alloc_aligned_mask, enum dma_data_direction dir,
		unsigned long attrs);

extern void swiotlb_tbl_unmap_single(struct device *hwdev,
				     phys_addr_t tlb_addr,
				     size_t mapping_size,
				     enum dma_data_direction dir,
				     unsigned long attrs);

void swiotlb_sync_single_for_device(struct device *dev, phys_addr_t tlb_addr,
		size_t size, enum dma_data_direction dir);
void swiotlb_sync_single_for_cpu(struct device *dev, phys_addr_t tlb_addr,
		size_t size, enum dma_data_direction dir);
dma_addr_t swiotlb_map(struct device *dev, phys_addr_t phys,
		size_t size, enum dma_data_direction dir, unsigned long attrs);

#ifdef CONFIG_SWIOTLB

/**
 * struct io_tlb_mem - IO TLB Memory Pool Descriptor
 *
 * @start:	The start address of the swiotlb memory pool. Used to do a quick
 *		range check to see if the memory was in fact allocated by this
 *		API.
 * @end:	The end address of the swiotlb memory pool. Used to do a quick
 *		range check to see if the memory was in fact allocated by this
 *		API.
 * @vaddr:	The vaddr of the swiotlb memory pool. The swiotlb memory pool
 *		may be remapped in the memory encrypted case and store virtual
 *		address for bounce buffer operation.
 * @nslabs:	The number of IO TLB blocks (in groups of 64) between @start and
 *		@end. For default swiotlb, this is command line adjustable via
 *		setup_io_tlb_npages.
 * @list:	The free list describing the number of free entries available
 *		from each index.
 * @orig_addr:	The original address corresponding to a mapped entry.
 * @alloc_size:	Size of the allocated buffer.
 * @debugfs:	The dentry to debugfs.
 * @late_alloc:	%true if allocated using the page allocator
 * @force_bounce: %true if swiotlb bouncing is forced
 * @for_alloc:  %true if the pool is used for memory allocation
 * @nareas:  The area number in the pool.
 * @area_nslabs: The slot number in the area.
 * @total_used:	The total number of slots in the pool that are currently used
 *		across all areas. Used only for calculating used_hiwater in
 *		debugfs.
 * @used_hiwater: The high water mark for total_used.  Used only for reporting
 *		in debugfs.
 */
struct io_tlb_mem {
	phys_addr_t start;
	phys_addr_t end;
	void *vaddr;
	unsigned long nslabs;
	struct dentry *debugfs;
	bool late_alloc;
	bool force_bounce;
	bool for_alloc;
	unsigned int nareas;
	unsigned int area_nslabs;
	struct io_tlb_area *areas;
	struct io_tlb_slot *slots;
#ifdef CONFIG_DEBUG_FS
	atomic_long_t total_used;
	atomic_long_t used_hiwater;
#endif
};
extern struct io_tlb_mem io_tlb_default_mem;

static inline bool is_swiotlb_buffer(struct device *dev, phys_addr_t paddr)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;

	return mem && paddr >= mem->start && paddr < mem->end;
}

static inline bool is_swiotlb_force_bounce(struct device *dev)
{
	struct io_tlb_mem *mem = dev->dma_io_tlb_mem;

	return mem && mem->force_bounce;
}

void swiotlb_init(bool addressing_limited, unsigned int flags);
void __init swiotlb_exit(void);
size_t swiotlb_max_mapping_size(struct device *dev);
bool is_swiotlb_active(struct device *dev);
void __init swiotlb_adjust_size(unsigned long size);
#else
static inline void swiotlb_init(bool addressing_limited, unsigned int flags)
{
}
static inline bool is_swiotlb_buffer(struct device *dev, phys_addr_t paddr)
{
	return false;
}
static inline bool is_swiotlb_force_bounce(struct device *dev)
{
	return false;
}
static inline void swiotlb_exit(void)
{
}
static inline size_t swiotlb_max_mapping_size(struct device *dev)
{
	return SIZE_MAX;
}

static inline bool is_swiotlb_active(struct device *dev)
{
	return false;
}

static inline void swiotlb_adjust_size(unsigned long size)
{
}
#endif /* CONFIG_SWIOTLB */

extern void swiotlb_print_info(void);

#ifdef CONFIG_DMA_RESTRICTED_POOL
struct page *swiotlb_alloc(struct device *dev, size_t size);
bool swiotlb_free(struct device *dev, struct page *page, size_t size);

static inline bool is_swiotlb_for_alloc(struct device *dev)
{
	return dev->dma_io_tlb_mem->for_alloc;
}
#else
static inline struct page *swiotlb_alloc(struct device *dev, size_t size)
{
	return NULL;
}
static inline bool swiotlb_free(struct device *dev, struct page *page,
				size_t size)
{
	return false;
}
static inline bool is_swiotlb_for_alloc(struct device *dev)
{
	return false;
}
#endif /* CONFIG_DMA_RESTRICTED_POOL */

#endif /* __LINUX_SWIOTLB_H */
