/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_MAPPING_H
#define _LINUX_DMA_MAPPING_H

#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/dma-debug.h>
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
 * DMA_ATTR_NON_CONSISTENT: Lets the platform to choose to return either
 * consistent or non-consistent memory as it sees fit.
 */
#define DMA_ATTR_NON_CONSISTENT		(1UL << 3)
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
 * A dma_addr_t can hold any valid DMA or bus address for the platform.
 * It can be given to a device to use as a DMA source or target.  A CPU cannot
 * reference a dma_addr_t directly because there may be translation between
 * its physical address space and the bus address space.
 */
struct dma_map_ops {
	void* (*alloc)(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp,
				unsigned long attrs);
	void (*free)(struct device *dev, size_t size,
			      void *vaddr, dma_addr_t dma_handle,
			      unsigned long attrs);
	int (*mmap)(struct device *, struct vm_area_struct *,
			  void *, dma_addr_t, size_t,
			  unsigned long attrs);

	int (*get_sgtable)(struct device *dev, struct sg_table *sgt, void *,
			   dma_addr_t, size_t, unsigned long attrs);

	dma_addr_t (*map_page)(struct device *dev, struct page *page,
			       unsigned long offset, size_t size,
			       enum dma_data_direction dir,
			       unsigned long attrs);
	void (*unmap_page)(struct device *dev, dma_addr_t dma_handle,
			   size_t size, enum dma_data_direction dir,
			   unsigned long attrs);
	/*
	 * map_sg returns 0 on error and a value > 0 on success.
	 * It should never return a value < 0.
	 */
	int (*map_sg)(struct device *dev, struct scatterlist *sg,
		      int nents, enum dma_data_direction dir,
		      unsigned long attrs);
	void (*unmap_sg)(struct device *dev,
			 struct scatterlist *sg, int nents,
			 enum dma_data_direction dir,
			 unsigned long attrs);
	dma_addr_t (*map_resource)(struct device *dev, phys_addr_t phys_addr,
			       size_t size, enum dma_data_direction dir,
			       unsigned long attrs);
	void (*unmap_resource)(struct device *dev, dma_addr_t dma_handle,
			   size_t size, enum dma_data_direction dir,
			   unsigned long attrs);
	void (*sync_single_for_cpu)(struct device *dev,
				    dma_addr_t dma_handle, size_t size,
				    enum dma_data_direction dir);
	void (*sync_single_for_device)(struct device *dev,
				       dma_addr_t dma_handle, size_t size,
				       enum dma_data_direction dir);
	void (*sync_sg_for_cpu)(struct device *dev,
				struct scatterlist *sg, int nents,
				enum dma_data_direction dir);
	void (*sync_sg_for_device)(struct device *dev,
				   struct scatterlist *sg, int nents,
				   enum dma_data_direction dir);
	void (*cache_sync)(struct device *dev, void *vaddr, size_t size,
			enum dma_data_direction direction);
	int (*dma_supported)(struct device *dev, u64 mask);
	u64 (*get_required_mask)(struct device *dev);
	size_t (*max_mapping_size)(struct device *dev);
	unsigned long (*get_merge_boundary)(struct device *dev);
};

#define DMA_MAPPING_ERROR		(~(dma_addr_t)0)

extern const struct dma_map_ops dma_virt_ops;
extern const struct dma_map_ops dma_dummy_ops;

#define DMA_BIT_MASK(n)	(((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))

#define DMA_MASK_NONE	0x0ULL

static inline int valid_dma_direction(int dma_direction)
{
	return ((dma_direction == DMA_BIDIRECTIONAL) ||
		(dma_direction == DMA_TO_DEVICE) ||
		(dma_direction == DMA_FROM_DEVICE));
}

#ifdef CONFIG_DMA_DECLARE_COHERENT
/*
 * These three functions are only for dma allocator.
 * Don't use them in device drivers.
 */
