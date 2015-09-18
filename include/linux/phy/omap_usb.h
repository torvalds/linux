/*
 * omap_usb.h -- omap usb2 phy header file
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DRIVERS_OMAP_USB2_H
#define __DRIVERS_OMAP_USB2_H

#include <linux/io.h>
#include <linux/usb/otg.h>

struct usb_dpll_params {
	u16	m;
	u8	n;
	u8	freq:3;
	u8	sd;
	u32	mf;
};

struct omap_usb {
	struct usb_phy		phy;
	struct phy_companion	*comparator;
	void __iomem		*pll_ctrl_base;
	void __iomem		*phy_base;
	struct device		*dev;
	struct device		*control_dev;
	struct clk		*wkupclk;
	struct clk		*optclk;
	u8			flags;
};

struct usb_phy_data {
	const char *label;
	u8 flags;
};

/* Driver Flags */
#define OMAP_USB2_HAS_START_SRP (1 << 0)
#define OMAP_USB2_HAS_SET_VBUS (1 << 1)
#define OMAP_USB2_CALIBRATE_FALSE_DISCONNECT (1 << 2)

#define	phy_to_omapusb(x)	container_of((x), struct omap_usb, phy)

#if defined(CONFIG_OMAP_USB2) || defined(CONFIG_OMAP_USB2_MODULE)
extern int omap_usb2_set_comparator(struct phy_companion *comparator);
#else
static inline int omap_usb2_set_comparator(struct phy_companion *comparator)
{
	return -ENODEV;
}
#endif

static inline u32 omap_usb_readl(void __iomem *addr, unsigned offset)
{
	return __raw_readl(addr + offset);
}

static inline void omap_usb_writel(void __iomem *addr, unsigned offset,
	u32 data)
{
	__raw_writel(data, addr + offset);
}

#endif /* __DRIVERS_OMAP_USB_H */
