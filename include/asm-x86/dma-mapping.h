#ifndef _ASM_DMA_MAPPING_H_
#define _ASM_DMA_MAPPING_H_

/*
 * IOMMU interface. See Documentation/DMA-mapping.txt and DMA-API.txt for
 * documentation.
 */

#include <linux/scatterlist.h>
#include <asm/io.h>
#include <asm/swiotlb.h>
#include <asm-generic/dma-coherent.h>

extern dma_addr_t bad_dma_address;
extern int iommu_merge;
extern struct device x86_dma_fallback_dev;
extern int panic_on_overflow;
extern int force_iommu;

struct dma_mapping_ops {
	int             (*mapping_error)(struct device *dev,
					 dma_addr_t dma_addr);
	void*           (*alloc_coherent)(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp);
	void            (*free_coherent)(struct device *dev, size_t size,
				void *vaddr, dma_addr_t dma_handle);
	dma_addr_t      (*map_single)(struct device *hwdev, phys_addr_t ptr,
				size_t size, int direction);
	void            (*unmap_single)(struct device *dev, dma_addr_t addr,
				size_t size, int direction);
	void            (*sync_single_for_cpu)(struct device *hwdev,
				dma_addr_t dma_handle, size_t size,
				int direction);
	void            (*sync_single_for_device)(struct device *hwdev,
				dma_addr_t dma_handle, size_t size,
				int direction);
	void            (*sync_single_range_for_cpu)(struct device *hwdev,
				dma_addr_t dma_handle, unsigned long offset,
				size_t size, int direction);
	void            (*sync_single_range_for_device)(struct device *hwdev,
				dma_addr_t dma_handle, unsigned long offset,
				size_t size, int direction);
	void            (*sync_sg_for_cpu)(struct device *hwdev,
				struct scatterlist *sg, int nelems,
				int direction);
	void            (*sync_sg_for_device)(struct device *hwdev,
				struct scatterlist *sg, int nelems,
				int direction);
	int             (*map_sg)(struct device *hwdev, struct scatterlist *sg,
				int nents, int direction);
	void            (*unmap_sg)(struct device *hwdev,
				struct scatterlist *sg, int nents,
				int direction);
	int             (*dma_supported)(struct device *hwdev, u64 mask);
	int		is_phys;
};

extern struct dma_mapping_ops *dma_ops;

static inline struct dma_mapping_ops *get_dma_ops(struct device *dev)
{
#ifdef CONFIG_X86_32
	return dma_ops;
#else
	if (unlikely(!dev) || !dev->archdata.dma_ops)
		return dma_ops;
	else
		return dev->archdata.dma_ops;
#endif
}

/* Make sure we keep the same behaviour */
static inline int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
#ifdef CONFIG_X86_32
	return 0;
#else
	struct dma_mapping_ops *ops = get_dma_ops(dev);
	if (ops->mapping_error)
		return ops->mapping_error(dev, dma_addr);

	return (dma_addr == bad_dma_address);
#endif
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#define dma_is_consistent(d, h)	(1)

extern int dma_supported(struct device *hwdev, u64 mask);
extern int dma_set_mask(struct device *dev, u64 mask);

static inline dma_addr_t
dma_map_single(struct device *hwdev, void *ptr, size_t size,
	       int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	return ops->map_single(hwdev, virt_to_phys(ptr), size, direction);
}

static inline void
dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size,
		 int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->unmap_single)
		ops->unmap_single(dev, addr, size, direction);
}

static inline int
dma_map_sg(struct device *hwdev, struct scatterlist *sg,
	   int nents, int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	return ops->map_sg(hwdev, sg, nents, direction);
}

static inline void
dma_unmap_sg(struct device *hwdev, struct scatterlist *sg, int nents,
	     int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->unmap_sg)
		ops->unmap_sg(hwdev, sg, nents, direction);
}

static inline void
dma_sync_single_for_cpu(struct device *hwdev, dma_addr_t dma_handle,
			size_t size, int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->sync_single_for_cpu)
		ops->sync_single_for_cpu(hwdev, dma_handle, size, direction);
	flush_write_buffers();
}

