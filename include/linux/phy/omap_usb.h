/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * omap_usb.h -- omap usb2 phy header file
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
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

enum omap_usb_phy_type {
	TYPE_USB2,    /* USB2_PHY, power down in CONTROL_DEV_CONF */
	TYPE_DRA7USB2, /* USB2 PHY, power and power_aux e.g. DRA7 */
	TYPE_AM437USB2, /* USB2 PHY, power e.g. AM437x */
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
	enum omap_usb_phy_type	type;
	struct regmap		*syscon_phy_power; /* ctrl. reg. acces */
	unsigned int		power_reg; /* power reg. index within syscon */
	u32			mask;
	u32			power_on;
	u32			power_off;
};

struct usb_phy_data {
	const char *label;
	u8 flags;
	u32 mask;
	u32 power_on;
	u32 power_off;
};

/* Driver Flags */
#define OMAP_USB2_HAS_START_SRP (1 << 0)
#define OMAP_USB2_HAS_SET_VBUS (1 << 1)
#define OMAP_USB2_CALIBRATE_FALSE_DISCONNECT (1 << 2)

#define OMAP_DEV_PHY_PD		BIT(0)
#define OMAP_USB2_PHY_PD	BIT(28)

#define AM437X_USB2_PHY_PD		BIT(0)
#define AM437X_USB2_OTG_PD		BIT(1)
#define AM437X_USB2_OTGVDET_EN		BIT(19)
#define AM437X_USB2_OTGSESSEND_EN	BIT(20)

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
