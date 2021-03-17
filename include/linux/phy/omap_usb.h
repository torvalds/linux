/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * omap_usb.h -- omap usb2 phy header file
 *
 * Copyright (C) 2012-2020 Texas Instruments Incorporated - http://www.ti.com
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#ifndef __DRIVERS_OMAP_USB2_H
#define __DRIVERS_OMAP_USB2_H

#include <linux/usb/phy_companion.h>

#define	phy_to_omapusb(x)	container_of((x), struct omap_usb, phy)

#if defined(CONFIG_OMAP_USB2) || defined(CONFIG_OMAP_USB2_MODULE)
extern int omap_usb2_set_comparator(struct phy_companion *comparator);
#else
static inline int omap_usb2_set_comparator(struct phy_companion *comparator)
{
	return -ENODEV;
}
#endif

#endif /* __DRIVERS_OMAP_USB_H */
