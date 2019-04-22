/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _XEN_ARM_PAGE_COHERENT_H
#define _XEN_ARM_PAGE_COHERENT_H

void __xen_dma_map_page(struct device *hwdev, struct page *page,
	     dma_addr_t dev_addr, unsigned long offset, size_t size,
	     enum dma_data_direction dir, unsigned long attrs);
void __xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		unsigned long attrs);
void __xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir);
void __xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir);

#endif /* _XEN_ARM_PAGE_COHERENT_H */
