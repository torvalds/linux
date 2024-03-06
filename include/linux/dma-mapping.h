/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/cache.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-direction.h>
#include <linux/scatterlist.h>
#include <linux/bug.h>
#include <linux/mem_encrypt.h>

/**
 * List of possible attributes associated with a DMA mapping. The semantics
 * of each attribute should be defined in Documentation/core-api/dma-attributes.rst.
 */

/*
 * DMA_ATTR_WEAK_ORDERING: Specifies that reads and writes to the mapping
 * may be weakly ordered, that is that reads and writes may pass each other.
 */
#define DMA_ATTR_WEAK_ORDERING		(1UL << 1)
/*
 * DMA_ATTR_WRITE_COMBINE: Specifies that writes to the mapping may be
 * buffered to improve performance.
 */
#define DMA_ATTR_WRITE_COMBINE		(1UL << 2)
/*
 * DMA_ATTR_NO_KERNEL_MAPPING: Lets the platform to avoid creating a kernel
 * virtual mapping for the allocated buffer.
 */
#define DMA_ATTR_NO_KERNEL_MAPPING	(1UL << 4)
/*
 * DMA_ATTR_SKIP_CPU_SYNC: Allows platform code to skip synchronization of
 * the CPU cache for the given buffer assuming that it has been already
 * transferred to 'device' domain.
 */
#define DMA_ATTR_SKIP_CPU_SYNC		(1UL << 5)
/*
 * DMA_ATTR_FORCE_CONTIGUOUS: Forces contiguous allocation of the buffer
 * in physical memory.
 */
#define DMA_ATTR_FORCE_CONTIGUOUS	(1UL << 6)
/*
 * DMA_ATTR_ALLOC_SINGLE_PAGES: This is a hint to the DMA-mapping subsystem
 * that it's probably not worth the time to try to allocate memory to in a way
 * that gives better TLB efficiency.
 */
#define DMA_ATTR_ALLOC_SINGLE_PAGES	(1UL << 7)
/*
 * DMA_ATTR_NO_WARN: This tells the DMA-mapping subsystem to suppress
 * allocation failure reports (similarly to __GFP_NOWARN).
 */
#define DMA_ATTR_NO_WARN	(1UL << 8)

/*
 * DMA_ATTR_PRIVILEGED: used to indicate that the buffer is fully
 * accessible at an elevated privilege level (and ideally inaccessible or
 * at least read-only at lesser-privileged levels).
 */
#define DMA_ATTR_PRIVILEGED		(1UL << 9)

/*
 * A dma_addr_t can hold any valid DMA or bus address for the platform.  It can
 * be given to a device to use as a DMA source or target.  It is specific to a
 * given device and there may be a translation between the CPU physical address
 * space and the bus address space.
 *
 * DMA_MAPPING_ERROR is the magic error code if a mapping failed.  It should not
 * be used directly in drivers, but checked for using dma_mapping_error()
 * instead.
 */
#define DMA_MAPPING_ERROR		(~(dma_addr_t)0)

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

#ifdef CONFIG_DMA_API_DEBUG
void debug_dma_mapping_error(struct device *dev, dma_addr_t dma_addr);
void debug_dma_map_single(struct device *dev, const void *addr,
		unsigned long len);
#else
static inline void debug_dma_mapping_error(struct device *dev,
		dma_addr_t dma_addr)
{
}
static inline void debug_dma_map_single(struct device *dev, const void *addr,
		unsigned long len)
{
}
#endif /* CONFIG_DMA_API_DEBUG */

#ifdef CONFIG_HAS_DMA
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	debug_dma_mapping_error(dev, dma_addr);

	if (unlikely(dma_addr == DMA_MAPPING_ERROR))
		return -ENOMEM;
	return 0;
}

dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
		size_t offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs);
void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
unsigned int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir,
				      unsigned long attrs);
int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs);
dma_addr_t dma_map_resource(struct device *dev, phys_addr_t phys_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_resource(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir);
void dma_sync_single_for_device(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir);
void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
		    int nelems, enum dma_data_direction dir);
void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
		       int nelems, enum dma_data_direction dir);
