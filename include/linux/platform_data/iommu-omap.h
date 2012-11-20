/*
 * omap iommu: main structures
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>

#define MMU_REG_SIZE		256

/**
 * struct iommu_arch_data - omap iommu private data
 * @name: name of the iommu device
 * @iommu_dev: handle of the iommu device
 *
 * This is an omap iommu private data object, which binds an iommu user
 * to its iommu device. This object should be placed at the iommu user's
 * dev_archdata so generic IOMMU API can be used without having to
 * utilize omap-specific plumbing anymore.
 */
struct omap_iommu_arch_data {
	const char *name;
	struct omap_iommu *iommu_dev;
};

/**
 * struct omap_mmu_dev_attr - OMAP mmu device attributes for omap_hwmod
 * @da_start:		device address where the va space starts.
 * @da_end:		device address where the va space ends.
 * @nr_tlb_entries:	number of entries supported by the translation
 *			look-aside buffer (TLB).
 */
struct omap_mmu_dev_attr {
	u32 da_start;
	u32 da_end;
	int nr_tlb_entries;
};

struct iommu_platform_data {
	const char *name;
	const char *reset_name;
	int nr_tlb_entries;
	u32 da_start;
	u32 da_end;

	int (*assert_reset)(struct platform_device *pdev, const char *name);
	int (*deassert_reset)(struct platform_device *pdev, const char *name);
};
