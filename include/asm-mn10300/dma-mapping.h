/* DMA mapping routines for the MN10300 arch
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_DMA_MAPPING_H
#define _ASM_DMA_MAPPING_H

#include <linux/mm.h>
#include <linux/scatterlist.h>

#include <asm/cache.h>
#include <asm/io.h>

extern void *dma_alloc_coherent(struct device *dev, size_t size,
				dma_addr_t *dma_handle, int flag);

extern void dma_free_coherent(struct device *dev, size_t size,
			      void *vaddr, dma_addr_t dma_handle);

#define dma_alloc_noncoherent(d, s, h, f) dma_alloc_coherent((d), (s), (h), (f))
#define dma_free_noncoherent(d, s, v, h)  dma_free_coherent((d), (s), (v), (h))

/*
 * Map a single buffer of the indicated size for DMA in streaming mode.  The
 * 32-bit bus address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory until
 * either pci_unmap_single or pci_dma_sync_single is performed.
 */
static inline
dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size,
			  enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	mn10300_dcache_flush_inv();
	return virt_to_bus(ptr);
}

/*
 * Unmap a single streaming mode DMA translation.  The dma_addr and size must
 * match what was provided for in a previous pci_map_single call.  All other
 * usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guarenteed to see
 * whatever the device wrote there.
 */
static inline
void dma_unmap_single(struct device *dev, dma_addr_t dma_addr, size_t size,
		      enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

/*
 * Map a set of buffers described by scatterlist in streaming mode for DMA.
 * This is the scather-gather version of the above pci_map_single interface.
 * Here the scatter gather list elements are each tagged with the appropriate
 * dma address and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of DMA
 *       address/length pairs than there are SG table elements.  (for example
 *       via virtual mapping capabilities) The routine returns the number of
 *       addr/length pairs actually used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are the same
 * here.
 */
static inline
int dma_map_sg(struct device *dev, struct scatterlist *sglist, int nents,
	       enum dma_data_direction direction)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(!valid_dma_direction(direction));
	WARN_ON(nents == 0 || sglist[0].length == 0);

	for_each_sg(sglist, sg, nents, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);
	}

	mn10300_dcache_flush_inv();
	return nents;
}

/*
 * Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
static inline
void dma_unmap_sg(struct device *dev, struct scatterlist *sg, int nhwentries,
		  enum dma_data_direction direction)
{
	BUG_ON(!valid_dma_direction(direction));
}

/*
 * pci_{map,unmap}_single_page maps a kernel page to a dma_addr_t. identical
 * to pci_map_single, but takes a struct page instead of a virtual address
 */
static inline
dma_addr_t dma_map_page(struct device *dev, struct page *page,
			unsigned long offset, size_t size,
			enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
	return page_to_bus(page) + offset;
}

static inline
void dma_unmap_page(struct device *dev, dma_addr_t dma_address, size_t size,
		    enum dma_data_direction direction)
{
	BUG_ON(direction == DMA_NONE);
}

/*
 * Make physical memory consistent for a single streaming mode DMA translation
 * after a transfer.
 *
 * If you perform a pci_map_single() but wish to interrogate the buffer using
 * the cpu, yet do not wish to teardown the PCI dma mapping, you must call this
 * function before doing so.  At the next point you give the PCI dma address
 * back to the card, the device again owns the buffer.
 */
static inline
void dma_sync_single_for_cpu(struct device *dev, dma_addr_t dma_handle,
			     size_t size, enum dma_data_direction direction)
{
}

static inline
void dma_sync_single_for_device(struct device *dev, dma_addr_t dma_handle,
				size_t size, enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

static inline
void dma_sync_single_range_for_cpu(struct device *dev, dma_addr_t dma_handle,
				   unsigned long offset, size_t size,
				   enum dma_data_direction direction)
{
}

static inline void
dma_sync_single_range_for_device(struct device *dev, dma_addr_t dma_handle,
				 unsigned long offset, size_t size,
				 enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}


/*
 * Make physical memory consistent for a set of streaming mode DMA translations
 * after a transfer.
 *
 * The same as pci_dma_sync_single but for a scatter-gather list, same rules
 * and usage.
 */
static inline
void dma_sync_sg_for_cpu(struct device *dev, struct scatterlist *sg,
			 int nelems, enum dma_data_direction direction)
{
}

static inline
void dma_sync_sg_for_device(struct device *dev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

static inline
int dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

/*
 * Return whether the given PCI device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits during
 * PCI bus mastering, then you would pass 0x00ffffff as the mask to this
 * function.
 */
static inline
int dma_supported(struct device *dev, u64 mask)
{
	/*
	 * we fall back to GFP_DMA when the mask isn't all 1s, so we can't
	 * guarantee allocations that must be within a tighter range than
	 * GFP_DMA
	 */
	if (mask < 0x00ffffff)
		return 0;
	return 1;
}

static inline
int dma_set_mask(struct device *dev, u64 mask)
{
	if (!dev->dma_mask || !dma_supported(dev, mask))
		return -EIO;

	*dev->dma_mask = mask;
	return 0;
}

static inline
int dma_get_cache_alignment(void)
{
	return 1 << L1_CACHE_SHIFT;
}

#define dma_is_consistent(d)	(1)

static inline
void dma_cache_sync(void *vaddr, size_t size,
		    enum dma_data_direction direction)
{
	mn10300_dcache_flush_inv();
}

#endif