static inline void
dma_sync_single_for_device(struct device *hwdev, dma_addr_t dma_handle,
			   size_t size, int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->sync_single_for_device)
		ops->sync_single_for_device(hwdev, dma_handle, size, direction);
	flush_write_buffers();
}

static inline void
dma_sync_single_range_for_cpu(struct device *hwdev, dma_addr_t dma_handle,
			      unsigned long offset, size_t size, int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->sync_single_range_for_cpu)
		ops->sync_single_range_for_cpu(hwdev, dma_handle, offset,
					       size, direction);
	flush_write_buffers();
}

static inline void
dma_sync_single_range_for_device(struct device *hwdev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->sync_single_range_for_device)
		ops->sync_single_range_for_device(hwdev, dma_handle,
						  offset, size, direction);
	flush_write_buffers();
}

static inline void
dma_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sg,
		    int nelems, int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->sync_sg_for_cpu)
		ops->sync_sg_for_cpu(hwdev, sg, nelems, direction);
	flush_write_buffers();
}

static inline void
dma_sync_sg_for_device(struct device *hwdev, struct scatterlist *sg,
		       int nelems, int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(hwdev);

	BUG_ON(!valid_dma_direction(direction));
	if (ops->sync_sg_for_device)
		ops->sync_sg_for_device(hwdev, sg, nelems, direction);

	flush_write_buffers();
}

static inline dma_addr_t dma_map_page(struct device *dev, struct page *page,
				      size_t offset, size_t size,
				      int direction)
{
	struct dma_mapping_ops *ops = get_dma_ops(dev);

	BUG_ON(!valid_dma_direction(direction));
	return ops->map_single(dev, page_to_phys(page) + offset,
			       size, direction);
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t addr,
				  size_t size, int direction)
{
	dma_unmap_single(dev, addr, size, direction);
}

static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	enum dma_data_direction dir)
{
	flush_write_buffers();
}

static inline int dma_get_cache_alignment(void)
{
	/* no easy way to get cache size on all x86, so return the
	 * maximum possible, to be safe */
	return boot_cpu_data.x86_clflush_size;
}

static inline unsigned long dma_alloc_coherent_mask(struct device *dev,
						    gfp_t gfp)
{
	unsigned long dma_mask = 0;

	dma_mask = dev->coherent_dma_mask;
	if (!dma_mask)
		dma_mask = (gfp & GFP_DMA) ? DMA_24BIT_MASK : DMA_32BIT_MASK;

	return dma_mask;
}

static inline gfp_t dma_alloc_coherent_gfp_flags(struct device *dev, gfp_t gfp)
{
#ifdef CONFIG_X86_64
	unsigned long dma_mask = dma_alloc_coherent_mask(dev, gfp);

	if (dma_mask <= DMA_32BIT_MASK && !(gfp & GFP_DMA))
		gfp |= GFP_DMA32;
#endif
       return gfp;
}

static inline void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp)
{
	struct dma_mapping_ops *ops = get_dma_ops(dev);
	void *memory;

	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM | __GFP_DMA32);

	if (dma_alloc_from_coherent(dev, size, dma_handle, &memory))
		return memory;

	if (!dev) {
		dev = &x86_dma_fallback_dev;
		gfp |= GFP_DMA;
	}

	if (!dev->dma_mask)
		return NULL;

	if (!ops->alloc_coherent)
		return NULL;

	return ops->alloc_coherent(dev, size, dma_handle,
				   dma_alloc_coherent_gfp_flags(dev, gfp));
}

static inline void dma_free_coherent(struct device *dev, size_t size,
				     void *vaddr, dma_addr_t bus)
{
	struct dma_mapping_ops *ops = get_dma_ops(dev);

	WARN_ON(irqs_disabled());       /* for portability */

	if (dma_release_from_coherent(dev, get_order(size), vaddr))
		return;

	if (ops->free_coherent)
		ops->free_coherent(dev, size, vaddr, bus);
}

#endif
