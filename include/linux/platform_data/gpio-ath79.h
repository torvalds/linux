/*
 *  Atheros AR7XXX/AR9XXX GPIO controller platform data
 *
 * Copyright (C) 2015 Alban Bedel <albeu@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_PLATFORM_DATA_GPIO_ATH79_H
#define __LINUX_PLATFORM_DATA_GPIO_ATH79_H

struct ath79_gpio_platform_data {
	unsigned ngpios;
	bool oe_inverted;
};

#endif
