/*
 * omap iommu: simple virtual address space management
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _OMAP_IOMMU_H_
#define _OMAP_IOMMU_H_

#ifdef CONFIG_OMAP_IOMMU
extern void omap_iommu_save_ctx(struct device *dev);
extern void omap_iommu_restore_ctx(struct device *dev);
#else
static inline void omap_iommu_save_ctx(struct device *dev) {}
static inline void omap_iommu_restore_ctx(struct device *dev) {}
#endif

#endif
