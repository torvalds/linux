/*
 * Header for generic DPI panel driver
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

#ifndef __OMAP_PANEL_GENERIC_DPI_H
#define __OMAP_PANEL_GENERIC_DPI_H

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

#endif /* __OMAP_PANEL_GENERIC_DPI_H */
