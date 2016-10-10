/*
 *	lib/dma-noop.c
 *
 * Simple DMA noop-ops that map 1:1 with memory
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>

static void *dma_noop_alloc(struct device *dev, size_t size,
			    dma_addr_t *dma_handle, gfp_t gfp,
			    unsigned long attrs)
{
	void *ret;

	ret = (void *)__get_free_pages(gfp, get_order(size));
	if (ret)
		*dma_handle = virt_to_phys(ret);
	return ret;
}

static void dma_noop_free(struct device *dev, size_t size,
			  void *cpu_addr, dma_addr_t dma_addr,
			  unsigned long attrs)
{
	free_pages((unsigned long)cpu_addr, get_order(size));
}

static dma_addr_t dma_noop_map_page(struct device *dev, struct page *page,
				      unsigned long offset, size_t size,
				      enum dma_data_direction dir,
				      unsigned long attrs)
{
	return page_to_phys(page) + offset;
}

static int dma_noop_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
			     enum dma_data_direction dir,
			     unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		void *va;

		BUG_ON(!sg_page(sg));
		va = sg_virt(sg);
		sg_dma_address(sg) = (dma_addr_t)virt_to_phys(va);
		sg_dma_len(sg) = sg->length;
	}

	return nents;
}

static int dma_noop_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

static int dma_noop_supported(struct device *dev, u64 mask)
{
	return 1;
}

struct dma_map_ops dma_noop_ops = {
	.alloc			= dma_noop_alloc,
	.free			= dma_noop_free,
	.map_page		= dma_noop_map_page,
	.map_sg			= dma_noop_map_sg,
	.mapping_error		= dma_noop_mapping_error,
	.dma_supported		= dma_noop_supported,
};

EXPORT_SYMBOL(dma_noop_ops);
