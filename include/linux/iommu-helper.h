extern int iommu_is_span_boundary(unsigned int index, unsigned int nr,
				  unsigned long shift,
				  unsigned long boundary_size);
extern unsigned long iommu_area_alloc(unsigned long *map, unsigned long size,
				      unsigned long start, unsigned int nr,
				      unsigned long shift,
				      unsigned long boundary_size,
				      unsigned long align_mask);
extern void iommu_area_free(unsigned long *map, unsigned long start,
			    unsigned int nr);
