/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __OF_IOMMU_H
#define __OF_IOMMU_H

#include <linux/device.h>
#include <linux/iommu.h>
#include <linux/of.h>

#ifdef CONFIG_OF_IOMMU

extern int of_get_dma_window(struct device_node *dn, const char *prefix,
			     int index, unsigned long *busno, dma_addr_t *addr,
			     size_t *size);

extern const struct iommu_ops *of_iommu_configure(struct device *dev,
					struct device_node *master_np,
					const u32 *id);

#else

static inline int of_get_dma_window(struct device_node *dn, const char *prefix,
			    int index, unsigned long *busno, dma_addr_t *addr,
			    size_t *size)
{
	return -EINVAL;
}

static inline const struct iommu_ops *of_iommu_configure(struct device *dev,
					 struct device_node *master_np,
					 const u32 *id)
{
	return NULL;
}

#endif	/* CONFIG_OF_IOMMU */

#endif /* __OF_IOMMU_H */
