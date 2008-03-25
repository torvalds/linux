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

/* same for gart, swiotlb, and nommu */
static inline int dma_get_cache_alignment(void)
{
	return boot_cpu_data.x86_clflush_size;
}

#define dma_is_consistent(d, h) 1

extern struct device fallback_dev;
extern int panic_on_overflow;

#endif /* _X8664_DMA_MAPPING_H */
