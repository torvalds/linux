/*
 *  include/linux/power/rt5025/rt-power.h
 *  Include header file for Richtek RT5025 Core charger Driver
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __LINUX_RT_POWER_H
#define __LINUX_RT_POWER_H

#define RT_AC_NAME	"rt-ac"
#define RT_USB_NAME	"rt-usb"

struct rt_power_data {
	int chg_volt;
	int acchg_icc;
	int usbtachg_icc;
	int usbchg_icc;
};
#endif /* #ifndef __LINUX_RT_POWER_H */

