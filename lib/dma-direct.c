// SPDX-License-Identifier: GPL-2.0
/*
 * DMA operations that map physical memory directly without using an IOMMU or
 * flushing caches.
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-direct.h>
#include <linux/scatterlist.h>
#include <linux/dma-contiguous.h>
#include <linux/pfn.h>

#define DIRECT_MAPPING_ERROR		0

/*
 * Most architectures use ZONE_DMA for the first 16 Megabytes, but
 * some use it for entirely different regions:
 */
#ifndef ARCH_ZONE_DMA_BITS
#define ARCH_ZONE_DMA_BITS 24
#endif

static bool
check_addr(struct device *dev, dma_addr_t dma_addr, size_t size,
		const char *caller)
{
	if (unlikely(dev && !dma_capable(dev, dma_addr, size))) {
		if (*dev->dma_mask >= DMA_BIT_MASK(32)) {
			dev_err(dev,
				"%s: overflow %pad+%zu of device mask %llx\n",
				caller, &dma_addr, size, *dev->dma_mask);
		}
		return false;
	}
	return true;
}

static bool dma_coherent_ok(struct device *dev, phys_addr_t phys, size_t size)
{
	return phys_to_dma(dev, phys) + size - 1 <= dev->coherent_dma_mask;
}

void *dma_direct_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	int page_order = get_order(size);
	struct page *page = NULL;

	/* GFP_DMA32 and GFP_DMA are no ops without the corresponding zones: */
	if (dev->coherent_dma_mask <= DMA_BIT_MASK(ARCH_ZONE_DMA_BITS))
		gfp |= GFP_DMA;
	if (dev->coherent_dma_mask <= DMA_BIT_MASK(32) && !(gfp & GFP_DMA))
		gfp |= GFP_DMA32;

again:
	/* CMA can be used only in the context which permits sleeping */
	if (gfpflags_allow_blocking(gfp)) {
		page = dma_alloc_from_contiguous(dev, count, page_order, gfp);
		if (page && !dma_coherent_ok(dev, page_to_phys(page), size)) {
			dma_release_from_contiguous(dev, page, count);
			page = NULL;
		}
	}
	if (!page)
		page = alloc_pages_node(dev_to_node(dev), gfp, page_order);

	if (page && !dma_coherent_ok(dev, page_to_phys(page), size)) {
		__free_pages(page, page_order);
		page = NULL;

		if (dev->coherent_dma_mask < DMA_BIT_MASK(32) &&
		    !(gfp & GFP_DMA)) {
			gfp = (gfp & ~GFP_DMA32) | GFP_DMA;
			goto again;
		}
	}

	if (!page)
		return NULL;

	*dma_handle = phys_to_dma(dev, page_to_phys(page));
	memset(page_address(page), 0, size);
	return page_address(page);
}

void dma_direct_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;

	if (!dma_release_from_contiguous(dev, virt_to_page(cpu_addr), count))
		free_pages((unsigned long)cpu_addr, get_order(size));
}

static dma_addr_t dma_direct_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	dma_addr_t dma_addr = phys_to_dma(dev, page_to_phys(page)) + offset;

	if (!check_addr(dev, dma_addr, size, __func__))
		return DIRECT_MAPPING_ERROR;
	return dma_addr;
}

static int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		BUG_ON(!sg_page(sg));

		sg_dma_address(sg) = phys_to_dma(dev, sg_phys(sg));
		if (!check_addr(dev, sg_dma_address(sg), sg->length, __func__))
			return 0;
		sg_dma_len(sg) = sg->length;
	}

	return nents;
}

int dma_direct_supported(struct device *dev, u64 mask)
{
#ifdef CONFIG_ZONE_DMA
	if (mask < DMA_BIT_MASK(ARCH_ZONE_DMA_BITS))
		return 0;
#else
	/*
	 * Because 32-bit DMA masks are so common we expect every architecture
	 * to be able to satisfy them - either by not supporting more physical
	 * memory, or by providing a ZONE_DMA32.  If neither is the case, the
	 * architecture needs to use an IOMMU instead of the direct mapping.
	 */
	if (mask < DMA_BIT_MASK(32))
		return 0;
#endif
	return 1;
}

static int dma_direct_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr == DIRECT_MAPPING_ERROR;
}

const struct dma_map_ops dma_direct_ops = {
	.alloc			= dma_direct_alloc,
	.free			= dma_direct_free,
	.map_page		= dma_direct_map_page,
	.map_sg			= dma_direct_map_sg,
	.dma_supported		= dma_direct_supported,
	.mapping_error		= dma_direct_mapping_error,
};
EXPORT_SYMBOL(dma_direct_ops);
