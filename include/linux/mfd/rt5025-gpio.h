/*
 *  include/linux/mfd/rt5025-gpio.h
 *  Include header file for Richtek RT5025 PMIC GPIO file
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_RT5025_GPIO_H
#define __LINUX_RT5025_GPIO_H

#define RT5025_REG_GPIO0 0x1C
#define RT5025_REG_GPIO1 0x1D
#define RT5025_REG_GPIO2 0x1E

#define RT5025_GPIO_NR 3

#define RT5025_GPIO_INPUT 	0x00
#define RT5025_GPIO_OUTPUT 	0x02

#define RT5025_GPIO_DIRSHIFT 6
#define RT5025_GPIO_DIRMASK  0xC0
#define RT5025_GPIO_OVALUESHIFT 4
#define RT5025_GPIO_OVALUEMASK 0x10
#define RT5025_GPIO_IVALUEMASK 0x08

#endif /* #ifndef __LINUX_RT5025_GPIO_H */
