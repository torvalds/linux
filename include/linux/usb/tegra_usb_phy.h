// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Google, Inc.
 */

#ifndef __TEGRA_USB_PHY_H
#define __TEGRA_USB_PHY_H

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/usb/otg.h>

struct gpio_desc;

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
 * uhsic_registers_offset: for Tegra30+ where HSIC registers were offset
 *      comparing to Tegra20 by 0x400, since Tegra20 has no UTMIP on PHY2
 * uhsic_tx_rtune: fine tuned 50 Ohm termination resistor for NMOS/PMOS driver
 * uhsic_pts_value: parallel transceiver select enumeration value
 * portsc1_offset: register offset of PORTSC1
 */

struct tegra_phy_soc_config {
	bool utmi_pll_config_in_car_module;
	bool has_hostpc;
	bool requires_usbmode_setup;
	bool requires_extra_tuning_parameters;
	bool requires_pmc_ao_power_up;
	u32 uhsic_registers_offset;
	u32 uhsic_tx_rtune;
	u32 uhsic_pts_value;
	u32 portsc1_offset;
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
	enum usb_phy_interface phy_type;
	struct gpio_desc *reset_gpio;
	struct reset_control *pad_rst;
	bool wakeup_enabled;
	bool pad_wakeup;
	bool powered_on;
};

#endif /* __TEGRA_USB_PHY_H */
