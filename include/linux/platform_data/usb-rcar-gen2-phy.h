/*
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __USB_RCAR_GEN2_PHY_H
#define __USB_RCAR_GEN2_PHY_H

#include <linux/types.h>

struct rcar_gen2_phy_platform_data {
	/* USB channel 0 configuration */
	bool chan0_pci:1;	/* true: PCI USB host 0, false: USBHS */
	/* USB channel 2 configuration */
	bool chan2_pci:1;	/* true: PCI USB host 2, false: USBSS */
};

#endif
