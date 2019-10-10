/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) 2009, Jiejing Zhang <kzjeef@gmail.com>
 */

#ifndef __JZ4740_BATTERY_H
#define __JZ4740_BATTERY_H

struct jz_battery_platform_data {
	struct power_supply_info info;
	int gpio_charge;	/* GPIO port of Charger state */
	int gpio_charge_active_low;
};

#endif
