/*
 * max77xxx.h - Driver for the Maxim 77686/802
 *
 * Copyright (c) 2013 Google, Inc
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  Chiwoong Byun <woong.byun@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This driver is based on max8997.h
 *
 * MAX77XXX has PMIC, RTC devices.
 * The devices share the same I2C bus and included in
 * this mfd driver.
 */

#ifndef __LINUX_MFD_MAX77XXX_H
#define __LINUX_MFD_MAX77XXX_H

#include <linux/regulator/consumer.h>

/* MAX77XXX regulator IDs - LDOS must come before BUCKs */
enum max77xxx_regulators {
	MAX77XXX_LDO1 = 0,
	MAX77XXX_LDO2,
	MAX77XXX_LDO3,
	MAX77XXX_LDO4,
	MAX77XXX_LDO5,
	MAX77XXX_LDO6,
	MAX77XXX_LDO7,
	MAX77XXX_LDO8,
	MAX77XXX_LDO9,
	MAX77XXX_LDO10,
	MAX77XXX_LDO11,
	MAX77XXX_LDO12,
	MAX77XXX_LDO13,
	MAX77XXX_LDO14,
	MAX77XXX_LDO15,
	MAX77XXX_LDO16,
	MAX77XXX_LDO17,
	MAX77XXX_LDO18,
	MAX77XXX_LDO19,
	MAX77XXX_LDO20,
	MAX77XXX_LDO21,
	MAX77XXX_LDO22,
	MAX77XXX_LDO23,
	MAX77XXX_LDO24,
	MAX77XXX_LDO25,
	MAX77XXX_LDO26,
	MAX77XXX_LDO27,
	MAX77XXX_LDO28,
	MAX77XXX_LDO29,
	MAX77XXX_LDO30,
	MAX77XXX_LDO31,
	MAX77XXX_LDO32,
	MAX77XXX_LDO33,
	MAX77XXX_LDO34,
	MAX77XXX_LDO35,
	MAX77XXX_BUCK1,
	MAX77XXX_BUCK2,
	MAX77XXX_BUCK3,
	MAX77XXX_BUCK4,
	MAX77XXX_BUCK5,
	MAX77XXX_BUCK6,
	MAX77XXX_BUCK7,
	MAX77XXX_BUCK8,
	MAX77XXX_BUCK9,
	MAX77XXX_BUCK10,
	MAX77XXX_EN32KHZ_AP,
	MAX77XXX_EN32KHZ_CP,
	MAX77XXX_P32KHZ,

	MAX77XXX_REG_MAX,
};

struct max77xxx_regulator_data {
	int id;
	int opmode;
	struct regulator_init_data *initdata;
	struct device_node *of_node;
};

enum max77xxx_opmode {
	MAX77XXX_OPMODE_OFF,
	MAX77XXX_OPMODE_STANDBY,
	MAX77XXX_OPMODE_LP,
	MAX77XXX_OPMODE_NORMAL,
};

struct max77xxx_opmode_data {
	int id;
	int mode;
};

struct max77xxx_platform_data {
	/* IRQ */
	int irq_gpio;
	int ono;
	int wakeup;

	/* ---- PMIC ---- */
	struct max77xxx_regulator_data *regulators;
	int num_regulators;

	struct max77xxx_opmode_data *opmode_data;

	/*
	 * GPIO-DVS feature is not fully enabled with the current version of
	 * MAX77XXX driver, but the driver does support using a DVS index other
	 * than the default of 0.
	 */
	int buck_gpio_dvs[3]; /* GPIO of [0]DVS1, [1]DVS2, [2]DVS3 */
	int buck_default_idx; /* Default value of DVS1, 2, 3 */

	int buck_gpio_selb[5]; /* 77686: 2, 3, 4; 77802: 1, 2, 3, 4, 6 */
};

#endif /* __LINUX_MFD_MAX77XXX_H */
