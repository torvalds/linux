/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * wm8400 client interface
 *
 * Copyright 2008 Wolfson Microelectronics plc
 */

#ifndef __LINUX_MFD_WM8400_H
#define __LINUX_MFD_WM8400_H

#include <linux/regulator/machine.h>

#define WM8400_LDO1  0
#define WM8400_LDO2  1
#define WM8400_LDO3  2
#define WM8400_LDO4  3
#define WM8400_DCDC1 4
#define WM8400_DCDC2 5

struct wm8400_platform_data {
	int (*platform_init)(struct device *dev);
};

int wm8400_register_regulator(struct device *dev, int reg,
			      struct regulator_init_data *initdata);

#endif
