/*
 * Copyright (c) 2011, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#ifndef __RFKILL_GPIO_H
#define __RFKILL_GPIO_H

#include <linux/types.h>
#include <linux/rfkill.h>

/**
 * struct rfkill_gpio_platform_data - platform data for rfkill gpio device.
 * for unused gpio's, the expected value is -1.
 * @name:		name for the gpio rf kill instance
 * @reset_gpio:		GPIO which is used for reseting rfkill switch
 * @shutdown_gpio:	GPIO which is used for shutdown of rfkill switch
 * @power_clk_name:	[optional] name of clk to turn off while blocked
 */

struct rfkill_gpio_platform_data {
	char			*name;
	int			reset_gpio;
	int			shutdown_gpio;
	const char		*power_clk_name;
	enum rfkill_type	type;
};

#endif /* __RFKILL_GPIO_H */
