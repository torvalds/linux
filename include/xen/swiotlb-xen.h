/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SWIOTLB_XEN_H
#define __LINUX_SWIOTLB_XEN_H

#include <linux/swiotlb.h>

void xen_dma_sync_for_cpu(struct device *dev, dma_addr_t handle,
		phys_addr_t paddr, size_t size, enum dma_data_direction dir);
void xen_dma_sync_for_device(struct device *dev, dma_addr_t handle,
		phys_addr_t paddr, size_t size, enum dma_data_direction dir);

extern int xen_swiotlb_init(int verbose, bool early);
extern const struct dma_map_ops xen_swiotlb_dma_ops;

#endif /* __LINUX_SWIOTLB_XEN_H */
