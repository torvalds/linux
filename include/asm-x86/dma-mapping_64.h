#ifndef _X8664_DMA_MAPPING_H
#define _X8664_DMA_MAPPING_H 1

extern dma_addr_t bad_dma_address;
extern int iommu_merge;

static inline int dma_mapping_error(dma_addr_t dma_addr)
{
	if (dma_ops->mapping_error)
		return dma_ops->mapping_error(dma_addr);

	return (dma_addr == bad_dma_address);
}

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent(d, s, h, f)
#define dma_free_noncoherent(d, s, v, h) dma_free_coherent(d, s, v, h)

extern void *dma_alloc_coherent(struct device *dev, size_t size,
				dma_addr_t *dma_handle, gfp_t gfp);
extern void dma_free_coherent(struct device *dev, size_t size, void *vaddr,
			      dma_addr_t dma_handle);

#define dma_map_page(dev,page,offset,size,dir) \
	dma_map_single((dev), page_address(page)+(offset), (size), (dir))

#define dma_unmap_page dma_unmap_single

static inline void
dma_sync_single_range_for_device(struct device *hwdev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_single_range_for_device)
		dma_ops->sync_single_range_for_device(hwdev, dma_handle,
						      offset, size, direction);

	flush_write_buffers();
}

static inline void
dma_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sg,
		    int nelems, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_sg_for_cpu)
		dma_ops->sync_sg_for_cpu(hwdev, sg, nelems, direction);
	flush_write_buffers();
}

static inline void
dma_sync_sg_for_device(struct device *hwdev, struct scatterlist *sg,
		       int nelems, int direction)
{
	BUG_ON(!valid_dma_direction(direction));
	if (dma_ops->sync_sg_for_device) {
		dma_ops->sync_sg_for_device(hwdev, sg, nelems, direction);
	}

	flush_write_buffers();
}

extern int dma_supported(struct device *hwdev, u64 mask);

/* same for gart, swiotlb, and nommu */
static inline int dma_get_cache_alignment(void)
{
	return boot_cpu_data.x86_clflush_size;
}

#define dma_is_consistent(d, h) 1

extern int dma_set_mask(struct device *dev, u64 mask);

static inline void
dma_cache_sync(struct device *dev, void *vaddr, size_t size,
	enum dma_data_direction dir)
{
	flush_write_buffers();
}

extern struct device fallback_dev;
extern int panic_on_overflow;

#endif /* _X8664_DMA_MAPPING_H */
