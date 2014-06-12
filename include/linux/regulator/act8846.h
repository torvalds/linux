/* include/linux/regulator/act8846.h
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __LINUX_REGULATOR_act8846_H
#define __LINUX_REGULATOR_act8846_H

#include <linux/regulator/machine.h>

//#define ACT8846_START 30

#define ACT8846_DCDC1  0                     //(0+ACT8846_START) 

#define ACT8846_LDO1 4                //(4+ACT8846_START)


#define act8846_NUM_REGULATORS 12
struct act8846;

void act8846_device_shutdown(void);

struct act8846_board {
	int irq;
	int irq_base;
	struct regulator_init_data *act8846_init_data[act8846_NUM_REGULATORS];
	struct device_node *of_node[act8846_NUM_REGULATORS];
	int pmic_sleep_gpio; /* */
	int pmic_hold_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	unsigned int dcdc_mode[3]; /* buckx_voltage in uV */
	bool pmic_sleep;
	unsigned int ldo_slp_voltage[7];
	bool pm_off;
};

struct act8846_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
	struct device_node *reg_node;
};

struct act8846_platform_data {
	int ono;
	int num_regulators;
	struct act8846_regulator_subdev *regulators;
	
	int pmic_sleep_gpio; /* */
	unsigned int dcdc_slp_voltage[3]; /* buckx_voltage in uV */
	bool pmic_sleep;
	unsigned int ldo_slp_voltage[7];
};

#endif

