#ifndef _LINUX_IOMMU_HELPER_H
#define _LINUX_IOMMU_HELPER_H

#include <linux/kernel.h>

static inline unsigned long iommu_device_max_index(unsigned long size,
						   unsigned long offset,
						   u64 dma_mask)
{
	if (size + offset > dma_mask)
		return dma_mask - offset + 1;
	else
		return size;
}

extern int iommu_is_span_boundary(unsigned int index, unsigned int nr,
				  unsigned long shift,
				  unsigned long boundary_size);
extern unsigned long iommu_area_alloc(unsigned long *map, unsigned long size,
				      unsigned long start, unsigned int nr,
				      unsigned long shift,
				      unsigned long boundary_size,
				      unsigned long align_mask);

static inline unsigned long iommu_num_pages(unsigned long addr,
					    unsigned long len,
					    unsigned long io_page_size)
{
	unsigned long size = (addr & (io_page_size - 1)) + len;

	return DIV_ROUND_UP(size, io_page_size);
}

#endif
