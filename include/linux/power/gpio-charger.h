/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 */

#ifndef __LINUX_POWER_GPIO_CHARGER_H__
#define __LINUX_POWER_GPIO_CHARGER_H__

#include <linux/power_supply.h>
#include <linux/types.h>

/**
 * struct gpio_charger_platform_data - platform_data for gpio_charger devices
 * @name:		Name for the chargers power_supply device
 * @type:		Type of the charger
 * @supplied_to:	Array of battery names to which this chargers supplies power
 * @num_supplicants:	Number of entries in the supplied_to array
 */
struct gpio_charger_platform_data {
	const char *name;
	enum power_supply_type type;
	char **supplied_to;
	size_t num_supplicants;
};

#endif
