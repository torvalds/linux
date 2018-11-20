/*
 * DaVinci GPIO Platform Related Defines
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DAVINCI_GPIO_PLATFORM_H
#define __DAVINCI_GPIO_PLATFORM_H

struct davinci_gpio_platform_data {
	u32	ngpio;
	u32	gpio_unbanked;
};

/* Convert GPIO signal to GPIO pin number */
#define GPIO_TO_PIN(bank, gpio)	(16 * (bank) + (gpio))

#endif
