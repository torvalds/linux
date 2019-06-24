/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap iommu: simple virtual address space management
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
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
