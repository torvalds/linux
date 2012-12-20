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


#define act8846_NUM_REGULATORS 13
struct act8846;

struct act8846_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct act8846_platform_data {
	int num_regulators;
	int (*set_init)(struct act8846 *act8846);
	struct act8846_regulator_subdev *regulators;
};

#endif

