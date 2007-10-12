/*
 * Copyright (C) 2004 IBM
 *
 * Implements the generic device dma API for powerpc.
 * the pci and vio busses
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

static inline void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
				  size_t size,
				  enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->unmap_single(dev, dma_address, size, direction);
}

static inline int dma_map_sg(struct device *dev, struct scatterlist *sg,
			     int nents, enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	return dma_ops->map_sg(dev, sg, nents, direction);
}

static inline void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
				int nhwentries,
				enum dma_data_direction direction)
{
	struct dma_mapping_ops *dma_ops = get_dma_ops(dev);

	BUG_ON(!dma_ops);
	dma_ops->unmap_sg(dev, sg, nhwentries, direction);
}


/*
 * Available generic sets of operations
 */
extern struct dma_mapping_ops dma_iommu_ops;
extern struct dma_mapping_ops dma_direct_ops;

extern unsigned long dma_direct_offset;

#else /* CONFIG_PPC64 */

#define dma_supported(dev, mask)	(1)

static inline int dma_set_mask(struct device *dev, u64 dma_mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = dma_mask;

	return 0;
}

static inline void *dma_alloc_coherent(struct device *dev, size_t size,
				       dma_addr_t * dma_handle,
				       gfp_t gfp)
{
#ifdef CONFIG_NOT_COHERENT_CACHE
	return __dma_alloc_coherent(size, dma_handle, gfp);
#else
	void *ret;
	/* ignore region specifiers */
	gfp &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dev == NULL || dev->coherent_dma_mask < 0xffffffff)
		gfp |= GFP_DMA;

	ret = (void *)__get_free_pages(gfp, get_order(size));

	if (ret != NULL) {
		memset(ret, 0, size);
		*dma_handle = virt_to_bus(ret);
	}

	return ret;
#endif
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *vaddr,
		  dma_addr_t dma_handle)
{
#ifdef CONFIG_NOT_COHERENT_CACHE
	__dma_free_coherent(size, vaddr);
#else
	free_pages((unsigned long)vaddr, get_order(size));
#endif
}

static inline dma_addr_t
dma_map_single(struct device *dev, void *ptr, size_t size,
	       enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	__dma_sync(ptr, size, direction);

	return virt_to_bus(ptr);
}

static inline void dma_unmap_single(struct device *dev, dma_addr_t dma_addr,
				    size_t size,
				    enum dma_data_direction direction)
{
	/* We do nothing. */
}

static inline dma_addr_t
dma_map_page(struct device *dev, struct page *page,
	     unsigned long offset, size_t size,
	     enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);

	__dma_sync_page(page, offset, size, direction);

	return page_to_bus(page) + offset;
}

static inline void dma_unmap_page(struct device *dev, dma_addr_t dma_address,
				  size_t size,
				  enum dma_data_direction direction)
{
	/* We do nothing. */
}

static inline int
dma_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
	   enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(direction == DMA_NONE);

	for_each_sg(sgl, sg, nents, i) {
		BUG_ON(!sg->page);
		__dma_sync_page(sg->page, sg->offset, sg->length, direction);
		sg->dma_address = page_to_bus(sg->page) + sg->offset;
	}

	return nents;
}

static inline void dma_unmap_sg(struct device *dev, struct scatterlist *sg,
				int nhwentries,
				enum dma_data_direction direction)
{
	/* We don't do anything here. */
}

#endif /* CONFIG_PPC64 */

static inline void dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	__dma_sync(bus_to_virt(dma_handle), size, direction);
}

static inline void dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	__dma_sync(bus_to_virt(dma_handle), size, direction);
}

static inline void dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(direction == DMA_NONE);

	for_each_sg(sgl, sg, nents, i)
		__dma_sync_page(sg->page, sg->offset, sg->length, direction);
}

static inline void dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents,
		enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(direction == DMA_NONE);

	for_each_sg(sgl, sg, nents, i)
		__dma_sync_page(sg->page, sg->offset, sg->length, direction);
}

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
#ifdef CONFIG_PPC64
	return (dma_addr == DMA_ERROR_CODE);
#else
	return 0;
#endif
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)
#ifdef CONFIG_NOT_COHERENT_CACHE
#define dma_is_consistent(d, h)	(0)
#else
#define dma_is_consistent(d, h)	(1)
#endif

static inline int dma_get_cache_alignment(void)
{
#ifdef CONFIG_PPC64
	/* no easy way to get cache size on all processors, so return
	 * the maximum possible, to be safe */
	return (1 << INTERNODE_CACHE_SHIFT);
#else
	/*
	 * Each processor family will define its own L1_CACHE_SHIFT,
	 * L1_CACHE_BYTES wraps to this, so this is always safe.
	 */
	return L1_CACHE_BYTES;
#endif
}

static inline void dma_sync_single_range_for_cpu(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
	/* just sync everything for now */
	dma_sync_single_for_cpu(dev, dma_handle, offset + size, direction);
}

static inline void dma_sync_single_range_for_device(struct device *dev,
		dma_addr_t dma_handle, unsigned long offset, size_t size,
		enum dma_data_direction direction)
{
	/* just sync everything for now */
	dma_sync_single_for_device(dev, dma_handle, offset + size, direction);
}

static inline void dma_cache_sync(struct device *dev, void *vaddr, size_t size,
		enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	__dma_sync(vaddr, size, (int)direction);
}

#endif /* __KERNEL__ */
#endif	/* _ASM_DMA_MAPPING_H */
