/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ACPI_IOVT_H__
#define __ACPI_IOVT_H__

#ifdef CONFIG_ACPI_IOVT
int iovt_iommu_configure_id(struct device *dev, const u32 *id_in);
#else
static inline int iovt_iommu_configure_id(struct device *dev, const u32 *id_in)
{ return -ENODEV; }
#endif

#endif /* __ACPI_IOVT_H__ */
