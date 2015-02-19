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

#include <video/omapdss.h>
#include <video/display_timing.h>

struct omap_dss_device;

/**
 * encoder_tfp410 platform data
 * @name: name for this display entity
 * @power_down_gpio: gpio number for PD pin (or -1 if not available)
 * @data_lines: number of DPI datalines
 */
struct encoder_tfp410_platform_data {
	const char *name;
	const char *source;
	int power_down_gpio;
	int data_lines;
};

/**
 * encoder_tpd12s015 platform data
 * @name: name for this display entity
 * @ct_cp_hpd_gpio: CT_CP_HPD gpio number
 * @ls_oe_gpio: LS_OE gpio number
 * @hpd_gpio: HPD gpio number
 */
struct encoder_tpd12s015_platform_data {
	const char *name;
	const char *source;

	int ct_cp_hpd_gpio;
	int ls_oe_gpio;
	int hpd_gpio;
};

/**
 * connector_dvi platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @i2c_bus_num: i2c bus number to be used for reading EDID
 */
struct connector_dvi_platform_data {
	const char *name;
	const char *source;
	int i2c_bus_num;
};

/**
 * connector_hdmi platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 */
struct connector_hdmi_platform_data {
	const char *name;
	const char *source;
};

/**
 * connector_atv platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @connector_type: composite/svideo
 * @invert_polarity: invert signal polarity
 */
struct connector_atv_platform_data {
	const char *name;
	const char *source;

	enum omap_dss_venc_type connector_type;
	bool invert_polarity;
};

/**
 * panel_dpi platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @data_lines: number of DPI datalines
 * @display_timing: timings for this panel
 * @backlight_gpio: gpio to enable/disable the backlight (or -1)
 * @enable_gpio: gpio to enable/disable the panel (or -1)
 */
struct panel_dpi_platform_data {
	const char *name;
	const char *source;

	int data_lines;

	const struct display_timing *display_timing;

	int backlight_gpio;
	int enable_gpio;
};

/**
 * panel_dsicm platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @reset_gpio: gpio to reset the panel (or -1)
 * @use_ext_te: use external TE GPIO
 * @ext_te_gpio: external TE GPIO
 * @ulps_timeout: time to wait before entering ULPS, 0 = disabled (ms)
 * @use_dsi_backlight: true if panel uses DSI command to control backlight
 * @pin_config: DSI pin configuration
 */
struct panel_dsicm_platform_data {
	const char *name;
	const char *source;

	int reset_gpio;

	bool use_ext_te;
	int ext_te_gpio;

	unsigned ulps_timeout;

	bool use_dsi_backlight;

	struct omap_dsi_pin_config pin_config;
};

/**
 * panel_acx565akm platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @reset_gpio: gpio to reset the panel (or -1)
 * @datapairs: number of SDI datapairs
 */
struct panel_acx565akm_platform_data {
	const char *name;
	const char *source;

	int reset_gpio;

	int datapairs;
};

/**
 * panel_lb035q02 platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @data_lines: number of DPI datalines
 * @backlight_gpio: gpio to enable/disable the backlight (or -1)
 * @enable_gpio: gpio to enable/disable the panel (or -1)
 */
struct panel_lb035q02_platform_data {
	const char *name;
	const char *source;

	int data_lines;

	int backlight_gpio;
	int enable_gpio;
};

/**
 * panel_sharp_ls037v7dw01 platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @data_lines: number of DPI datalines
 * @resb_gpio: reset signal GPIO
 * @ini_gpio: power on control GPIO
 * @mo_gpio: selection for resolution(VGA/QVGA) GPIO
 * @lr_gpio: selection for horizontal scanning direction GPIO
 * @ud_gpio: selection for vertical scanning direction GPIO
 */
struct panel_sharp_ls037v7dw01_platform_data {
	const char *name;
	const char *source;

	int data_lines;

	int resb_gpio;
	int ini_gpio;
	int mo_gpio;
	int lr_gpio;
	int ud_gpio;
};

/**
 * panel-tpo-td043mtea1 platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @data_lines: number of DPI datalines
 * @nreset_gpio: reset signal
 */
struct panel_tpo_td043mtea1_platform_data {
	const char *name;
	const char *source;

	int data_lines;

	int nreset_gpio;
};

/**
 * panel-nec-nl8048hl11 platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @data_lines: number of DPI datalines
 * @res_gpio: reset signal
 * @qvga_gpio: selection for resolution(QVGA/WVGA)
 */
struct panel_nec_nl8048hl11_platform_data {
	const char *name;
	const char *source;

	int data_lines;

	int res_gpio;
	int qvga_gpio;
};

/**
 * panel-tpo-td028ttec1 platform data
 * @name: name for display entity
 * @source: name of the display entity used as a video source
 * @data_lines: number of DPI datalines
 */
struct panel_tpo_td028ttec1_platform_data {
	const char *name;
	const char *source;

	int data_lines;
};

#endif /* __OMAP_PANEL_DATA_H */
