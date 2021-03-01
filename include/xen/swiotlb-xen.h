/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SWIOTLB_XEN_H
#define __LINUX_SWIOTLB_XEN_H

#include <linux/swiotlb.h>

void xen_dma_sync_for_cpu(struct device *dev, dma_addr_t handle,
			  size_t size, enum dma_data_direction dir);
void xen_dma_sync_for_device(struct device *dev, dma_addr_t handle,
			     size_t size, enum dma_data_direction dir);

int xen_swiotlb_init(void);
void __init xen_swiotlb_init_early(void);
extern const struct dma_map_ops xen_swiotlb_dma_ops;

#endif /* __LINUX_SWIOTLB_XEN_H */
