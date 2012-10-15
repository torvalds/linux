/*
 * max77686.h - Driver for the Maxim 77686
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
 * MAX77686 has PMIC, RTC devices.
 * The devices share the same I2C bus and included in
 * this mfd driver.
 */

#ifndef __LINUX_MFD_MAX77686_H
#define __LINUX_MFD_MAX77686_H

#include <linux/regulator/consumer.h>

/* MAX77686 regulator IDs */
enum max77686_regulators {
	MAX77686_LDO1 = 0,
	MAX77686_LDO2,
	MAX77686_LDO3,
	MAX77686_LDO4,
	MAX77686_LDO5,
	MAX77686_LDO6,
	MAX77686_LDO7,
	MAX77686_LDO8,
	MAX77686_LDO9,
	MAX77686_LDO10,
	MAX77686_LDO11,
	MAX77686_LDO12,
	MAX77686_LDO13,
	MAX77686_LDO14,
	MAX77686_LDO15,
	MAX77686_LDO16,
	MAX77686_LDO17,
	MAX77686_LDO18,
	MAX77686_LDO19,
	MAX77686_LDO20,
	MAX77686_LDO21,
	MAX77686_LDO22,
	MAX77686_LDO23,
	MAX77686_LDO24,
	MAX77686_LDO25,
	MAX77686_LDO26,
	MAX77686_BUCK1,
	MAX77686_BUCK2,
	MAX77686_BUCK3,
	MAX77686_BUCK4,
	MAX77686_BUCK5,
	MAX77686_BUCK6,
	MAX77686_BUCK7,
	MAX77686_BUCK8,
	MAX77686_BUCK9,

	MAX77686_REG_MAX,
};

struct max77686_regulator_data {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *of_node;
};

enum max77686_opmode {
	MAX77686_OPMODE_NORMAL,
	MAX77686_OPMODE_LP,
	MAX77686_OPMODE_STANDBY,
};

struct max77686_opmode_data {
	int id;
	int mode;
};

struct max77686_platform_data {
	/* IRQ */
	int irq_gpio;
	int ono;
	int wakeup;

	/* ---- PMIC ---- */
	struct max77686_regulator_data *regulators;
	int num_regulators;

	struct max77686_opmode_data *opmode_data;

	/*
	 * GPIO-DVS feature is not enabled with the current version of
	 * MAX77686 driver. Buck2/3/4_voltages[0] is used as the default
	 * voltage at probe. DVS/SELB gpios are set as OUTPUT-LOW.
	 */
	int buck234_gpio_dvs[3]; /* GPIO of [0]DVS1, [1]DVS2, [2]DVS3 */
	int buck234_gpio_selb[3]; /* [0]SELB2, [1]SELB3, [2]SELB4 */
	unsigned int buck2_voltage[8]; /* buckx_voltage in uV */
	unsigned int buck3_voltage[8];
	unsigned int buck4_voltage[8];
};

#endif /* __LINUX_MFD_MAX77686_H */
