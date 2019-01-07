// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Christoph Hellwig.
 *
 * DMA operations that map physical memory directly without using an IOMMU.
 */
#include <linux/memblock.h> /* for max_pfn */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-direct.h>
#include <linux/scatterlist.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-noncoherent.h>
#include <linux/pfn.h>
#include <linux/set_memory.h>

/*
 * Most architectures use ZONE_DMA for the first 16 Megabytes, but
 * some use it for entirely different regions:
 */
#ifndef ARCH_ZONE_DMA_BITS
#define ARCH_ZONE_DMA_BITS 24
#endif

/*
 * For AMD SEV all DMA must be to unencrypted addresses.
 */
static inline bool force_dma_unencrypted(void)
{
	return sev_active();
}

static bool
check_addr(struct device *dev, dma_addr_t dma_addr, size_t size,
		const char *caller)
{
	if (unlikely(dev && !dma_capable(dev, dma_addr, size))) {
		if (!dev->dma_mask) {
			dev_err(dev,
				"%s: call on device without dma_mask\n",
				caller);
			return false;
		}

		if (*dev->dma_mask >= DMA_BIT_MASK(32) || dev->bus_dma_mask) {
			dev_err(dev,
				"%s: overflow %pad+%zu of device mask %llx bus mask %llx\n",
				caller, &dma_addr, size,
				*dev->dma_mask, dev->bus_dma_mask);
		}
		return false;
	}
	return true;
}

static inline dma_addr_t phys_to_dma_direct(struct device *dev,
		phys_addr_t phys)
{
	if (force_dma_unencrypted())
		return __phys_to_dma(dev, phys);
	return phys_to_dma(dev, phys);
}

u64 dma_direct_get_required_mask(struct device *dev)
{
	u64 max_dma = phys_to_dma_direct(dev, (max_pfn - 1) << PAGE_SHIFT);

	if (dev->bus_dma_mask && dev->bus_dma_mask < max_dma)
		max_dma = dev->bus_dma_mask;

	return (1ULL << (fls64(max_dma) - 1)) * 2 - 1;
}

static gfp_t __dma_direct_optimal_gfp_mask(struct device *dev, u64 dma_mask,
		u64 *phys_mask)
{
	if (dev->bus_dma_mask && dev->bus_dma_mask < dma_mask)
		dma_mask = dev->bus_dma_mask;

	if (force_dma_unencrypted())
		*phys_mask = __dma_to_phys(dev, dma_mask);
	else
		*phys_mask = dma_to_phys(dev, dma_mask);

	/*
	 * Optimistically try the zone that the physical address mask falls
	 * into first.  If that returns memory that isn't actually addressable
	 * we will fallback to the next lower zone and try again.
	 *
	 * Note that GFP_DMA32 and GFP_DMA are no ops without the corresponding
	 * zones.
	 */
	if (*phys_mask <= DMA_BIT_MASK(ARCH_ZONE_DMA_BITS))
		return GFP_DMA;
	if (*phys_mask <= DMA_BIT_MASK(32))
		return GFP_DMA32;
	return 0;
}

static bool dma_coherent_ok(struct device *dev, phys_addr_t phys, size_t size)
{
	return phys_to_dma_direct(dev, phys) + size - 1 <=
			min_not_zero(dev->coherent_dma_mask, dev->bus_dma_mask);
}

void *dma_direct_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	int page_order = get_order(size);
	struct page *page = NULL;
	u64 phys_mask;
	void *ret;

	if (attrs & DMA_ATTR_NO_WARN)
		gfp |= __GFP_NOWARN;

	/* we always manually zero the memory once we are done: */
	gfp &= ~__GFP_ZERO;
	gfp |= __dma_direct_optimal_gfp_mask(dev, dev->coherent_dma_mask,
			&phys_mask);
again:
	/* CMA can be used only in the context which permits sleeping */
	if (gfpflags_allow_blocking(gfp)) {
		page = dma_alloc_from_contiguous(dev, count, page_order,
						 gfp & __GFP_NOWARN);
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

		if (IS_ENABLED(CONFIG_ZONE_DMA32) &&
		    phys_mask < DMA_BIT_MASK(64) &&
		    !(gfp & (GFP_DMA32 | GFP_DMA))) {
			gfp |= GFP_DMA32;
			goto again;
		}

		if (IS_ENABLED(CONFIG_ZONE_DMA) &&
		    phys_mask < DMA_BIT_MASK(32) && !(gfp & GFP_DMA)) {
			gfp = (gfp & ~GFP_DMA32) | GFP_DMA;
			goto again;
		}
	}

	if (!page)
		return NULL;
	ret = page_address(page);
	if (force_dma_unencrypted()) {
		set_memory_decrypted((unsigned long)ret, 1 << page_order);
		*dma_handle = __phys_to_dma(dev, page_to_phys(page));
	} else {
		*dma_handle = phys_to_dma(dev, page_to_phys(page));
	}
	memset(ret, 0, size);
	return ret;
}

