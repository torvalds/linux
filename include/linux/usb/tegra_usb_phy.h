// SPDX-License-Identifier: GPL-2.0
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
#include <linux/gpio.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/otg.h>

/*
 * utmi_pll_config_in_car_module: true if the UTMI PLL configuration registers
 *     should be set up by clk-tegra, false if by the PHY code
 * has_hostpc: true if the USB controller has the HOSTPC extension, which
 *     changes the location of the PHCD and PTS fields
 * requires_usbmode_setup: true if the USBMODE register needs to be set to
 *      enter host mode
 * requires_extra_tuning_parameters: true if xcvr_hsslew, hssquelch_level
 *      and hsdiscon_level should be set for adequate signal quality
 * requires_pmc_ao_power_up: true if USB AO is powered down by default
 */

struct tegra_phy_soc_config {
	bool utmi_pll_config_in_car_module;
	bool has_hostpc;
	bool requires_usbmode_setup;
	bool requires_extra_tuning_parameters;
	bool requires_pmc_ao_power_up;
};

struct tegra_utmip_config {
	u8 hssync_start_delay;
	u8 elastic_limit;
	u8 idle_wait_delay;
	u8 term_range_adj;
	bool xcvr_setup_use_fuses;
	u8 xcvr_setup;
	u8 xcvr_lsfslew;
	u8 xcvr_lsrslew;
	u8 xcvr_hsslew;
	u8 hssquelch_level;
	u8 hsdiscon_level;
};

enum tegra_usb_phy_port_speed {
	TEGRA_USB_PHY_PORT_SPEED_FULL = 0,
	TEGRA_USB_PHY_PORT_SPEED_LOW,
	TEGRA_USB_PHY_PORT_SPEED_HIGH,
};

struct tegra_xtal_freq;

struct tegra_usb_phy {
	int irq;
	int instance;
	const struct tegra_xtal_freq *freq;
	void __iomem *regs;
	void __iomem *pad_regs;
	struct clk *clk;
	struct clk *pll_u;
	struct clk *pad_clk;
	struct regulator *vbus;
	struct regmap *pmc_regmap;
	enum usb_dr_mode mode;
	void *config;
	const struct tegra_phy_soc_config *soc_config;
	struct usb_phy *ulpi;
	struct usb_phy u_phy;
	bool is_legacy_phy;
	bool is_ulpi_phy;
	struct gpio_desc *reset_gpio;
	struct reset_control *pad_rst;
	bool wakeup_enabled;
	bool pad_wakeup;
	bool powered_on;
};

void tegra_usb_phy_preresume(struct usb_phy *phy);

void tegra_usb_phy_postresume(struct usb_phy *phy);

void tegra_ehci_phy_restore_start(struct usb_phy *phy,
				 enum tegra_usb_phy_port_speed port_speed);

void tegra_ehci_phy_restore_end(struct usb_phy *phy);

#endif /* __TEGRA_USB_PHY_H */