void *dma_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t flag, unsigned long attrs);
void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_handle, unsigned long attrs);
void *dmam_alloc_attrs(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void dmam_free_coherent(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle);
int dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
bool dma_can_mmap(struct device *dev);
bool dma_pci_p2pdma_supported(struct device *dev);
int dma_set_mask(struct device *dev, u64 mask);
int dma_set_coherent_mask(struct device *dev, u64 mask);
u64 dma_get_required_mask(struct device *dev);
bool dma_addressing_limited(struct device *dev);
size_t dma_max_mapping_size(struct device *dev);
size_t dma_opt_mapping_size(struct device *dev);
bool dma_need_sync(struct device *dev, dma_addr_t dma_addr);
unsigned long dma_get_merge_boundary(struct device *dev);
struct sg_table *dma_alloc_noncontiguous(struct device *dev, size_t size,
		enum dma_data_direction dir, gfp_t gfp, unsigned long attrs);
void dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir);
void *dma_vmap_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt);
void dma_vunmap_noncontiguous(struct device *dev, void *vaddr);
int dma_mmap_noncontiguous(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct sg_table *sgt);
#else /* CONFIG_HAS_DMA */
static inline dma_addr_t dma_map_page_attrs(struct device *dev,
		struct page *page, size_t offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}
static inline void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
}
static inline unsigned int dma_map_sg_attrs(struct device *dev,
		struct scatterlist *sg, int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
	return 0;
}
static inline void dma_unmap_sg_attrs(struct device *dev,
		struct scatterlist *sg, int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
}
static inline int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs)
{
	return -EOPNOTSUPP;
}
static inline dma_addr_t dma_map_resource(struct device *dev,
		phys_addr_t phys_addr, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}
static inline void dma_unmap_resource(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
}
static inline void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir)
{
}
static inline void dma_sync_single_for_device(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
}
static inline void dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sg, int nelems, enum dma_data_direction dir)
{
}
static inline void dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sg, int nelems, enum dma_data_direction dir)
{
}
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return -ENOMEM;
}
static inline void *dma_alloc_attrs(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t flag, unsigned long attrs)
{
	return NULL;
}
static void dma_free_attrs(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_handle, unsigned long attrs)
{
}
static inline void *dmam_alloc_attrs(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	return NULL;
}
static inline void dmam_free_coherent(struct device *dev, size_t size,
		void *vaddr, dma_addr_t dma_handle)
{
}
static inline int dma_get_sgtable_attrs(struct device *dev,
		struct sg_table *sgt, void *cpu_addr, dma_addr_t dma_addr,
		size_t size, unsigned long attrs)
{
	return -ENXIO;
}
static inline int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	return -ENXIO;
}
static inline bool dma_can_mmap(struct device *dev)
{
	return false;
}
static inline bool dma_pci_p2pdma_supported(struct device *dev)
{
	return false;
}
static inline int dma_set_mask(struct device *dev, u64 mask)
{
	return -EIO;
}
static inline int dma_set_coherent_mask(struct device *dev, u64 mask)
{
	return -EIO;
}
static inline u64 dma_get_required_mask(struct device *dev)
{
	return 0;
}
static inline bool dma_addressing_limited(struct device *dev)
{
	return false;
}
static inline size_t dma_max_mapping_size(struct device *dev)
{
	return 0;
}
static inline size_t dma_opt_mapping_size(struct device *dev)
{
	return 0;
}
static inline bool dma_need_sync(struct device *dev, dma_addr_t dma_addr)
{
	return false;
}
static inline unsigned long dma_get_merge_boundary(struct device *dev)
{
	return 0;
}
static inline struct sg_table *dma_alloc_noncontiguous(struct device *dev,
		size_t size, enum dma_data_direction dir, gfp_t gfp,
		unsigned long attrs)
{
	return NULL;
}
static inline void dma_free_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt, enum dma_data_direction dir)
{
}
static inline void *dma_vmap_noncontiguous(struct device *dev, size_t size,
		struct sg_table *sgt)
{
	return NULL;
}
static inline void dma_vunmap_noncontiguous(struct device *dev, void *vaddr)
{
}
static inline int dma_mmap_noncontiguous(struct device *dev,
		struct vm_area_struct *vma, size_t size, struct sg_table *sgt)
{
	return -EINVAL;
}
#endif /* CONFIG_HAS_DMA */

struct page *dma_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp);
void dma_free_pages(struct device *dev, size_t size, struct page *page,
		dma_addr_t dma_handle, enum dma_data_direction dir);
int dma_mmap_pages(struct device *dev, struct vm_area_struct *vma,
		size_t size, struct page *page);

static inline void *dma_alloc_noncoherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp)
{
	struct page *page = dma_alloc_pages(dev, size, dma_handle, dir, gfp);
	return page ? page_address(page) : NULL;
}

static inline void dma_free_noncoherent(struct device *dev, size_t size,
		void *vaddr, dma_addr_t dma_handle, enum dma_data_direction dir)
{
	dma_free_pages(dev, size, virt_to_page(vaddr), dma_handle, dir);
}

