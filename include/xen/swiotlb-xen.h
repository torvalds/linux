/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_SWIOTLB_XEN_H
#define __LINUX_SWIOTLB_XEN_H

#include <linux/swiotlb.h>

extern int xen_swiotlb_init(int verbose, bool early);
extern const struct dma_map_ops xen_swiotlb_dma_ops;

#endif /* __LINUX_SWIOTLB_XEN_H */
