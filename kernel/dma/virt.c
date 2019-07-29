// SPDX-License-Identifier: GPL-2.0
/*
 * DMA operations that map to virtual addresses without flushing memory.
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

static void *dma_virt_alloc(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t gfp,
			    unsigned long attrs)
{
	void *ret;

	ret = (void *)__get_free_pages(gfp | __GFP_ZERO, get_order(size));
	if (ret)
		*dma_handle = (uintptr_t)ret;
	return ret;
}

static void dma_virt_free(struct device *dev, size_t size,
			  void *cpu_addr, dma_addr_t dma_addr,
			  unsigned long attrs)
{
	free_pages((unsigned long)cpu_addr, get_order(size));
}

static dma_addr_t dma_virt_map_page(struct device *dev, struct page *page,
				    unsigned long offset, size_t size,
				    enum dma_data_direction dir,
				    unsigned long attrs)
{
	return (uintptr_t)(page_address(page) + offset);
}

static int dma_virt_map_sg(struct device *dev, struct scatterlist *sgl,
			   int nents, enum dma_data_direction dir,
			   unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		BUG_ON(!sg_page(sg));
		sg_dma_address(sg) = (uintptr_t)sg_virt(sg);
		sg_dma_len(sg) = sg->length;
	}

	return nents;
}

const struct dma_map_ops dma_virt_ops = {
	.alloc			= dma_virt_alloc,
	.free			= dma_virt_free,
	.map_page		= dma_virt_map_page,
	.map_sg			= dma_virt_map_sg,
};
EXPORT_SYMBOL(dma_virt_ops);
