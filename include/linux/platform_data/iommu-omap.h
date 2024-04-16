/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap iommu: main structures
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Written by Hiroshi DOYU <Hiroshi.DOYU@nokia.com>
 */

#include <linux/platform_device.h>

struct iommu_platform_data {
	const char *reset_name;
	int (*assert_reset)(struct platform_device *pdev, const char *name);
	int (*deassert_reset)(struct platform_device *pdev, const char *name);
	int (*device_enable)(struct platform_device *pdev);
	int (*device_idle)(struct platform_device *pdev);
	int (*set_pwrdm_constraint)(struct platform_device *pdev, bool request,
				    u8 *pwrst);
};
