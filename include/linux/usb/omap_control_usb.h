/*
 * omap_control_usb.h - Header file for the USB part of control module.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
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

#ifndef __OMAP_CONTROL_USB_H__
#define __OMAP_CONTROL_USB_H__

struct omap_control_usb {
	struct device *dev;

	u32 __iomem *dev_conf;
	u32 __iomem *otghs_control;
	u32 __iomem *phy_power;

	struct clk *sys_clk;

	u32 type;
};

struct omap_control_usb_platform_data {
	u8 type;
};

enum omap_control_usb_mode {
	USB_MODE_UNDEFINED = 0,
	USB_MODE_HOST,
	USB_MODE_DEVICE,
	USB_MODE_DISCONNECT,
};

/* To differentiate ctrl module IP having either mailbox or USB3 PHY power */
#define	OMAP_CTRL_DEV_TYPE1		0x1
#define	OMAP_CTRL_DEV_TYPE2		0x2

#define	OMAP_CTRL_DEV_PHY_PD		BIT(0)

#define	OMAP_CTRL_DEV_AVALID		BIT(0)
#define	OMAP_CTRL_DEV_BVALID		BIT(1)
#define	OMAP_CTRL_DEV_VBUSVALID		BIT(2)
#define	OMAP_CTRL_DEV_SESSEND		BIT(3)
#define	OMAP_CTRL_DEV_IDDIG		BIT(4)

#define	OMAP_CTRL_USB_PWRCTL_CLK_CMD_MASK	0x003FC000
#define	OMAP_CTRL_USB_PWRCTL_CLK_CMD_SHIFT	0xE

#define	OMAP_CTRL_USB_PWRCTL_CLK_FREQ_MASK	0xFFC00000
#define	OMAP_CTRL_USB_PWRCTL_CLK_FREQ_SHIFT	0x16

#define	OMAP_CTRL_USB3_PHY_TX_RX_POWERON	0x3
#define	OMAP_CTRL_USB3_PHY_TX_RX_POWEROFF	0x0

#if IS_ENABLED(CONFIG_OMAP_CONTROL_USB)
extern struct device *omap_get_control_dev(void);
extern void omap_control_usb_phy_power(struct device *dev, int on);
extern void omap_control_usb3_phy_power(struct device *dev, bool on);
extern void omap_control_usb_set_mode(struct device *dev,
	enum omap_control_usb_mode mode);
#else
static inline struct device *omap_get_control_dev(void)
{
	return ERR_PTR(-ENODEV);
}

static inline void omap_control_usb_phy_power(struct device *dev, int on)
{
}

static inline void omap_control_usb3_phy_power(struct device *dev, int on)
{
}

static inline void omap_control_usb_set_mode(struct device *dev,
	enum omap_control_usb_mode mode)
{
}
#endif

#endif	/* __OMAP_CONTROL_USB_H__ */
