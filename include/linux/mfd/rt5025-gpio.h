/*
 *  include/linux/mfd/rt5025/rt5025-gpio.h
 *  Include header file for Richtek RT5025 PMIC GPIO file
 *
 *  Copyright (C) 2013 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __LINUX_RT5025_GPIO_H
#define __LINUX_RT5025_GPIO_H

#define RT5025_GPIO_INPUT	0x00
#define RT5025_GPIO_OUTPUT		0x02

#define RT5025_GPIO_DIRSHIFT 6
#define RT5025_GPIO_DIRMASK  0xC0
#define RT5025_GPIO_OVALUESHIFT 4
#define RT5025_GPIO_OVALUEMASK 0x10
#define RT5025_GPIO_IVALUEMASK 0x08

#endif /* #ifndef __LINUX_RT5025_GPIO_H */
