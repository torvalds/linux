/* include/linux/regulator/charge-regulator.h
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
#ifndef __LINUX_REGULATOR_CHARGE_H

#define __LINUX_REGULATOR_CHARGE_H

#include <linux/regulator/machine.h>


struct regulator_init_data;

struct charge_platform_data {
	int gpio_charge;
	struct regulator_init_data *init_data;
};

#endif

