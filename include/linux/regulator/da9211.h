/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * da9211.h - Regulator device driver for DA9211/DA9212
 * /DA9213/DA9223/DA9214/DA9224/DA9215/DA9225
 * Copyright (C) 2015  Dialog Semiconductor Ltd.
 */

#ifndef __LINUX_REGULATOR_DA9211_H
#define __LINUX_REGULATOR_DA9211_H

#include <linux/regulator/machine.h>

#define DA9211_MAX_REGULATORS	2

struct gpio_desc;

enum da9211_chip_id {
	DA9211,
	DA9212,
	DA9213,
	DA9223,
	DA9214,
	DA9224,
	DA9215,
	DA9225,
};

struct da9211_pdata {
	/*
	 * Number of buck
	 * 1 : 4 phase 1 buck
	 * 2 : 2 phase 2 buck
	 */
	int num_buck;
	struct gpio_desc *gpiod_ren[DA9211_MAX_REGULATORS];
	struct device_node *reg_node[DA9211_MAX_REGULATORS];
	struct regulator_init_data *init_data[DA9211_MAX_REGULATORS];
};
#endif
