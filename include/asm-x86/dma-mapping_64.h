#ifndef _X8664_DMA_MAPPING_H
#define _X8664_DMA_MAPPING_H 1

extern int iommu_merge;

/* same for gart, swiotlb, and nommu */
static inline int dma_get_cache_alignment(void)
{
	return boot_cpu_data.x86_clflush_size;
}

#define dma_is_consistent(d, h) 1

extern struct device fallback_dev;
extern int panic_on_overflow;

#endif /* _X8664_DMA_MAPPING_H */
