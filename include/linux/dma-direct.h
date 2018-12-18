/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_DIRECT_H
#define _LINUX_DMA_DIRECT_H 1

#include <linux/dma-mapping.h>
#include <linux/mem_encrypt.h>

#ifdef CONFIG_ARCH_HAS_PHYS_TO_DMA
#include <asm/dma-direct.h>
#else
static inline dma_addr_t __phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	dma_addr_t dev_addr = (dma_addr_t)paddr;

	return dev_addr - ((dma_addr_t)dev->dma_pfn_offset << PAGE_SHIFT);
}

static inline phys_addr_t __dma_to_phys(struct device *dev, dma_addr_t dev_addr)
{
	phys_addr_t paddr = (phys_addr_t)dev_addr;

	return paddr + ((phys_addr_t)dev->dma_pfn_offset << PAGE_SHIFT);
}

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size)
{
	if (!dev->dma_mask)
		return false;

	return addr + size - 1 <= *dev->dma_mask;
}
#endif /* !CONFIG_ARCH_HAS_PHYS_TO_DMA */

/*
 * If memory encryption is supported, phys_to_dma will set the memory encryption
 * bit in the DMA address, and dma_to_phys will clear it.  The raw __phys_to_dma
 * and __dma_to_phys versions should only be used on non-encrypted memory for
 * special occasions like DMA coherent buffers.
 */
static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return __sme_set(__phys_to_dma(dev, paddr));
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t daddr)
{
	return __sme_clr(__dma_to_phys(dev, daddr));
}

#ifdef CONFIG_ARCH_HAS_DMA_MARK_CLEAN
void dma_mark_clean(void *addr, size_t size);
#else
static inline void dma_mark_clean(void *addr, size_t size)
{
}
#endif /* CONFIG_ARCH_HAS_DMA_MARK_CLEAN */

void *dma_direct_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void dma_direct_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);
dma_addr_t dma_direct_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs);
int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, unsigned long attrs);
int dma_direct_supported(struct device *dev, u64 mask);
int dma_direct_mapping_error(struct device *dev, dma_addr_t dma_addr);
#endif /* _LINUX_DMA_DIRECT_H */
