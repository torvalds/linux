// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2020 Christoph Hellwig.
 *
 * DMA operations that map physical memory directly without using an IOMMU.
 */
#include <linux/memblock.h> /* for max_pfn */
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/dma-map-ops.h>
#include <linux/scatterlist.h>
#include <linux/pfn.h>
#include <linux/vmalloc.h>
#include <linux/set_memory.h>
#include <linux/slab.h>
#include "direct.h"

/*
 * Most architectures use ZONE_DMA for the first 16 Megabytes, but some use it
 * it for entirely different regions. In that case the arch code needs to
 * override the variable below for dma-direct to work properly.
 */
unsigned int zone_dma_bits __ro_after_init = 24;

static inline dma_addr_t phys_to_dma_direct(struct device *dev,
		phys_addr_t phys)
{
	if (force_dma_unencrypted(dev))
		return phys_to_dma_unencrypted(dev, phys);
	return phys_to_dma(dev, phys);
}

static inline struct page *dma_direct_to_page(struct device *dev,
		dma_addr_t dma_addr)
{
	return pfn_to_page(PHYS_PFN(dma_to_phys(dev, dma_addr)));
}

u64 dma_direct_get_required_mask(struct device *dev)
{
	phys_addr_t phys = (phys_addr_t)(max_pfn - 1) << PAGE_SHIFT;
	u64 max_dma = phys_to_dma_direct(dev, phys);

	return (1ULL << (fls64(max_dma) - 1)) * 2 - 1;
}

static gfp_t dma_direct_optimal_gfp_mask(struct device *dev, u64 dma_mask,
				  u64 *phys_limit)
{
	u64 dma_limit = min_not_zero(dma_mask, dev->bus_dma_limit);

	/*
	 * Optimistically try the zone that the physical address mask falls
	 * into first.  If that returns memory that isn't actually addressable
	 * we will fallback to the next lower zone and try again.
	 *
	 * Note that GFP_DMA32 and GFP_DMA are no ops without the corresponding
	 * zones.
	 */
	*phys_limit = dma_to_phys(dev, dma_limit);
	if (*phys_limit <= DMA_BIT_MASK(zone_dma_bits))
		return GFP_DMA;
	if (*phys_limit <= DMA_BIT_MASK(32))
		return GFP_DMA32;
	return 0;
}

static bool dma_coherent_ok(struct device *dev, phys_addr_t phys, size_t size)
{
	dma_addr_t dma_addr = phys_to_dma_direct(dev, phys);

	if (dma_addr == DMA_MAPPING_ERROR)
		return false;
	return dma_addr + size - 1 <=
		min_not_zero(dev->coherent_dma_mask, dev->bus_dma_limit);
}

/*
 * Decrypting memory is allowed to block, so if this device requires
 * unencrypted memory it must come from atomic pools.
 */
static inline bool dma_should_alloc_from_pool(struct device *dev, gfp_t gfp,
					      unsigned long attrs)
{
	if (!IS_ENABLED(CONFIG_DMA_COHERENT_POOL))
		return false;
	if (gfpflags_allow_blocking(gfp))
		return false;
	if (force_dma_unencrypted(dev))
		return true;
	if (!IS_ENABLED(CONFIG_DMA_DIRECT_REMAP))
		return false;
	if (dma_alloc_need_uncached(dev, attrs))
		return true;
	return false;
}

static inline bool dma_should_free_from_pool(struct device *dev,
					     unsigned long attrs)
{
	if (IS_ENABLED(CONFIG_DMA_COHERENT_POOL))
		return true;
	if ((attrs & DMA_ATTR_NO_KERNEL_MAPPING) &&
	    !force_dma_unencrypted(dev))
		return false;
	if (IS_ENABLED(CONFIG_DMA_DIRECT_REMAP))
		return true;
	return false;
}

static struct page *__dma_direct_alloc_pages(struct device *dev, size_t size,
		gfp_t gfp)
{
	int node = dev_to_node(dev);
	struct page *page = NULL;
	u64 phys_limit;

	WARN_ON_ONCE(!PAGE_ALIGNED(size));

	gfp |= dma_direct_optimal_gfp_mask(dev, dev->coherent_dma_mask,
					   &phys_limit);
	page = dma_alloc_contiguous(dev, size, gfp);
	if (page && !dma_coherent_ok(dev, page_to_phys(page), size)) {
		dma_free_contiguous(dev, page, size);
		page = NULL;
	}
again:
	if (!page)
		page = alloc_pages_node(node, gfp, get_order(size));
	if (page && !dma_coherent_ok(dev, page_to_phys(page), size)) {
		dma_free_contiguous(dev, page, size);
		page = NULL;

		if (IS_ENABLED(CONFIG_ZONE_DMA32) &&
		    phys_limit < DMA_BIT_MASK(64) &&
		    !(gfp & (GFP_DMA32 | GFP_DMA))) {
			gfp |= GFP_DMA32;
			goto again;
		}

		if (IS_ENABLED(CONFIG_ZONE_DMA) && !(gfp & GFP_DMA)) {
			gfp = (gfp & ~GFP_DMA32) | GFP_DMA;
			goto again;
		}
	}

	return page;
}

