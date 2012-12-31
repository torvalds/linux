/*
 * max77686.h - Driver for the Maxim 77686
 *
 *  Copyright (C) 2009-2010 Samsung Electrnoics
 *  MyungJoo Ham <myungjoo.ham@samsung.com>
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
 * This driver is based on max77686.h
 *
 * MAX77686 has PMIC, MUIC, HAPTIC, RTC, FLASH, and Fuel Gauge devices.
 * Except Fuel Gauge, every device shares the same I2C bus and included in
 * this mfd driver. Although the fuel gauge is included in the chip, it is
 * excluded from the driver because a) it has a different I2C bus from
 * others and b) it can be enabled simply by using MAX17042 driver.
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

	MAX77686_EN32KHZ_AP,
	MAX77686_EN32KHZ_CP,

	MAX77686_REG_MAX,
};

struct max77686_regulator_data {
	int id;
	struct regulator_init_data *initdata;
};

struct max77686_platform_data {
	/* IRQ */
	int irq_base;
	int ono;
	int wakeup;

	/* ---- PMIC ---- */
	struct max77686_regulator_data *regulators;
	int num_regulators;

	/*
	 * SET1~3 DVS GPIOs control Buck1, 2, and 5 simultaneously. Therefore,
	 * With buckx_gpiodvs enabled, the buckx cannot be controlled
	 * independently. To control buckx (of 1, 2, and 5) independently,
	 * disable buckx_gpiodvs and control with BUCKxDVS1 register.
	 *
	 * When buckx_gpiodvs and bucky_gpiodvs are both enabled, set_voltage
	 * on buckx will change the voltage of bucky at the same time.
	 *
	 */
	bool ignore_gpiodvs_side_effect;
	int buck234_gpios[3]; /* GPIO of [0]SET1, [1]SET2, [2]SET3 */
	int buck234_default_idx; /* Default value of SET1, 2, 3 */
	unsigned int buck2_voltage[8]; /* buckx_voltage in uV */
	bool buck2_gpiodvs;
	unsigned int buck3_voltage[8];
	bool buck3_gpiodvs;
	unsigned int buck4_voltage[8];
	bool buck4_gpiodvs;

	/* RTC: Not implemented */
};

#endif /* __LINUX_MFD_MAX77686_H */
