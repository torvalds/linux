/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Atheros AR7XXX/AR9XXX GPIO controller platform data
 *
 * Copyright (C) 2015 Alban Bedel <albeu@free.fr>
 */

#ifndef __LINUX_PLATFORM_DATA_GPIO_ATH79_H
#define __LINUX_PLATFORM_DATA_GPIO_ATH79_H

struct ath79_gpio_platform_data {
	unsigned ngpios;
	bool oe_inverted;
};

#endif