static void *dma_direct_alloc_from_pool(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp)
{
	struct page *page;
	u64 phys_mask;
	void *ret;

	gfp |= dma_direct_optimal_gfp_mask(dev, dev->coherent_dma_mask,
					   &phys_mask);
	page = dma_alloc_from_pool(dev, size, &ret, gfp, dma_coherent_ok);
	if (!page)
		return NULL;
	*dma_handle = phys_to_dma_direct(dev, page_to_phys(page));
	return ret;
}

void *dma_direct_alloc(struct device *dev, size_t size,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	void *ret;
	int err;

	if (!IS_ENABLED(CONFIG_ARCH_HAS_DMA_SET_UNCACHED) &&
	    !IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) &&
	    dma_alloc_need_uncached(dev, attrs))
		return arch_dma_alloc(dev, size, dma_handle, gfp, attrs);

	size = PAGE_ALIGN(size);
	if (attrs & DMA_ATTR_NO_WARN)
		gfp |= __GFP_NOWARN;

	if (dma_should_alloc_from_pool(dev, gfp, attrs))
		return dma_direct_alloc_from_pool(dev, size, dma_handle, gfp);

	/* we always manually zero the memory once we are done */
	page = __dma_direct_alloc_pages(dev, size, gfp & ~__GFP_ZERO);
	if (!page)
		return NULL;

	if ((attrs & DMA_ATTR_NO_KERNEL_MAPPING) &&
	    !force_dma_unencrypted(dev)) {
		/* remove any dirty cache lines on the kernel alias */
		if (!PageHighMem(page))
			arch_dma_prep_coherent(page, size);
		/* return the page pointer as the opaque cookie */
		ret = page;
		goto done;
	}

	if ((IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) &&
	     dma_alloc_need_uncached(dev, attrs)) ||
	    (IS_ENABLED(CONFIG_DMA_REMAP) && PageHighMem(page))) {
		/* remove any dirty cache lines on the kernel alias */
		arch_dma_prep_coherent(page, size);

		/* create a coherent mapping */
		ret = dma_common_contiguous_remap(page, size,
				dma_pgprot(dev, PAGE_KERNEL, attrs),
				__builtin_return_address(0));
		if (!ret)
			goto out_free_pages;
		if (force_dma_unencrypted(dev)) {
			err = set_memory_decrypted((unsigned long)ret,
						   1 << get_order(size));
			if (err)
				goto out_free_pages;
		}
		memset(ret, 0, size);
		goto done;
	}

	if (PageHighMem(page)) {
		/*
		 * Depending on the cma= arguments and per-arch setup
		 * dma_alloc_contiguous could return highmem pages.
		 * Without remapping there is no way to return them here,
		 * so log an error and fail.
		 */
		dev_info(dev, "Rejecting highmem page from CMA.\n");
		goto out_free_pages;
	}

	ret = page_address(page);
	if (force_dma_unencrypted(dev)) {
		err = set_memory_decrypted((unsigned long)ret,
					   1 << get_order(size));
		if (err)
			goto out_free_pages;
	}

	memset(ret, 0, size);

	if (IS_ENABLED(CONFIG_ARCH_HAS_DMA_SET_UNCACHED) &&
	    dma_alloc_need_uncached(dev, attrs)) {
		arch_dma_prep_coherent(page, size);
		ret = arch_dma_set_uncached(ret, size);
		if (IS_ERR(ret))
			goto out_encrypt_pages;
	}
done:
	*dma_handle = phys_to_dma_direct(dev, page_to_phys(page));
	return ret;

out_encrypt_pages:
	if (force_dma_unencrypted(dev)) {
		err = set_memory_encrypted((unsigned long)page_address(page),
					   1 << get_order(size));
		/* If memory cannot be re-encrypted, it must be leaked */
		if (err)
			return NULL;
	}
out_free_pages:
	dma_free_contiguous(dev, page, size);
	return NULL;
}

