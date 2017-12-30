// SPDX-License-Identifier: GPL-2.0+
/*
 * phy-companion.h -- phy companion to indicate the comparator part of PHY
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DRIVERS_PHY_COMPANION_H
#define __DRIVERS_PHY_COMPANION_H

#include <linux/usb/otg.h>

/* phy_companion to take care of VBUS, ID and srp capabilities */
struct phy_companion {

	/* effective for A-peripheral, ignored for B devices */
	int	(*set_vbus)(struct phy_companion *x, bool enabled);

	/* for B devices only:  start session with A-Host */
	int	(*start_srp)(struct phy_companion *x);
};

#endif /* __DRIVERS_PHY_COMPANION_H */
