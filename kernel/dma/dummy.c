// SPDX-License-Identifier: GPL-2.0
/*
 * Dummy DMA ops that always fail.
 */
#include <linux/dma-mapping.h>

static int dma_dummy_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	return -ENXIO;
}

static dma_addr_t dma_dummy_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	return DMA_MAPPING_ERROR;
}

static int dma_dummy_map_sg(struct device *dev, struct scatterlist *sgl,
		int nelems, enum dma_data_direction dir,
		unsigned long attrs)
{
	return 0;
}

static int dma_dummy_supported(struct device *hwdev, u64 mask)
{
	return 0;
}

const struct dma_map_ops dma_dummy_ops = {
	.mmap                   = dma_dummy_mmap,
	.map_page               = dma_dummy_map_page,
	.map_sg                 = dma_dummy_map_sg,
	.dma_supported          = dma_dummy_supported,
};
EXPORT_SYMBOL(dma_dummy_ops);
