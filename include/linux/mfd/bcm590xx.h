/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Broadcom BCM590xx PMU
 *
 * Copyright 2014 Linaro Limited
 * Author: Matt Porter <mporter@linaro.org>
 */

#ifndef __LINUX_MFD_BCM590XX_H
#define __LINUX_MFD_BCM590XX_H

#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/regmap.h>

/* PMU ID register values; also used as device type */
#define BCM590XX_PMUID_BCM59054		0x54
#define BCM590XX_PMUID_BCM59056		0x56

/* max register address */
#define BCM590XX_MAX_REGISTER_PRI	0xe7
#define BCM590XX_MAX_REGISTER_SEC	0xf0

struct bcm590xx {
	struct device *dev;
	struct i2c_client *i2c_pri;
	struct i2c_client *i2c_sec;
	struct regmap *regmap_pri;
	struct regmap *regmap_sec;
	unsigned int id;

	/* PMU ID value; also used as device type */
	u8 pmu_id;
};

#endif /*  __LINUX_MFD_BCM590XX_H */
