/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internals of the DMA direct mapping implementation.  Only for use by the
 * DMA mapping code and IOMMU drivers.
 */
#ifndef _LINUX_DMA_DIRECT_H
#define _LINUX_DMA_DIRECT_H 1

#include <linux/dma-mapping.h>
#include <linux/dma-noncoherent.h>
#include <linux/memblock.h> /* for min_low_pfn */
#include <linux/mem_encrypt.h>
#include <linux/swiotlb.h>

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

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size,
		bool is_ram)
{
	dma_addr_t end = addr + size - 1;

	if (!dev->dma_mask)
		return false;

	if (is_ram && !IS_ENABLED(CONFIG_ARCH_DMA_ADDR_T_64BIT) &&
	    min(addr, end) < phys_to_dma(dev, PFN_PHYS(min_low_pfn)))
		return false;

	return end <= min_not_zero(*dev->dma_mask, dev->bus_dma_limit);
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
int dma_direct_get_sgtable(struct device *dev, struct sg_table *sgt,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
bool dma_direct_can_mmap(struct device *dev);
int dma_direct_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs);
int dma_direct_supported(struct device *dev, u64 mask);
bool dma_direct_need_sync(struct device *dev, dma_addr_t dma_addr);
int dma_direct_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, unsigned long attrs);
dma_addr_t dma_direct_map_resource(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir, unsigned long attrs);
size_t dma_direct_max_mapping_size(struct device *dev);

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE) || \
    defined(CONFIG_SWIOTLB)
void dma_direct_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir);
#else
static inline void dma_direct_sync_sg_for_device(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
}
#endif

#if defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU) || \
    defined(CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU_ALL) || \
    defined(CONFIG_SWIOTLB)
void dma_direct_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs);
void dma_direct_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir);
#else
static inline void dma_direct_unmap_sg(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir,
		unsigned long attrs)
{
}
static inline void dma_direct_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sgl, int nents, enum dma_data_direction dir)
{
}
#endif

static inline void dma_direct_sync_single_for_device(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = dma_to_phys(dev, addr);

	if (unlikely(is_swiotlb_buffer(paddr)))
		swiotlb_tbl_sync_single(dev, paddr, size, dir, SYNC_FOR_DEVICE);

	if (!dev_is_dma_coherent(dev))
		arch_sync_dma_for_device(paddr, size, dir);
}

static inline void dma_direct_sync_single_for_cpu(struct device *dev,
		dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = dma_to_phys(dev, addr);

	if (!dev_is_dma_coherent(dev)) {
		arch_sync_dma_for_cpu(paddr, size, dir);
		arch_sync_dma_for_cpu_all();
	}

	if (unlikely(is_swiotlb_buffer(paddr)))
		swiotlb_tbl_sync_single(dev, paddr, size, dir, SYNC_FOR_CPU);
}

static inline dma_addr_t dma_direct_map_page(struct device *dev,
		struct page *page, unsigned long offset, size_t size,
		enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;
	dma_addr_t dma_addr = phys_to_dma(dev, phys);

	if (unlikely(swiotlb_force == SWIOTLB_FORCE))
		return swiotlb_map(dev, phys, size, dir, attrs);

	if (unlikely(!dma_capable(dev, dma_addr, size, true))) {
		if (swiotlb_force != SWIOTLB_NO_FORCE)
			return swiotlb_map(dev, phys, size, dir, attrs);

		dev_WARN_ONCE(dev, 1,
			     "DMA addr %pad+%zu overflow (mask %llx, bus limit %llx).\n",
			     &dma_addr, size, *dev->dma_mask, dev->bus_dma_limit);
		return DMA_MAPPING_ERROR;
	}

	if (!dev_is_dma_coherent(dev) && !(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		arch_sync_dma_for_device(phys, size, dir);
	return dma_addr;
}

static inline void dma_direct_unmap_page(struct device *dev, dma_addr_t addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t phys = dma_to_phys(dev, addr);

	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_direct_sync_single_for_cpu(dev, addr, size, dir);

	if (unlikely(is_swiotlb_buffer(phys)))
		swiotlb_tbl_unmap_single(dev, phys, size, size, dir, attrs);
}
#endif /* _LINUX_DMA_DIRECT_H */
