/*
 * Header for DVI output driver
 *
 * Copyright (C) 2011 Texas Instruments Inc
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
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

#ifndef __OMAP_PANEL_DVI_H
#define __OMAP_PANEL_DVI_H

struct omap_dss_device;

/**
 * struct panel_dvi_platform_data - panel driver configuration data
 * @i2c_bus_num: i2c bus id for the panel
 * @power_down_gpio: gpio number for PD pin (or -1 if not available)
 */
struct panel_dvi_platform_data {
	u16 i2c_bus_num;
	int power_down_gpio;
};

#endif /* __OMAP_PANEL_DVI_H */