int dma_alloc_from_dev_coherent(struct device *dev, ssize_t size,
				       dma_addr_t *dma_handle, void **ret);
int dma_release_from_dev_coherent(struct device *dev, int order, void *vaddr);

int dma_mmap_from_dev_coherent(struct device *dev, struct vm_area_struct *vma,
			    void *cpu_addr, size_t size, int *ret);

void *dma_alloc_from_global_coherent(struct device *dev, ssize_t size, dma_addr_t *dma_handle);
int dma_release_from_global_coherent(int order, void *vaddr);
int dma_mmap_from_global_coherent(struct vm_area_struct *vma, void *cpu_addr,
				  size_t size, int *ret);

#else
#define dma_alloc_from_dev_coherent(dev, size, handle, ret) (0)
#define dma_release_from_dev_coherent(dev, order, vaddr) (0)
#define dma_mmap_from_dev_coherent(dev, vma, vaddr, order, ret) (0)

static inline void *dma_alloc_from_global_coherent(struct device *dev, ssize_t size,
						   dma_addr_t *dma_handle)
{
	return NULL;
}

static inline int dma_release_from_global_coherent(int order, void *vaddr)
{
	return 0;
}

static inline int dma_mmap_from_global_coherent(struct vm_area_struct *vma,
						void *cpu_addr, size_t size,
						int *ret)
{
	return 0;
}
#endif /* CONFIG_DMA_DECLARE_COHERENT */

#ifdef CONFIG_HAS_DMA
#include <asm/dma-mapping.h>

#ifdef CONFIG_DMA_OPS
static inline const struct dma_map_ops *get_dma_ops(struct device *dev)
{
	if (dev->dma_ops)
		return dev->dma_ops;
	return get_arch_dma_ops(dev->bus);
}

static inline void set_dma_ops(struct device *dev,
			       const struct dma_map_ops *dma_ops)
{
	dev->dma_ops = dma_ops;
}
#else /* CONFIG_DMA_OPS */
static inline const struct dma_map_ops *get_dma_ops(struct device *dev)
{
	return NULL;
}
static inline void set_dma_ops(struct device *dev,
			       const struct dma_map_ops *dma_ops)
{
}
#endif /* CONFIG_DMA_OPS */

static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	debug_dma_mapping_error(dev, dma_addr);

	if (dma_addr == DMA_MAPPING_ERROR)
		return -ENOMEM;
	return 0;
}

dma_addr_t dma_map_page_attrs(struct device *dev, struct page *page,
		size_t offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs);
void dma_unmap_page_attrs(struct device *dev, dma_addr_t addr, size_t size,
		enum dma_data_direction dir, unsigned long attrs);
int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg, int nents,
		enum dma_data_direction dir, unsigned long attrs);