static inline dma_addr_t dma_map_single_attrs(struct device *dev, void *ptr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	/* DMA must never operate on areas that might be remapped. */
	if (dev_WARN_ONCE(dev, is_vmalloc_addr(ptr),
			  "rejecting DMA map of vmalloc memory\n"))
		return DMA_MAPPING_ERROR;
	debug_dma_map_single(dev, ptr, size);
	return dma_map_page_attrs(dev, virt_to_page(ptr), offset_in_page(ptr),
			size, dir, attrs);
}

static inline void dma_unmap_single_attrs(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	return dma_unmap_page_attrs(dev, addr, size, dir, attrs);
}

static inline void dma_sync_single_range_for_cpu(struct device *dev,
		dma_addr_t addr, unsigned long offset, size_t size,
		enum dma_data_direction dir)
{
	return dma_sync_single_for_cpu(dev, addr + offset, size, dir);
}

static inline void dma_sync_single_range_for_device(struct device *dev,
		dma_addr_t addr, unsigned long offset, size_t size,
		enum dma_data_direction dir)
{
	return dma_sync_single_for_device(dev, addr + offset, size, dir);
}

/**
 * dma_unmap_sgtable - Unmap the given buffer for DMA
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 * @attrs:	Optional DMA attributes for the unmap operation
 *
 * Unmaps a buffer described by a scatterlist stored in the given sg_table
 * object for the @dir DMA operation by the @dev device. After this function
 * the ownership of the buffer is transferred back to the CPU domain.
 */
static inline void dma_unmap_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs)
{
	dma_unmap_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
}

/**
 * dma_sync_sgtable_for_cpu - Synchronize the given buffer for CPU access
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 *
 * Performs the needed cache synchronization and moves the ownership of the
 * buffer back to the CPU domain, so it is safe to perform any access to it
 * by the CPU. Before doing any further DMA operations, one has to transfer
 * the ownership of the buffer back to the DMA domain by calling the
 * dma_sync_sgtable_for_device().
 */
static inline void dma_sync_sgtable_for_cpu(struct device *dev,
		struct sg_table *sgt, enum dma_data_direction dir)
{
	dma_sync_sg_for_cpu(dev, sgt->sgl, sgt->orig_nents, dir);
}

/**
 * dma_sync_sgtable_for_device - Synchronize the given buffer for DMA
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 *
 * Performs the needed cache synchronization and moves the ownership of the
 * buffer back to the DMA domain, so it is safe to perform the DMA operation.
 * Once finished, one has to call dma_sync_sgtable_for_cpu() or
 * dma_unmap_sgtable().
 */
static inline void dma_sync_sgtable_for_device(struct device *dev,
		struct sg_table *sgt, enum dma_data_direction dir)
{
	dma_sync_sg_for_device(dev, sgt->sgl, sgt->orig_nents, dir);
}

#define dma_map_single(d, a, s, r) dma_map_single_attrs(d, a, s, r, 0)
#define dma_unmap_single(d, a, s, r) dma_unmap_single_attrs(d, a, s, r, 0)
#define dma_map_sg(d, s, n, r) dma_map_sg_attrs(d, s, n, r, 0)
#define dma_unmap_sg(d, s, n, r) dma_unmap_sg_attrs(d, s, n, r, 0)
#define dma_map_page(d, p, o, s, r) dma_map_page_attrs(d, p, o, s, r, 0)
#define dma_unmap_page(d, a, s, r) dma_unmap_page_attrs(d, a, s, r, 0)
#define dma_get_sgtable(d, t, v, h, s) dma_get_sgtable_attrs(d, t, v, h, s, 0)
#define dma_mmap_coherent(d, v, c, h, s) dma_mmap_attrs(d, v, c, h, s, 0)

bool dma_coherent_ok(struct device *dev, phys_addr_t phys, size_t size);

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp)
{
	return dma_alloc_attrs(dev, size, dma_handle, gfp,
			(gfp & __GFP_NOWARN) ? DMA_ATTR_NO_WARN : 0);
}

static inline void dma_free_coherent(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle)
{
	return dma_free_attrs(dev, size, cpu_addr, dma_handle, 0);
}


static inline u64 dma_get_mask(struct device *dev)
{
	if (dev->dma_mask && *dev->dma_mask)
		return *dev->dma_mask;
	return DMA_BIT_MASK(32);
}

/*
 * Set both the DMA mask and the coherent DMA mask to the same thing.
 * Note that we don't check the return value from dma_set_coherent_mask()
 * as the DMA API guarantees that the coherent DMA mask can be set to
 * the same or smaller than the streaming DMA mask.
 */
static inline int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
	int rc = dma_set_mask(dev, mask);
	if (rc == 0)
		dma_set_coherent_mask(dev, mask);
	return rc;
}

/*
 * Similar to the above, except it deals with the case where the device
 * does not have dev->dma_mask appropriately setup.
 */
