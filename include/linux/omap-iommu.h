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

#ifndef _INTEL_IOMMU_H_
#define _INTEL_IOMMU_H_

struct iovm_struct {
	struct omap_iommu	*iommu;	/* iommu object which this belongs to */
	u32			da_start; /* area definition */
	u32			da_end;
	u32			flags; /* IOVMF_: see below */
	struct list_head	list; /* linked in ascending order */
	const struct sg_table	*sgt; /* keep 'page' <-> 'da' mapping */
	void			*va; /* mpu side mapped address */
};

#define MMU_RAM_ENDIAN_SHIFT	9
#define MMU_RAM_ENDIAN_LITTLE	(0 << MMU_RAM_ENDIAN_SHIFT)
#define MMU_RAM_ELSZ_8		(0 << MMU_RAM_ELSZ_SHIFT)
#define IOVMF_ENDIAN_LITTLE	MMU_RAM_ENDIAN_LITTLE
#define MMU_RAM_ELSZ_SHIFT	7
#define IOVMF_ELSZ_8		MMU_RAM_ELSZ_8

struct iommu_domain;

extern struct iovm_struct *omap_find_iovm_area(struct device *dev, u32 da);
extern u32
omap_iommu_vmap(struct iommu_domain *domain, struct device *dev, u32 da,
			const struct sg_table *sgt, u32 flags);
extern struct sg_table *omap_iommu_vunmap(struct iommu_domain *domain,
				struct device *dev, u32 da);
extern u32
omap_iommu_vmalloc(struct iommu_domain *domain, struct device *dev,
				u32 da, size_t bytes, u32 flags);
extern void
omap_iommu_vfree(struct iommu_domain *domain, struct device *dev,
				const u32 da);
extern void *omap_da_to_va(struct device *dev, u32 da);

extern void omap_iommu_save_ctx(struct device *dev);
extern void omap_iommu_restore_ctx(struct device *dev);

#endif
