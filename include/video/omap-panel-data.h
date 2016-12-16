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

#include <video/display_timing.h>

/**
 * connector_atv platform data
 * @name: name for this display entity
 * @source: name of the display entity used as a video source
 * @invert_polarity: invert signal polarity
 */
struct connector_atv_platform_data {
	const char *name;
	const char *source;

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

#endif /* __OMAP_PANEL_DATA_H */
