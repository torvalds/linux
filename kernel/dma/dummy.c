// SPDX-License-Identifier: GPL-2.0
/*
 * Dummy DMA ops that always fail.
 */
#include <linux/dma-map-ops.h>

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
static void dma_dummy_unmap_page(struct device *dev, dma_addr_t dma_handle,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	/*
	 * Dummy ops doesn't support map_page, so unmap_page should never be
	 * called.
	 */
	WARN_ON_ONCE(true);
}

static int dma_dummy_map_sg(struct device *dev, struct scatterlist *sgl,
		int nelems, enum dma_data_direction dir,
		unsigned long attrs)
{
	return -EINVAL;
}

static void dma_dummy_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nelems, enum dma_data_direction dir,
		unsigned long attrs)
{
	/*
	 * Dummy ops doesn't support map_sg, so unmap_sg should never be called.
	 */
	WARN_ON_ONCE(true);
}

static int dma_dummy_supported(struct device *hwdev, u64 mask)
{
	return 0;
}

const struct dma_map_ops dma_dummy_ops = {
	.mmap                   = dma_dummy_mmap,
	.map_page               = dma_dummy_map_page,
	.unmap_page             = dma_dummy_unmap_page,
	.map_sg                 = dma_dummy_map_sg,
	.unmap_sg               = dma_dummy_unmap_sg,
	.dma_supported          = dma_dummy_supported,
};
