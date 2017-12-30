/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 eGauge Systems LLC
 *	Contributed by David Mosberger-Tang <davidm@egauge.net>
 *
 * Platform-data structure for MAX3421 USB HCD driver.
 *
 */
#ifndef MAX3421_HCD_PLAT_H_INCLUDED
#define MAX3421_HCD_PLAT_H_INCLUDED

/*
 * This structure defines the mapping of certain auxiliary functions to the
 * MAX3421E GPIO pins.  The chip has eight GP inputs and eight GP outputs.
 * A value of 0 indicates that the pin is not used/wired to anything.
 *
 * At this point, the only control the max3421-hcd driver cares about is
 * to control Vbus (5V to the peripheral).
 */
struct max3421_hcd_platform_data {
	u8 vbus_gpout;			/* pin controlling Vbus */
	u8 vbus_active_level;		/* level that turns on power */
};

#endif /* MAX3421_HCD_PLAT_H_INCLUDED */
