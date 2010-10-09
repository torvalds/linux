/* include/linux/regulator/rk2818_lp8725.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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
#ifndef __LINUX_REGULATOR_LP8725_H
#define __LINUX_REGULATOR_LP8725_H

#include <linux/regulator/machine.h>

#define LP8725_LDO1  0
#define LP8725_LDO2  1
#define LP8725_LDO3  2
#define LP8725_LDO4  3
#define LP8725_LDO5  4

#define LP8725_LILO1 5
#define LP8725_LILO2 6

#define LP8725_DCDC1 7
#define LP8725_DCDC2 8
#define LP8725_DCDC1_V2 9
#define LP8725_DCDC2_V2 10

#define LP8725_NUM_REGULATORS 11

struct lp8725_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct lp8725_platform_data {
	int num_regulators;
	struct lp8725_regulator_subdev *regulators;
};
extern int rk2818_lp8725_pm_control(void);
#endif