void dma_direct_free(struct device *dev, size_t size,
		void *cpu_addr, dma_addr_t dma_addr, unsigned long attrs)
{
	unsigned int page_order = get_order(size);

	if (!IS_ENABLED(CONFIG_ARCH_HAS_DMA_SET_UNCACHED) &&
	    !IS_ENABLED(CONFIG_DMA_DIRECT_REMAP) &&
	    dma_alloc_need_uncached(dev, attrs)) {
		arch_dma_free(dev, size, cpu_addr, dma_addr, attrs);
		return;
	}

	/* If cpu_addr is not from an atomic pool, dma_free_from_pool() fails */
	if (dma_should_free_from_pool(dev, attrs) &&
	    dma_free_from_pool(dev, cpu_addr, PAGE_ALIGN(size)))
		return;

	if ((attrs & DMA_ATTR_NO_KERNEL_MAPPING) &&
	    !force_dma_unencrypted(dev)) {
		/* cpu_addr is a struct page cookie, not a kernel address */
		dma_free_contiguous(dev, cpu_addr, size);
		return;
	}

	if (force_dma_unencrypted(dev))
		set_memory_encrypted((unsigned long)cpu_addr, 1 << page_order);

	if (IS_ENABLED(CONFIG_DMA_REMAP) && is_vmalloc_addr(cpu_addr))
		vunmap(cpu_addr);
	else if (IS_ENABLED(CONFIG_ARCH_HAS_DMA_CLEAR_UNCACHED))
		arch_dma_clear_uncached(cpu_addr, size);

	dma_free_contiguous(dev, dma_direct_to_page(dev, dma_addr), size);
}

struct page *dma_direct_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp)
{
	struct page *page;
	void *ret;

	if (dma_should_alloc_from_pool(dev, gfp, 0))
		return dma_direct_alloc_from_pool(dev, size, dma_handle, gfp);

	page = __dma_direct_alloc_pages(dev, size, gfp);
	if (!page)
		return NULL;
	if (PageHighMem(page)) {
		/*
		 * Depending on the cma= arguments and per-arch setup
		 * dma_alloc_contiguous could return highmem pages.
		 * Without remapping there is no way to return them here,
		 * so log an error and fail.
		 */
		dev_info(dev, "Rejecting highmem page from CMA.\n");
		goto out_free_pages;
	}

	ret = page_address(page);
	if (force_dma_unencrypted(dev)) {
		if (set_memory_decrypted((unsigned long)ret,
				1 << get_order(size)))
			goto out_free_pages;
	}
	memset(ret, 0, size);
	*dma_handle = phys_to_dma_direct(dev, page_to_phys(page));
	return page;
out_free_pages:
	dma_free_contiguous(dev, page, size);
	return NULL;
}

void dma_direct_free_pages(struct device *dev, size_t size,
		struct page *page, dma_addr_t dma_addr,
		enum dma_data_direction dir)
{
	unsigned int page_order = get_order(size);
	void *vaddr = page_address(page);

	/* If cpu_addr is not from an atomic pool, dma_free_from_pool() fails */
	if (dma_should_free_from_pool(dev, 0) &&
	    dma_free_from_pool(dev, vaddr, size))
		return;

	if (force_dma_unencrypted(dev))
		set_memory_encrypted((unsigned long)vaddr, 1 << page_order);

	dma_free_contiguous(dev, page, size);
}

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
    defined(CONFIG_SWIOTLB)
void dma_direct_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t paddr = dma_to_phys(dev, sg_dma_address(sg));

		if (unlikely(is_swiotlb_buffer(paddr)))
			swiotlb_tbl_sync_single(dev, paddr, sg->length,
					dir, SYNC_FOR_DEVICE);

		if (!dev_is_dma_coherent(dev))
			arch_sync_dma_for_device(paddr, sg->length,
					dir);
	}
}
#endif

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) || \
    defined(CONFIG_SWIOTLB)
void dma_direct_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		phys_addr_t paddr = dma_to_phys(dev, sg_dma_address(sg));

		if (!dev_is_dma_coherent(dev))
			arch_sync_dma_for_cpu(paddr, sg->length, dir);

		if (unlikely(is_swiotlb_buffer(paddr)))
			swiotlb_tbl_sync_single(dev, paddr, sg->length, dir,
					SYNC_FOR_CPU);

		if (dir == DMA_FROM_DEVICE)
			arch_dma_mark_clean(paddr, sg->length);
	}

	if (!dev_is_dma_coherent(dev))
		arch_sync_dma_for_cpu_all();
}

void dma_direct_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i)
		dma_direct_unmap_page(dev, sg->dma_address, sg_dma_len(sg), dir,
			     attrs);
}
#endif

int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, unsigned long attrs)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = dma_direct_map_page(dev, sg_page(sg),
				sg->offset, sg->length, dir, attrs);
		if (sg->dma_address == DMA_MAPPING_ERROR)
			goto out_unmap;
		sg_dma_len(sg) = sg->length;
	}

	return nents;

