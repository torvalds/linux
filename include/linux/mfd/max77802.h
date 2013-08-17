/*
 * max77802.h - Driver for the Maxim 77802
 *
 *  Copyright (C) 2011 Samsung Electrnoics
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
 * MAX77802 has PMIC, RTC devices.
 * The devices share the same I2C bus and included in
 * this mfd driver.
 */

#ifndef __LINUX_MFD_MAX77802_H
#define __LINUX_MFD_MAX77802_H

#include <linux/regulator/consumer.h>

#define MAX77802_SMPL_ENABLE			(0x1)
#define MAX77802_WTSR_ENABLE			(0x2)

/* MAX77802 regulator IDs */
enum max77802_regulators {
	MAX77802_LDO1 = 0,
	MAX77802_LDO2,
	MAX77802_LDO3,
	MAX77802_LDO4,
	MAX77802_LDO5,
	MAX77802_LDO6,
	MAX77802_LDO7,
	MAX77802_LDO8,
	MAX77802_LDO9,
	MAX77802_LDO10,
	MAX77802_LDO11,
	MAX77802_LDO12,
	MAX77802_LDO13,
	MAX77802_LDO14,
	MAX77802_LDO15,
	MAX77802_LDO17,
	MAX77802_LDO18,
	MAX77802_LDO19,
	MAX77802_LDO20,
	MAX77802_LDO21,
	MAX77802_LDO23,
	MAX77802_LDO24,
	MAX77802_LDO25,
	MAX77802_LDO26,
	MAX77802_LDO27,
	MAX77802_LDO28,
	MAX77802_LDO29,
	MAX77802_LDO30,
	MAX77802_LDO32,
	MAX77802_LDO33,
	MAX77802_LDO34,
	MAX77802_LDO35,
	MAX77802_BUCK1,
	MAX77802_BUCK2,
	MAX77802_BUCK3,
	MAX77802_BUCK4,
	MAX77802_BUCK5,
	MAX77802_BUCK6,
	MAX77802_BUCK7,
	MAX77802_BUCK8,
	MAX77802_BUCK9,
	MAX77802_BUCK10,
#if 0
	MAX77802_EN32KHZ_AP,
	MAX77802_EN32KHZ_CP,
#endif
	MAX77802_REG_MAX,
};

struct max77802_regulator_data {
	int id;
	struct regulator_init_data *initdata;
};

enum max77802_opmode {
	MAX77802_OPMODE_NORMAL,
	MAX77802_OPMODE_LP,
	MAX77802_OPMODE_STANDBY,
};

enum max77802_ramp_rate {
	MAX77802_RAMP_RATE_12P5MV,
	MAX77802_RAMP_RATE_25MV,
	MAX77802_RAMP_RATE_50MV,
	MAX77802_RAMP_RATE_100MV,
};

struct max77802_opmode_data {
	int id;
	int mode;
};

struct max77802_buck12346_gpio_data {
	int gpio;
	int data;
};

struct max77802_platform_data {
	/* IRQ */
	int irq_gpio;
	int irq_base;
	int ono;
	int wakeup;

	/* ---- PMIC ---- */
	struct max77802_regulator_data *regulators;
	int num_regulators;
	int has_full_constraints;

	struct max77802_opmode_data *opmode_data;
	int ramp_rate;
	int wtsr_smpl;

	/*
	 * GPIO-DVS feature is not enabled with the current version of
	 * MAX77802 driver. Buck2/3/4_voltages[0] is used as the default
	 * voltage at probe. DVS/SELB gpios are set as OUTPUT-LOW.
	 */
	struct max77802_buck12346_gpio_data buck12346_gpio_dvs[3]; /* GPIO of [0]DVS1, [1]DVS2, [2]DVS3 */
	int buck12346_gpio_selb[5]; /* [0]SELB1, [1]SELB2, [2]SELB3, [3]SELB4, [4]SELB6 */
	unsigned int buck1_voltage[8]; /* buckx_voltage in uV */
	unsigned int buck2_voltage[8];
	unsigned int buck3_voltage[8];
	unsigned int buck4_voltage[8];
	unsigned int buck6_voltage[8];
};

#endif /* __LINUX_MFD_MAX77802_H */
