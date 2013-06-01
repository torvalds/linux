/*
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __USB_RCAR_PHY_H
#define __USB_RCAR_PHY_H

#include <linux/types.h>

struct rcar_phy_platform_data {
	bool port1_func:1;	/* true: port 1 used by function, false: host */
	unsigned penc1:1;	/* Output of the PENC1 pin in function mode */
	struct {		/* Overcurrent pin control for ports 0..2 */
		bool select_3_3v:1; /* true: USB_OVCn pin, false: OVCn pin */
				/* Set to false on port 1 in function mode */
		bool active_high:1; /* true: active  high, false: active low */
				/* Set to true  on port 1 in function mode */
	} ovc_pin[3];
};

#endif /* __USB_RCAR_PHY_H */
