/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */

#ifndef __CHARGER_BQ24735_H_
#define __CHARGER_BQ24735_H_

#include <linux/types.h>
#include <linux/power_supply.h>

struct bq24735_platform {
	uint32_t charge_current;
	uint32_t charge_voltage;
	uint32_t input_current;

	const char *name;

	bool ext_control;

	char **supplied_to;
	size_t num_supplicants;
};

#endif /* __CHARGER_BQ24735_H_ */