/*
 * NOTE: this function must never look at the dma_addr argument, because we want
 * to be able to use it as a helper for iommu implementations as well.
 */
void dma_direct_free_pages(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs)
{
	unsigned int count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned int page_order = get_order(size);

	if (force_dma_unencrypted())
		set_memory_encrypted((unsigned long)cpu_addr, 1 << page_order);
	if (!dma_release_from_contiguous(dev, virt_to_page(cpu_addr), count))
		free_pages((unsigned long)cpu_addr, page_order);
}

void *dma_direct_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	if (!dev_is_dma_coherent(dev))
		return arch_dma_alloc(dev, size, dma_handle, gfp, attrs);
	return dma_direct_alloc_pages(dev, size, dma_handle, gfp, attrs);
}

void dma_direct_free(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t dma_addr, unsigned long attrs)
{
	if (!dev_is_dma_coherent(dev))
		arch_dma_free(dev, size, cpu_addr, dma_addr, attrs);
	else
		dma_direct_free_pages(dev, size, cpu_addr, dma_addr, attrs);
}

static void dma_direct_sync_single_for_device(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	if (dev_is_dma_coherent(dev))
		return;
	arch_sync_dma_for_device(dev, dma_to_phys(dev, addr), size, dir);
}

static void dma_direct_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	if (dev_is_dma_coherent(dev))
		return;

	for_each_sg(sgl, sg, nents, i)
		arch_sync_dma_for_device(dev, sg_phys(sg), sg->length, dir);
}

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
static void dma_direct_sync_single_for_cpu(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	if (dev_is_dma_coherent(dev))
		return;
	arch_sync_dma_for_cpu(dev, dma_to_phys(dev, addr), size, dir);
	arch_sync_dma_for_cpu_all(dev);
}

static void dma_direct_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	if (dev_is_dma_coherent(dev))
		return;

	for_each_sg(sgl, sg, nents, i)
		arch_sync_dma_for_cpu(dev, sg_phys(sg), sg->length, dir);
	arch_sync_dma_for_cpu_all(dev);
}

static void dma_direct_unmap_page(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_direct_sync_single_for_cpu(dev, addr, size, dir);
}

static void dma_direct_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_direct_sync_sg_for_cpu(dev, sgl, nents, dir);
}
#endif

dma_addr_t dma_direct_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size, enum dma_data_direction dir,
		unsigned long attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;
	dma_addr_t dma_addr = phys_to_dma(dev, phys);

	if (!check_addr(dev, dma_addr, size, __func__))
		return DIRECT_MAPPING_ERROR;

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_direct_sync_single_for_device(dev, dma_addr, size, dir);
	return dma_addr;
}

int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, unsigned long attrs)
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

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_direct_sync_sg_for_device(dev, sgl, nents, dir);
	return nents;
}

/*
 * Because 32-bit DMA masks are so common we expect every architecture to be
 * able to satisfy them - either by not supporting more physical memory, or by
 * providing a ZONE_DMA32.  If neither is the case, the architecture needs to
 * use an IOMMU instead of the direct mapping.
 */
int dma_direct_supported(struct device *dev, u64 mask)
{
	u64 min_mask;

	if (IS_ENABLED(CONFIG_ZONE_DMA))
		min_mask = DMA_BIT_MASK(ARCH_ZONE_DMA_BITS);
	else
		min_mask = DMA_BIT_MASK(32);

	min_mask = min_t(u64, min_mask, (max_pfn - 1) << PAGE_SHIFT);

	/*
	 * This check needs to be against the actual bit mask value, so
	 * use __phys_to_dma() here so that the SME encryption mask isn't
	 * part of the check.
	 */
	return mask >= __phys_to_dma(dev, min_mask);
}

int dma_direct_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr == DIRECT_MAPPING_ERROR;
}

const struct dma_map_ops dma_direct_ops = {
	.alloc			= dma_direct_alloc,
	.free			= dma_direct_free,
	.map_page		= dma_direct_map_page,
	.map_sg			= dma_direct_map_sg,
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE)
	.sync_single_for_device	= dma_direct_sync_single_for_device,
	.sync_sg_for_device	= dma_direct_sync_sg_for_device,
#endif
#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL)
	.sync_single_for_cpu	= dma_direct_sync_single_for_cpu,
	.sync_sg_for_cpu	= dma_direct_sync_sg_for_cpu,
	.unmap_page		= dma_direct_unmap_page,
	.unmap_sg		= dma_direct_unmap_sg,
#endif
	.get_required_mask	= dma_direct_get_required_mask,
	.dma_supported		= dma_direct_supported,
	.mapping_error		= dma_direct_mapping_error,
	.cache_sync		= arch_dma_cache_sync,
};
EXPORT_SYMBOL(dma_direct_ops);
