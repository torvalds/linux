/*
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __TEGRA_USB_PHY_H
#define __TEGRA_USB_PHY_H

#include <linux/clk.h>
#include <linux/usb/otg.h>

struct tegra_utmip_config {
	u8 hssync_start_delay;
	u8 elastic_limit;
	u8 idle_wait_delay;
	u8 term_range_adj;
	u8 xcvr_setup;
	u8 xcvr_lsfslew;
	u8 xcvr_lsrslew;
};

struct tegra_ulpi_config {
	int reset_gpio;
	const char *clk;
};

enum tegra_usb_phy_port_speed {
	TEGRA_USB_PHY_PORT_SPEED_FULL = 0,
	TEGRA_USB_PHY_PORT_SPEED_LOW,
	TEGRA_USB_PHY_PORT_SPEED_HIGH,
};

enum tegra_usb_phy_mode {
	TEGRA_USB_PHY_MODE_DEVICE,
	TEGRA_USB_PHY_MODE_HOST,
};

struct tegra_xtal_freq;

struct tegra_usb_phy {
	int instance;
	const struct tegra_xtal_freq *freq;
	void __iomem *regs;
	void __iomem *pad_regs;
	struct clk *clk;
	struct clk *pll_u;
	struct clk *pad_clk;
	enum tegra_usb_phy_mode mode;
	void *config;
	struct usb_phy *ulpi;
	struct usb_phy u_phy;
	struct device *dev;
};

struct tegra_usb_phy *tegra_usb_phy_open(struct device *dev, int instance,
	void __iomem *regs, void *config, enum tegra_usb_phy_mode phy_mode);

void tegra_usb_phy_clk_disable(struct tegra_usb_phy *phy);

void tegra_usb_phy_clk_enable(struct tegra_usb_phy *phy);

void tegra_usb_phy_preresume(struct tegra_usb_phy *phy);

void tegra_usb_phy_postresume(struct tegra_usb_phy *phy);

void tegra_ehci_phy_restore_start(struct tegra_usb_phy *phy,
				 enum tegra_usb_phy_port_speed port_speed);

void tegra_ehci_phy_restore_end(struct tegra_usb_phy *phy);

#endif /* __TEGRA_USB_PHY_H */
