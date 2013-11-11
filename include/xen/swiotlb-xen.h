#ifndef __LINUX_SWIOTLB_XEN_H
#define __LINUX_SWIOTLB_XEN_H

#include <linux/swiotlb.h>

extern int xen_swiotlb_init(int verbose, bool early);

extern void
*xen_swiotlb_alloc_coherent(struct device *hwdev, size_t size,
			    dma_addr_t *dma_handle, gfp_t flags,
			    struct dma_attrs *attrs);

extern void
xen_swiotlb_free_coherent(struct device *hwdev, size_t size,
			  void *vaddr, dma_addr_t dma_handle,
			  struct dma_attrs *attrs);

extern dma_addr_t xen_swiotlb_map_page(struct device *dev, struct page *page,
				       unsigned long offset, size_t size,
				       enum dma_data_direction dir,
				       struct dma_attrs *attrs);

extern void xen_swiotlb_unmap_page(struct device *hwdev, dma_addr_t dev_addr,
				   size_t size, enum dma_data_direction dir,
				   struct dma_attrs *attrs);
extern int
xen_swiotlb_map_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
			 int nelems, enum dma_data_direction dir,
			 struct dma_attrs *attrs);

extern void
xen_swiotlb_unmap_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
			   int nelems, enum dma_data_direction dir,
			   struct dma_attrs *attrs);

extern void
xen_swiotlb_sync_single_for_cpu(struct device *hwdev, dma_addr_t dev_addr,
				size_t size, enum dma_data_direction dir);

extern void
xen_swiotlb_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction dir);

extern void
xen_swiotlb_sync_single_for_device(struct device *hwdev, dma_addr_t dev_addr,
				   size_t size, enum dma_data_direction dir);

extern void
xen_swiotlb_sync_sg_for_device(struct device *hwdev, struct scatterlist *sg,
			       int nelems, enum dma_data_direction dir);

extern int
xen_swiotlb_dma_mapping_error(struct device *hwdev, dma_addr_t dma_addr);

extern int
xen_swiotlb_dma_supported(struct device *hwdev, u64 mask);

#endif /* __LINUX_SWIOTLB_XEN_H */
