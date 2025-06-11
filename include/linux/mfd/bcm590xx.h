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

/* Known chip revision IDs */
#define BCM59054_REV_DIGITAL_A1		1
#define BCM59054_REV_ANALOG_A1		2

#define BCM59056_REV_DIGITAL_A0		1
#define BCM59056_REV_ANALOG_A0		1

#define BCM59056_REV_DIGITAL_B0		2
#define BCM59056_REV_ANALOG_B0		2

/* regmap types */
enum bcm590xx_regmap_type {
	BCM590XX_REGMAP_PRI,
	BCM590XX_REGMAP_SEC,
};

/* max register address */
#define BCM590XX_MAX_REGISTER_PRI	0xe7
#define BCM590XX_MAX_REGISTER_SEC	0xf0

struct bcm590xx {
	struct device *dev;
	struct i2c_client *i2c_pri;
	struct i2c_client *i2c_sec;
	struct regmap *regmap_pri;
	struct regmap *regmap_sec;

	/* PMU ID value; also used as device type */
	u8 pmu_id;

	/* Chip revision, read from PMUREV reg */
	u8 rev_digital;
	u8 rev_analog;
};

#endif /*  __LINUX_MFD_BCM590XX_H */
