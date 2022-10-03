// SPDX-License-Identifier: GPL-2.0+
/*
 * phy-companion.h -- phy companion to indicate the comparator part of PHY
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - https://www.ti.com
 *
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
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
