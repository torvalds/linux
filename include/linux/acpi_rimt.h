/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2024-2025, Ventana Micro Systems Inc.
 *	Author: Sunil V L <sunilvl@ventanamicro.com>
 */

#ifndef _ACPI_RIMT_H
#define _ACPI_RIMT_H

#ifdef CONFIG_ACPI_RIMT
int rimt_iommu_register(struct device *dev);
#else
static inline int rimt_iommu_register(struct device *dev)
{
	return -ENODEV;
}
#endif

#if defined(CONFIG_IOMMU_API) && defined(CONFIG_ACPI_RIMT)
int rimt_iommu_configure_id(struct device *dev, const u32 *id_in);
#else
static inline int rimt_iommu_configure_id(struct device *dev, const u32 *id_in)
{
	return -ENODEV;
}
#endif

#endif /* _ACPI_RIMT_H */
