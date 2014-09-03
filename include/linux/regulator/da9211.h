/*
 * da9211.h - Regulator device driver for DA9211
 * Copyright (C) 2014  Dialog Semiconductor Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 */

#ifndef __LINUX_REGULATOR_DA9211_H
#define __LINUX_REGULATOR_DA9211_H

#include <linux/regulator/machine.h>

#define DA9211_MAX_REGULATORS	2

struct da9211_pdata {
	/*
	 * Number of buck
	 * 1 : 4 phase 1 buck
	 * 2 : 2 phase 2 buck
	 */
	int num_buck;
	struct regulator_init_data *init_data;
};
#endif