void dma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
				      int nents, enum dma_data_direction dir,
				      unsigned long attrs);
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
void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction dir);
int dma_get_sgtable_attrs(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int dma_mmap_attrs(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
bool dma_can_mmap(struct device *dev);
int dma_supported(struct device *dev, u64 mask);
int dma_set_mask(struct device *dev, u64 mask);
int dma_set_coherent_mask(struct device *dev, u64 mask);
u64 dma_get_required_mask(struct device *dev);
size_t dma_max_mapping_size(struct device *dev);
bool dma_need_sync(struct device *dev, dma_addr_t dma_addr);
unsigned long dma_get_merge_boundary(struct device *dev);
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
static inline int dma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	return 0;
}
static inline void dma_unmap_sg_attrs(struct device *dev,
		struct scatterlist *sg, int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
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
static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction dir)
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
static inline int dma_supported(struct device *dev, u64 mask)
{
	return 0;
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
static inline size_t dma_max_mapping_size(struct device *dev)
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
#endif /* CONFIG_HAS_DMA */

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
 * dma_map_sgtable - Map the given buffer for DMA
 * @dev:	The device for which to perform the DMA operation
 * @sgt:	The sg_table object describing the buffer
 * @dir:	DMA direction
 * @attrs:	Optional DMA attributes for the map operation
 *
 * Maps a buffer described by a scatterlist stored in the given sg_table
 * object for the @dir DMA operation by the @dev device. After success the
 * ownership for the buffer is transferred to the DMA domain.  One has to
 * call dma_sync_sgtable_for_cpu() or dma_unmap_sgtable() to move the
 * ownership of the buffer back to the CPU domain before touching the
 * buffer by the CPU.
 *
 * Returns 0 on success or -EINVAL on error during mapping the buffer.
 */
static inline int dma_map_sgtable(struct device *dev, struct sg_table *sgt,
		enum dma_data_direction dir, unsigned long attrs)
{
	int nents;

	nents = dma_map_sg_attrs(dev, sgt->sgl, sgt->orig_nents, dir, attrs);
	if (nents <= 0)
		return -EINVAL;
	sgt->nents = nents;
	return 0;
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

extern int dma_common_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);

struct page **dma_common_find_pages(void *cpu_addr);
void *dma_common_contiguous_remap(struct page *page, size_t size,
			pgprot_t prot, const void *caller);

void *dma_common_pages_remap(struct page **pages, size_t size,
			pgprot_t prot, const void *caller);
void dma_common_free_remap(void *cpu_addr, size_t size);

struct page *dma_alloc_from_pool(struct device *dev, size_t size,
		void **cpu_addr, gfp_t flags,
		bool (*phys_addr_ok)(struct device *, phys_addr_t, size_t));
bool dma_free_from_pool(struct device *dev, void *start, size_t size);

int
dma_common_get_sgtable(struct device *dev, struct sg_table *sgt, void *cpu_addr,
		dma_addr_t dma_addr, size_t size, unsigned long attrs);

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

/**
 * dma_addressing_limited - return if the device is addressing limited
 * @dev:	device to check
 *
 * Return %true if the devices DMA mask is too small to address all memory in
 * the system, else %false.  Lack of addressing bits is the prime reason for
 * bounce buffering, but might not be the only one.
 */
static inline bool dma_addressing_limited(struct device *dev)
{
	return min_not_zero(dma_get_mask(dev), dev->bus_dma_limit) <
			    dma_get_required_mask(dev);
}

#ifdef CONFIG_ARCH_HAS_SETUP_DMA_OPS
void arch_setup_dma_ops(struct device *dev, u64 dma_base, u64 size,
		const struct iommu_ops *iommu, bool coherent);
#else
static inline void arch_setup_dma_ops(struct device *dev, u64 dma_base,
		u64 size, const struct iommu_ops *iommu, bool coherent)
{
}
#endif /* CONFIG_ARCH_HAS_SETUP_DMA_OPS */

#ifdef CONFIG_ARCH_HAS_TEARDOWN_DMA_OPS
void arch_teardown_dma_ops(struct device *dev);
#else
static inline void arch_teardown_dma_ops(struct device *dev)
{
}
#endif /* CONFIG_ARCH_HAS_TEARDOWN_DMA_OPS */

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
	return DMA_BIT_MASK(32);
}

static inline int dma_set_seg_boundary(struct device *dev, unsigned long mask)
{
	if (dev->dma_parms) {
		dev->dma_parms->segment_boundary_mask = mask;
		return 0;
	}
	return -EIO;
}

static inline int dma_get_cache_alignment(void)
{
#ifdef ARCH_DMA_MINALIGN
	return ARCH_DMA_MINALIGN;
#endif
	return 1;
}

#ifdef CONFIG_DMA_DECLARE_COHERENT
int dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
				dma_addr_t device_addr, size_t size);
#else
static inline int
dma_declare_coherent_memory(struct device *dev, phys_addr_t phys_addr,
			    dma_addr_t device_addr, size_t size)
{
	return -ENOSYS;
}
#endif /* CONFIG_DMA_DECLARE_COHERENT */

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

#endif
