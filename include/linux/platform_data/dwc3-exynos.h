/**
 * dwc3-exynos.h - Samsung EXYNOS DWC3 Specific Glue layer, header.
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Anton Tikhomirov <av.tikhomirov@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _DWC3_EXYNOS_H_
#define _DWC3_EXYNOS_H_

struct dwc3_exynos_data {
	const char *udc_name;
	const char *xhci_name;
	int phy_type;
	int (*phy_init)(struct platform_device *pdev, int type);
	int (*phy_exit)(struct platform_device *pdev, int type);
	int (*phy_tune)(struct platform_device *pdev, int type);
	int (*phy_crport_ctrl)(struct platform_device *pdev,
				u32 uaddr, u32 udata);
	int (*vbus_ctrl)(struct platform_device *pdev, int on);
	int (*get_id_state)(struct platform_device *pdev);
	bool (*get_bses_vld)(struct platform_device *pdev);
	unsigned int quirks;
#define EXYNOS_PHY20_NO_SUSPEND	(1 << 0)
/* Force udc to start; useful when host driver support is off */
#define FORCE_RUN_PERIPHERAL	(1 << 1)
/* Force udc to suspend when VBus sensing isn't available */
#define FORCE_PM_PERIPHERAL	(1 << 2)
/* Initialize ID state to 1; useful when ID sensing isn't available */
#define FORCE_INIT_PERIPHERAL	(1 << 3)
/* Do not create udc and xhci child devices */
#define DUMMY_DRD		(1 << 4)
/* Do not create udc child device */
#define SKIP_UDC		(1 << 5)
/* Do not create xhci child device */
#define SKIP_XHCI		(1 << 6)

/* Force xhci to start */
#define FORCE_RUN_XHCI	(1 << 7)

	int id_irq;
	int vbus_irq;
	unsigned long irq_flags;
};

#endif /* _DWC3_EXYNOS_H_ */
