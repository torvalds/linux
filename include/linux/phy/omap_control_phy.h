/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * omap_control_phy.h - Header file for the PHY part of control module.
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#ifndef __OMAP_CONTROL_PHY_H__
#define __OMAP_CONTROL_PHY_H__

enum omap_control_phy_type {
	OMAP_CTRL_TYPE_OTGHS = 1,	/* Mailbox OTGHS_CONTROL */
	OMAP_CTRL_TYPE_USB2,	/* USB2_PHY, power down in CONTROL_DEV_CONF */
	OMAP_CTRL_TYPE_PIPE3,	/* PIPE3 PHY, DPLL & seperate Rx/Tx power */
	OMAP_CTRL_TYPE_PCIE,	/* RX TX control of ACSPCIE */
	OMAP_CTRL_TYPE_DRA7USB2, /* USB2 PHY, power and power_aux e.g. DRA7 */
	OMAP_CTRL_TYPE_AM437USB2, /* USB2 PHY, power e.g. AM437x */
};

struct omap_control_phy {
	struct device *dev;

	u32 __iomem *otghs_control;
	u32 __iomem *power;
	u32 __iomem *power_aux;
	u32 __iomem *pcie_pcs;

	struct clk *sys_clk;

	enum omap_control_phy_type type;
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

#define	OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_CMD_MASK		0x003FC000
#define	OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_CMD_SHIFT	0xE

#define	OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_FREQ_MASK	0xFFC00000
#define	OMAP_CTRL_PIPE3_PHY_PWRCTL_CLK_FREQ_SHIFT	0x16

#define	OMAP_CTRL_PIPE3_PHY_TX_RX_POWERON	0x3
#define	OMAP_CTRL_PIPE3_PHY_TX_RX_POWEROFF	0x0

#define	OMAP_CTRL_PCIE_PCS_MASK			0xff
#define	OMAP_CTRL_PCIE_PCS_DELAY_COUNT_SHIFT	16

#define OMAP_CTRL_USB2_PHY_PD		BIT(28)

#define AM437X_CTRL_USB2_PHY_PD		BIT(0)
#define AM437X_CTRL_USB2_OTG_PD		BIT(1)
#define AM437X_CTRL_USB2_OTGVDET_EN	BIT(19)
#define AM437X_CTRL_USB2_OTGSESSEND_EN	BIT(20)

#if IS_ENABLED(CONFIG_OMAP_CONTROL_PHY)
void omap_control_phy_power(struct device *dev, int on);
void omap_control_usb_set_mode(struct device *dev,
			       enum omap_control_usb_mode mode);
void omap_control_pcie_pcs(struct device *dev, u8 delay);
#else

static inline void omap_control_phy_power(struct device *dev, int on)
{
}

static inline void omap_control_usb_set_mode(struct device *dev,
	enum omap_control_usb_mode mode)
{
}

static inline void omap_control_pcie_pcs(struct device *dev, u8 delay)
{
}
#endif

#endif	/* __OMAP_CONTROL_PHY_H__ */
