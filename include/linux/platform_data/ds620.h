/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DS620_H
#define _LINUX_DS620_H

#include <linux/types.h>
#include <linux/i2c.h>

/* platform data for the DS620 temperature sensor and thermostat */

struct ds620_platform_data {
	/*
	 *  Thermostat output pin PO mode:
	 *  0 = always low (default)
	 *  1 = PO_LOW
	 *  2 = PO_HIGH
	 *
	 * (see Documentation/hwmon/ds620)
	 */
	int pomode;
};

#endif /* _LINUX_DS620_H */
