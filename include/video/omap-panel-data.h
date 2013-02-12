/*
 * Header containing platform_data structs for omap panels
 *
 * Copyright (C) 2013 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *	   Archit Taneja <archit@ti.com>
 *
 * Copyright (C) 2011 Texas Instruments
 * Author: Mayuresh Janorkar <mayur@ti.com>
 *
 * Copyright (C) 2010 Canonical Ltd.
 * Author: Bryan Wu <bryan.wu@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __OMAP_PANEL_DATA_H
#define __OMAP_PANEL_DATA_H

struct omap_dss_device;

/**
 * struct panel_generic_dpi_data - panel driver configuration data
 * @name: panel name
 * @platform_enable: platform specific panel enable function
 * @platform_disable: platform specific panel disable function
 */
struct panel_generic_dpi_data {
	const char *name;
	int (*platform_enable)(struct omap_dss_device *dssdev);
	void (*platform_disable)(struct omap_dss_device *dssdev);
};

/**
 * struct panel_n8x0_data - N800 panel driver configuration data
 */
struct panel_n8x0_data {
	int (*platform_enable)(struct omap_dss_device *dssdev);
	void (*platform_disable)(struct omap_dss_device *dssdev);
	int panel_reset;
	int ctrl_pwrdown;

	int (*set_backlight)(struct omap_dss_device *dssdev, int level);
};

/**
 * struct nokia_dsi_panel_data - Nokia DSI panel driver configuration data
 * @name: panel name
 * @use_ext_te: use external TE
 * @ext_te_gpio: external TE GPIO
 * @esd_interval: interval of ESD checks, 0 = disabled (ms)
 * @ulps_timeout: time to wait before entering ULPS, 0 = disabled (ms)
 * @use_dsi_backlight: true if panel uses DSI command to control backlight
 * @pin_config: DSI pin configuration
 */

struct nokia_dsi_panel_data {
	const char *name;

	int reset_gpio;

	bool use_ext_te;
	int ext_te_gpio;

	unsigned esd_interval;
	unsigned ulps_timeout;

	bool use_dsi_backlight;

	struct omap_dsi_pin_config pin_config;
};

/**
 * struct picodlp_panel_data - picodlp panel driver configuration data
 * @picodlp_adapter_id:	i2c_adapter number for picodlp
 */
struct picodlp_panel_data {
	int picodlp_adapter_id;
	int emu_done_gpio;
	int pwrgood_gpio;
};

/**
 * struct tfp410_platform_data - tfp410 panel driver configuration data
 * @i2c_bus_num: i2c bus id for the panel
 * @power_down_gpio: gpio number for PD pin (or -1 if not available)
 */
struct tfp410_platform_data {
	int i2c_bus_num;
	int power_down_gpio;
};

#endif /* __OMAP_PANEL_DATA_H */
