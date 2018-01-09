// SPDX-License-Identifier: GPL-2.0
/*
 * DMA operations that map physical memory directly without using an IOMMU or
 * flushing caches.
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-direct.h>
#include <linux/scatterlist.h>
#include <linux/pfn.h>

#define DIRECT_MAPPING_ERROR		0

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

static void *dma_direct_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	void *ret;

	ret = (void *)__get_free_pages(gfp, get_order(size));
	if (ret)
		*dma_handle = phys_to_dma(dev, virt_to_phys(ret));

	return ret;
}

static void dma_direct_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs)
{
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

static int dma_direct_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr == DIRECT_MAPPING_ERROR;
}

const struct dma_map_ops dma_direct_ops = {
	.alloc			= dma_direct_alloc,
	.free			= dma_direct_free,
	.map_page		= dma_direct_map_page,
	.map_sg			= dma_direct_map_sg,
	.mapping_error		= dma_direct_mapping_error,
};
EXPORT_SYMBOL(dma_direct_ops);
