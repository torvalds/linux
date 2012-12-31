/* inlude/linux/platform_data/exynos_usb3_drd.h
 *
 * Copyright (c) 2012 Samsung Electronics Co. Ltd
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * EXYNOS SuperSpeed USB 3.0 DRD Controller platform data
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef _EXYNOS_USB3_DRD_H_
#define _EXYNOS_USB3_DRD_H_

struct exynos_usb3_drd_pdata {
	int phy_type;
	int (*phy_init)(struct platform_device *pdev, int type);
	int (*phy_exit)(struct platform_device *pdev, int type);
};

#endif
