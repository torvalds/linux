/*
 * max8997.h - Driver for the Maxim 8997/8966
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
 * This driver is based on max8998.h
 *
 * MAX8997 has PMIC, MUIC, HAPTIC, RTC, FLASH, and Fuel Gauge devices.
 * Except Fuel Gauge, every device shares the same I2C bus and included in
 * this mfd driver. Although the fuel gauge is included in the chip, it is
 * excluded from the driver because a) it has a different I2C bus from
 * others and b) it can be enabled simply by using MAX17042 driver.
 */

#ifndef __LINUX_MFD_MAX8998_H
#define __LINUX_MFD_MAX8998_H

#include <linux/regulator/consumer.h>

/* MAX8997/8966 regulator IDs */
enum max8998_regulators {
	MAX8997_LDO1 = 0,
	MAX8997_LDO2,
	MAX8997_LDO3,
	MAX8997_LDO4,
	MAX8997_LDO5,
	MAX8997_LDO6,
	MAX8997_LDO7,
	MAX8997_LDO8,
	MAX8997_LDO9,
	MAX8997_LDO10,
	MAX8997_LDO11,
	MAX8997_LDO12,
	MAX8997_LDO13,
	MAX8997_LDO14,
	MAX8997_LDO15,
	MAX8997_LDO16,
	MAX8997_LDO17,
	MAX8997_LDO18,
	MAX8997_LDO21,
	MAX8997_BUCK1,
	MAX8997_BUCK2,
	MAX8997_BUCK3,
	MAX8997_BUCK4,
	MAX8997_BUCK5,
	MAX8997_BUCK6,
	MAX8997_BUCK7,
	MAX8997_EN32KHZ_AP,
	MAX8997_EN32KHZ_CP,
	MAX8997_ENVICHG,
	MAX8997_ESAFEOUT1,
	MAX8997_ESAFEOUT2,
	MAX8997_CHARGER_CV, /* control MBCCV of MBCCTRL3 */
	MAX8997_CHARGER, /* charger current, MBCCTRL4 */
	MAX8997_CHARGER_TOPOFF, /* MBCCTRL5 */

	MAX8997_REG_MAX,
};

struct max8997_regulator_data {
	int id;
	struct regulator_init_data *initdata;
};

struct max8997_platform_data {
	/* IRQ */
	int irq_base;
	int ono;
	int wakeup;

	/* ---- PMIC ---- */
	struct max8997_regulator_data *regulators;
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
	int buck125_gpios[3]; /* GPIO of [0]SET1, [1]SET2, [2]SET3 */
	int buck125_default_idx; /* Default value of SET1, 2, 3 */
	unsigned int buck1_voltage[8]; /* buckx_voltage in uV */
	bool buck1_gpiodvs;
	unsigned int buck2_voltage[8];
	bool buck2_gpiodvs;
	unsigned int buck5_voltage[8];
	bool buck5_gpiodvs;

	/* ---- Charger control ---- */
	/* eoc stands for 'end of charge' */
	int eoc_mA; /* 50 ~ 200mA by 10mA step */
	/* charge Full Timeout */
	int timeout; /* 0 (no timeout), 5, 6, 7 hours */

	/* MUIC: Not implemented */
	/* HAPTIC: Not implemented */
	/* RTC: Not implemented */
	/* Flash: Not implemented */
};

#endif /* __LINUX_MFD_MAX8998_H */
