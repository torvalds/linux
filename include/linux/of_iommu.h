/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __OF_IOMMU_H
#define __OF_IOMMU_H

struct device;
struct device_node;
struct iommu_ops;

#ifdef CONFIG_OF_IOMMU

extern const struct iommu_ops *of_iommu_configure(struct device *dev,
					struct device_node *master_np,
					const u32 *id);

#else

static inline const struct iommu_ops *of_iommu_configure(struct device *dev,
					 struct device_node *master_np,
					 const u32 *id)
{
	return NULL;
}

#endif	/* CONFIG_OF_IOMMU */

#endif /* __OF_IOMMU_H */