out_unmap:
	dma_direct_unmap_sg(dev, sgl, i, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
	return 0;
}

dma_addr_t dma_direct_map_resource(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	dma_addr_t dma_addr = paddr;

	if (unlikely(!dma_capable(dev, dma_addr, size, false))) {
		dev_err_once(dev,
			     "DMA addr %pad+%zu overflow (mask %llx, bus limit %llx).\n",
			     &dma_addr, size, *dev->dma_mask, dev->bus_dma_limit);
		WARN_ON_ONCE(1);
		return DMA_MAPPING_ERROR;
	}

	return dma_addr;
}

int dma_direct_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	struct page *page = dma_direct_to_page(dev, dma_addr);
	int ret;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (!ret)
		sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return ret;
}

bool dma_direct_can_mmap(struct device *dev)
{
	return dev_is_dma_coherent(dev) ||
		IS_ENABLED(CONFIG_DMA_NONCOHERENT_MMAP);
}

int dma_direct_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	unsigned long user_count = vma_pages(vma);
	unsigned long count = PAGE_ALIGN(size) >> PAGE_SHIFT;
	unsigned long pfn = PHYS_PFN(dma_to_phys(dev, dma_addr));
	int ret = -ENXIO;

	vma->vm_page_prot = dma_pgprot(dev, vma->vm_page_prot, attrs);

	if (dma_mmap_from_dev_coherent(dev, vma, cpu_addr, size, &ret))
		return ret;

	if (vma->vm_pgoff >= count || user_count > count - vma->vm_pgoff)
		return -ENXIO;
	return remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
			user_count << PAGE_SHIFT, vma->vm_page_prot);
}

int dma_direct_supported(struct device *dev, u64 mask)
{
	u64 min_mask = (max_pfn - 1) << PAGE_SHIFT;

	/*
	 * Because 32-bit DMA masks are so common we expect every architecture
	 * to be able to satisfy them - either by not supporting more physical
	 * memory, or by providing a ZONE_DMA32.  If neither is the case, the
	 * architecture needs to use an IOMMU instead of the direct mapping.
	 */
	if (mask >= DMA_BIT_MASK(32))
		return 1;

	/*
	 * This check needs to be against the actual bit mask value, so use
	 * phys_to_dma_unencrypted() here so that the SME encryption mask isn't
	 * part of the check.
	 */
	if (IS_ENABLED(CONFIG_ZONE_DMA))
		min_mask = min_t(u64, min_mask, DMA_BIT_MASK(zone_dma_bits));
	return mask >= phys_to_dma_unencrypted(dev, min_mask);
}

size_t dma_direct_max_mapping_size(struct device *dev)
{
	/* If SWIOTLB is active, use its maximum mapping size */
	if (is_swiotlb_active() &&
	    (dma_addressing_limited(dev) || swiotlb_force == SWIOTLB_FORCE))
		return swiotlb_max_mapping_size(dev);
	return SIZE_MAX;
}

bool dma_direct_need_sync(struct device *dev, dma_addr_t dma_addr)
{
	return !dev_is_dma_coherent(dev) ||
		is_swiotlb_buffer(dma_to_phys(dev, dma_addr));
}

/**
 * dma_direct_set_offset - Assign scalar offset for a single DMA range.
 * @dev:	device pointer; needed to "own" the alloced memory.
 * @cpu_start:  beginning of memory region covered by this offset.
 * @dma_start:  beginning of DMA/PCI region covered by this offset.
 * @size:	size of the region.
 *
 * This is for the simple case of a uniform offset which cannot
 * be discovered by "dma-ranges".
 *
 * It returns -ENOMEM if out of memory, -EINVAL if a map
 * already exists, 0 otherwise.
 *
 * Note: any call to this from a driver is a bug.  The mapping needs
 * to be described by the device tree or other firmware interfaces.
 */
int dma_direct_set_offset(struct device *dev, phys_addr_t cpu_start,
			 dma_addr_t dma_start, u64 size)
{
	struct bus_dma_region *map;
	u64 offset = (u64)cpu_start - (u64)dma_start;

	if (dev->dma_range_map) {
		dev_err(dev, "attempt to add DMA range to existing map\n");
		return -EINVAL;
	}

	if (!offset)
		return 0;

	map = kcalloc(2, sizeof(*map), GFP_KERNEL);
	if (!map)
		return -ENOMEM;
	map[0].cpu_start = cpu_start;
	map[0].dma_start = dma_start;
	map[0].offset = offset;
	map[0].size = size;
	dev->dma_range_map = map;
	return 0;
}
EXPORT_SYMBOL_GPL(dma_direct_set_offset);
