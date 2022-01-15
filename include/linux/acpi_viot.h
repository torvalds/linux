/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ACPI_VIOT_H__
#define __ACPI_VIOT_H__

#include <linux/acpi.h>

#ifdef CONFIG_ACPI_VIOT
void __init acpi_viot_init(void);
int viot_iommu_configure(struct device *dev);
#else
static inline void acpi_viot_init(void) {}
static inline int viot_iommu_configure(struct device *dev)
{
	return -ENODEV;
}
#endif

#endif /* __ACPI_VIOT_H__ */
