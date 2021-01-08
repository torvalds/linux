/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * DA9121 Single-channel dual-phase 10A buck converter
 * DA9130 Single-channel dual-phase 10A buck converter (Automotive)
 * DA9217 Single-channel dual-phase  6A buck converter
 * DA9122 Dual-channel single-phase  5A buck converter
 * DA9131 Dual-channel single-phase  5A buck converter (Automotive)
 * DA9220 Dual-channel single-phase  3A buck converter
 * DA9132 Dual-channel single-phase  3A buck converter (Automotive)
 *
 * Copyright (C) 2020  Dialog Semiconductor
 *
 * Authors: Adam Ward, Dialog Semiconductor
 */

#ifndef __LINUX_REGULATOR_DA9121_H
#define __LINUX_REGULATOR_DA9121_H

#include <linux/regulator/machine.h>

struct gpio_desc;

enum {
	DA9121_IDX_BUCK1,
	DA9121_IDX_BUCK2,
	DA9121_IDX_MAX
};

struct da9121_pdata {
	int num_buck;
	struct gpio_desc *gpiod_ren[DA9121_IDX_MAX];
	struct device_node *reg_node[DA9121_IDX_MAX];
	struct regulator_init_data *init_data[DA9121_IDX_MAX];
};

#endif