static inline int dma_coerce_mask_and_coherent(struct device *dev, u64 mask)
{
	dev->dma_mask = &dev->coherent_dma_mask;
	return dma_set_mask_and_coherent(dev, mask);
}

static inline unsigned int dma_get_max_seg_size(struct device *dev)
{
	if (dev->dma_parms && dev->dma_parms->max_segment_size)
		return dev->dma_parms->max_segment_size;
	return SZ_64K;
}

static inline int dma_set_max_seg_size(struct device *dev, unsigned int size)
{
	if (dev->dma_parms) {
		dev->dma_parms->max_segment_size = size;
		return 0;
	}
	return -EIO;
}

static inline unsigned long dma_get_seg_boundary(struct device *dev)
{
	if (dev->dma_parms && dev->dma_parms->segment_boundary_mask)
		return dev->dma_parms->segment_boundary_mask;
	return ULONG_MAX;
}

/**
 * dma_get_seg_boundary_nr_pages - return the segment boundary in "page" units
 * @dev: device to guery the boundary for
 * @page_shift: ilog() of the IOMMU page size
 *
 * Return the segment boundary in IOMMU page units (which may be different from
 * the CPU page size) for the passed in device.
 *
 * If @dev is NULL a boundary of U32_MAX is assumed, this case is just for
 * non-DMA API callers.
 */
static inline unsigned long dma_get_seg_boundary_nr_pages(struct device *dev,
		unsigned int page_shift)
{
	if (!dev)
		return (U32_MAX >> page_shift) + 1;
	return (dma_get_seg_boundary(dev) >> page_shift) + 1;
}

static inline int dma_set_seg_boundary(struct device *dev, unsigned long mask)
{
	if (dev->dma_parms) {
		dev->dma_parms->segment_boundary_mask = mask;
		return 0;
	}
	return -EIO;
}

static inline unsigned int dma_get_min_align_mask(struct device *dev)
{
	if (dev->dma_parms)
		return dev->dma_parms->min_align_mask;
	return 0;
}

static inline int dma_set_min_align_mask(struct device *dev,
		unsigned int min_align_mask)
{
	if (WARN_ON_ONCE(!dev->dma_parms))
		return -EIO;
	dev->dma_parms->min_align_mask = min_align_mask;
	return 0;
}

#ifndef dma_get_cache_alignment
static inline int dma_get_cache_alignment(void)
{
#ifdef ARCH_HAS_DMA_MINALIGN
	return ARCH_DMA_MINALIGN;
#endif
	return 1;
}
#endif

static inline void *dmam_alloc_coherent(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp)
{
	return dmam_alloc_attrs(dev, size, dma_handle, gfp,
			(gfp & __GFP_NOWARN) ? DMA_ATTR_NO_WARN : 0);
}

static inline void *dma_alloc_wc(struct device *dev, size_t size,
				 dma_addr_t *dma_addr, gfp_t gfp)
{
	unsigned long attrs = DMA_ATTR_WRITE_COMBINE;

	if (gfp & __GFP_NOWARN)
		attrs |= DMA_ATTR_NO_WARN;

	return dma_alloc_attrs(dev, size, dma_addr, gfp, attrs);
}

static inline void dma_free_wc(struct device *dev, size_t size,
			       void *cpu_addr, dma_addr_t dma_addr)
{
	return dma_free_attrs(dev, size, cpu_addr, dma_addr,
			      DMA_ATTR_WRITE_COMBINE);
}

static inline int dma_mmap_wc(struct device *dev,
			      struct vm_area_struct *vma,
			      void *cpu_addr, dma_addr_t dma_addr,
			      size_t size)
{
	return dma_mmap_attrs(dev, vma, cpu_addr, dma_addr, size,
			      DMA_ATTR_WRITE_COMBINE);
}

#ifdef CONFIG_NEED_DMA_MAP_STATE
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)        dma_addr_t ADDR_NAME
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)          __u32 LEN_NAME
#define dma_unmap_addr(PTR, ADDR_NAME)           ((PTR)->ADDR_NAME)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  (((PTR)->ADDR_NAME) = (VAL))
#define dma_unmap_len(PTR, LEN_NAME)             ((PTR)->LEN_NAME)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    (((PTR)->LEN_NAME) = (VAL))
#else
#define DEFINE_DMA_UNMAP_ADDR(ADDR_NAME)
#define DEFINE_DMA_UNMAP_LEN(LEN_NAME)
#define dma_unmap_addr(PTR, ADDR_NAME)           (0)
#define dma_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define dma_unmap_len(PTR, LEN_NAME)             (0)
#define dma_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)
#endif

#endif /* _LINUX_DMA_MAPPING_H */
