#ifndef _ASM_GENERIC_DMA_MAPPING_H
#define _ASM_GENERIC_DMA_MAPPING_H

/* This is used for archs that do not support DMA */


static inline void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   int flag)
{
	BUG();
	return NULL;
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		    dma_addr_t dma_handle)
{
	BUG();
}

#endif /* _ASM_GENERIC_DMA_MAPPING_H */
