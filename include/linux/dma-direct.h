/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internals of the DMA direct mapping implementation.  Only for use by the
 * DMA mapping code and IOMMU drivers.
 */
#ifndef _LINUX_DMA_DIRECT_H
#define _LINUX_DMA_DIRECT_H 1

#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/memblock.h> /* for min_low_pfn */
#include <linux/mem_encrypt.h>
#include <linux/swiotlb.h>

extern unsigned int zone_dma_bits;

/*
 * Record the mapping of CPU physical to DMA addresses for a given region.
 */
struct bus_dma_region {
	phys_addr_t	cpu_start;
	dma_addr_t	dma_start;
	u64		size;
	u64		offset;
};

static inline bool zone_dma32_is_empty(int node)
{
#ifdef CONFIG_ZONE_DMA32
	pg_data_t *pgdat = NODE_DATA(node);

	return zone_is_empty(&pgdat->node_zones[ZONE_DMA32]);
#else
	return true;
#endif
}

static inline bool zone_dma32_are_empty(void)
{
#ifdef CONFIG_NUMA
	int node;

	for_each_node(node)
		if (!zone_dma32_is_empty(node))
			return false;
#else
	if (!zone_dma32_is_empty(numa_node_id()))
		return false;
#endif

	return true;
}

static inline dma_addr_t translate_phys_to_dma(struct device *dev,
		phys_addr_t paddr)
{
	const struct bus_dma_region *m;

	for (m = dev->dma_range_map; m->size; m++)
		if (paddr >= m->cpu_start && paddr - m->cpu_start < m->size)
			return (dma_addr_t)paddr - m->offset;

	/* make sure dma_capable fails when no translation is available */
	return DMA_MAPPING_ERROR;
}

static inline phys_addr_t translate_dma_to_phys(struct device *dev,
		dma_addr_t dma_addr)
{
	const struct bus_dma_region *m;

	for (m = dev->dma_range_map; m->size; m++)
		if (dma_addr >= m->dma_start && dma_addr - m->dma_start < m->size)
			return (phys_addr_t)dma_addr + m->offset;

	return (phys_addr_t)-1;
}

#ifdef CONFIG_ARCH_HAS_PHYS_TO_DMA
#include <asm/dma-direct.h>
#ifndef phys_to_dma_unencrypted
#define phys_to_dma_unencrypted		phys_to_dma
#endif
#else
static inline dma_addr_t phys_to_dma_unencrypted(struct device *dev,
		phys_addr_t paddr)
{
	if (dev->dma_range_map)
		return translate_phys_to_dma(dev, paddr);
	return paddr;
}

/*
 * If memory encryption is supported, phys_to_dma will set the memory encryption
 * bit in the DMA address, and dma_to_phys will clear it.
 * phys_to_dma_unencrypted is for use on special unencrypted memory like swiotlb
 * buffers.
 */
static inline dma_addr_t phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return __sme_set(phys_to_dma_unencrypted(dev, paddr));
}

static inline phys_addr_t dma_to_phys(struct device *dev, dma_addr_t dma_addr)
{
	phys_addr_t paddr;

	if (dev->dma_range_map)
		paddr = translate_dma_to_phys(dev, dma_addr);
	else
		paddr = dma_addr;

	return __sme_clr(paddr);
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

static inline bool dma_capable(struct device *dev, dma_addr_t addr, size_t size,
		bool is_ram)
{
	dma_addr_t end = addr + size - 1;

	if (addr == DMA_MAPPING_ERROR)
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
struct page *dma_direct_alloc_pages(struct device *dev, size_t size,
		dma_addr_t *dma_handle, enum dma_data_direction dir, gfp_t gfp);
void dma_direct_free_pages(struct device *dev, size_t size,
		struct page *page, dma_addr_t dma_addr,
		enum dma_data_direction dir);
int dma_direct_supported(struct device *dev, u64 mask);
dma_addr_t dma_direct_map_resource(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir, unsigned long attrs);

#endif /* _LINUX_DMA_DIRECT_H */
