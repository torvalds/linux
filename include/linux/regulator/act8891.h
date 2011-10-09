/* include/linux/regulator/act8891.h
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
#ifndef __LINUX_REGULATOR_act8891_H
#define __LINUX_REGULATOR_act8891_H

#include <linux/regulator/machine.h>

//#define ACT8891_START 30

#define ACT8891_LDO1  0                     //(0+ACT8891_START)
#define ACT8891_LDO2  1                    // (1+ACT8891_START)
#define ACT8891_LDO3  2                  //(2+ACT8891_START)
#define ACT8891_LDO4  3                //(3+ACT8891_START)


#define ACT8891_DCDC1 4                //(4+ACT8891_START)
#define ACT8891_DCDC2 5                //(5+ACT8891_START)
#define ACT8891_DCDC3 6                //(6+ACT8891_START)


#define act8891_NUM_REGULATORS 7
struct act8891;

struct act8891_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct act8891_platform_data {
	int num_regulators;
	int (*set_init)(struct act8891 *act8891);
	struct act8891_regulator_subdev *regulators;
};

#endif

