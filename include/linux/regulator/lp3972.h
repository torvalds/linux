/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * National Semiconductors LP3972 PMIC chip client interface
 *
 * Based on lp3971.h
 */

#ifndef __LINUX_REGULATOR_LP3972_H
#define __LINUX_REGULATOR_LP3972_H

#include <linux/regulator/machine.h>

#define LP3972_LDO1  0
#define LP3972_LDO2  1
#define LP3972_LDO3  2
#define LP3972_LDO4  3
#define LP3972_LDO5  4

#define LP3972_DCDC1 5
#define LP3972_DCDC2 6
#define LP3972_DCDC3 7

#define LP3972_NUM_REGULATORS 8

struct lp3972_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct lp3972_platform_data {
	int num_regulators;
	struct lp3972_regulator_subdev *regulators;
};

#endif
