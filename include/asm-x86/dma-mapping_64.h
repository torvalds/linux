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
