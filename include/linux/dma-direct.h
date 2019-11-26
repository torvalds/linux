/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DMA_DIRECT_H
#define _LINUX_DMA_DIRECT_H 1

#include <linux/dma-mapping.h>
#include <linux/mem_encrypt.h>

extern unsigned int zone_dma_bits;

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

	return addr + size - 1 <=
		min_not_zero(*dev->dma_mask, dev->bus_dma_mask);
}
#endif /* !CONFIG_ARCH_HAS_PHYS_TO_DMA */

#ifdef CONFIG_ARCH_HAS_FORCE_DMA_UNENCRYPTED
bool force_dma_unencrypted(struct device *dev);
#else
static inline bool force_dma_unencrypted(struct device *dev)
{
	return false;
}
#endif /* CONFIG_ARCH_HAS_FORCE_DMA_UNENCRYPTED */

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

u64 dma_direct_get_required_mask(struct device *dev);
void *dma_direct_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs);
void dma_direct_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);
void *dma_direct_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs);
void dma_direct_free_pages(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs);
struct page *__dma_direct_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs);
void __dma_direct_free_pages(struct device *dev, size_t size, struct page *page);
int dma_direct_supported(struct device *dev, u64 mask);
#endif /* _LINUX_DMA_DIRECT_H */
