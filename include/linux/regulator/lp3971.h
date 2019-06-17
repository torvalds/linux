/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * National Semiconductors LP3971 PMIC chip client interface
 *
 *  Copyright (C) 2009 Samsung Electronics
 *  Author: Marek Szyprowski <m.szyprowski@samsung.com>
 *
 * Based on wm8400.h
 */

#ifndef __LINUX_REGULATOR_LP3971_H
#define __LINUX_REGULATOR_LP3971_H

#include <linux/regulator/machine.h>

#define LP3971_LDO1  0
#define LP3971_LDO2  1
#define LP3971_LDO3  2
#define LP3971_LDO4  3
#define LP3971_LDO5  4

#define LP3971_DCDC1 5
#define LP3971_DCDC2 6
#define LP3971_DCDC3 7

#define LP3971_NUM_REGULATORS 8

struct lp3971_regulator_subdev {
	int id;
	struct regulator_init_data *initdata;
};

struct lp3971_platform_data {
	int num_regulators;
	struct lp3971_regulator_subdev *regulators;
};

#endif
