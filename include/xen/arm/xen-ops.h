/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_XEN_OPS_H
#define _ASM_ARM_XEN_OPS_H

#include <xen/swiotlb-xen.h>
#include <xen/xen-ops.h>

static inline void xen_setup_dma_ops(struct device *dev)
{
#ifdef CONFIG_XEN
	if (xen_swiotlb_detect())
		dev->dma_ops = &xen_swiotlb_dma_ops;
#endif
}

#endif /* _ASM_ARM_XEN_OPS_H */
