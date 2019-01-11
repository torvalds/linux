// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Christoph Hellwig.
 *
 * DMA operations that map physical memory directly without providing cache
 * coherence.
 */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/scatterlist.h>

static void dma_noncoherent_sync_single_for_device(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	arch_sync_dma_for_device(dev, dma_to_phys(dev, addr), size, dir);
}

static void dma_noncoherent_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		arch_sync_dma_for_device(dev, sg_phys(sg), sg->length, dir);
}

static dma_addr_t dma_noncoherent_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	dma_addr_t addr;

	addr = dma_direct_map_page(dev, page, offset, size, dir, attrs);
	if (!dma_mapping_error(dev, addr) && !(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_device(dev, page_to_phys(page) + offset,
				size, dir);
	return addr;
}

static int dma_noncoherent_map_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	nents = dma_direct_map_sg(dev, sgl, nents, dir, attrs);
	if (nents > 0 && !(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_noncoherent_sync_sg_for_device(dev, sgl, nents, dir);
	return nents;
}

#ifdef CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU
static void dma_noncoherent_sync_single_for_cpu(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	arch_sync_dma_for_cpu(dev, dma_to_phys(dev, addr), size, dir);
}

static void dma_noncoherent_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		arch_sync_dma_for_cpu(dev, sg_phys(sg), sg->length, dir);
}

static void dma_noncoherent_unmap_page(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_noncoherent_sync_single_for_cpu(dev, addr, size, dir);
}

static void dma_noncoherent_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_noncoherent_sync_sg_for_cpu(dev, sgl, nents, dir);
}
#endif

const struct dma_map_ops dma_noncoherent_ops = {
	.alloc			= arch_dma_alloc,
	.free			= arch_dma_free,
	.mmap			= arch_dma_mmap,
	.sync_single_for_device	= dma_noncoherent_sync_single_for_device,
	.sync_sg_for_device	= dma_noncoherent_sync_sg_for_device,
	.map_page		= dma_noncoherent_map_page,
	.map_sg			= dma_noncoherent_map_sg,
#ifdef CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU
	.sync_single_for_cpu	= dma_noncoherent_sync_single_for_cpu,
	.sync_sg_for_cpu	= dma_noncoherent_sync_sg_for_cpu,
	.unmap_page		= dma_noncoherent_unmap_page,
	.unmap_sg		= dma_noncoherent_unmap_sg,
#endif
	.dma_supported		= dma_direct_supported,
	.mapping_error		= dma_direct_mapping_error,
	.cache_sync		= arch_dma_cache_sync,
};
EXPORT_SYMBOL(dma_noncoherent_ops);
