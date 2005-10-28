#ifndef _ASM_M32R_DMA_MAPPING_H
#define _ASM_M32R_DMA_MAPPING_H

/*
 * NOTE: Do not include <asm-generic/dma-mapping.h>
 * Because it requires PCI stuffs, but current M32R don't provide these.
 */

static inline void *
dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle,
		   gfp_t flag)
{
	return (void *)NULL;
}

static inline void
dma_free_coherent(struct device *dev, size_t size, void *cpu_addr,
		    dma_addr_t dma_handle)
{
	return;
}

#endif /* _ASM_M32R_DMA_MAPPING_H */
