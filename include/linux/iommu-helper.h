#ifndef _LINUX_IOMMU_HELPER_H
#define _LINUX_IOMMU_HELPER_H

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
extern void iommu_area_reserve(unsigned long *map, unsigned long i, int len);
extern unsigned long iommu_area_alloc(unsigned long *map, unsigned long size,
				      unsigned long start, unsigned int nr,
				      unsigned long shift,
				      unsigned long boundary_size,
				      unsigned long align_mask);
extern void iommu_area_free(unsigned long *map, unsigned long start,
			    unsigned int nr);

#endif
