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

enum omap_control_usb_type {
	OMAP_CTRL_TYPE_OTGHS = 1,	/* Mailbox OTGHS_CONTROL */
	OMAP_CTRL_TYPE_USB2,	/* USB2_PHY, power down in CONTROL_DEV_CONF */
	OMAP_CTRL_TYPE_PIPE3,	/* PIPE3 PHY, DPLL & seperate Rx/Tx power */
	OMAP_CTRL_TYPE_DRA7USB2, /* USB2 PHY, power and power_aux e.g. DRA7 */
};

struct omap_control_usb {
	struct device *dev;

	u32 __iomem *otghs_control;
	u32 __iomem *power;
	u32 __iomem *power_aux;

	struct clk *sys_clk;

	enum omap_control_usb_type type;
};

enum omap_control_usb_mode {
	USB_MODE_UNDEFINED = 0,
	USB_MODE_HOST,
	USB_MODE_DEVICE,
	USB_MODE_DISCONNECT,
};

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

#define OMAP_CTRL_USB2_PHY_PD		BIT(28)

#if IS_ENABLED(CONFIG_OMAP_CONTROL_USB)
extern void omap_control_usb_phy_power(struct device *dev, int on);
extern void omap_control_usb_set_mode(struct device *dev,
	enum omap_control_usb_mode mode);
#else

static inline void omap_control_usb_phy_power(struct device *dev, int on)
{
}

static inline void omap_control_usb_set_mode(struct device *dev,
	enum omap_control_usb_mode mode)
{
}
#endif

#endif	/* __OMAP_CONTROL_USB_H__ */
