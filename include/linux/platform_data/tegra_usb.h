/*
 * Copyright (C) 2010 Google, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _TEGRA_USB_H_
#define _TEGRA_USB_H_

enum tegra_usb_operating_modes {
	TEGRA_USB_DEVICE,
	TEGRA_USB_HOST,
	TEGRA_USB_OTG,
};

struct tegra_ehci_platform_data {
	enum tegra_usb_operating_modes operating_mode;
	/* power down the phy on bus suspend */
	int power_down_on_bus_suspend;
	void *phy_config;
};

#endif /* _TEGRA_USB_H_ */
