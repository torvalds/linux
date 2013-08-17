/*
 * wm8400 client interface
 *
 * Copyright 2008 Wolfson Microelectronics plc
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
