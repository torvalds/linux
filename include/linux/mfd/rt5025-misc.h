/*
 *  include/linux/mfd/rt5025/rt5025-misc.h
 *  Include header file for Richtek RT5025 PMIC Misc
 *
 *  Copyright (C) 2013 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef __LINUX_RT5025_MISC_H
#define __LINUX_RT5025_MISC_H

enum {
	MISCEVENT_GPIO0_IE = 1,
	MISCEVENT_GPIO1_IE,
	MISCEVENT_GPIO2_IE,
	MISCEVENT_RESETB,
	MISCEVENT_PWRONF,
	MISCEVENT_PWRONR,
	MISCEVENT_KPSHDN,
	MISCEVENT_SYSLV,
	MISCEVENT_DCDC4LVHV,
	MISCEVENT_PWRONLP_IRQ,
	MISCEVENT_PWRONSP_IRQ,
	MISCEVENT_DCDC3LV,
	MISCEVENT_DCDC2LV,
	MISCEVENT_DCDC1LV,
	MISCEVENT_OT,
	MISCEVENT_MAX,
};

#define RT5025_SHDNCTRL_MASK	0x80
#define RT5025_VSYSOFF_MASK	0xE0
#define RT5025_VSYSOFF_SHFT	5
#define RT5025_SHDNLPRESS_MASK	0x0C
#define RT5025_SHDNLPRESS_SHFT	2
#define RT5025_STARTLPRESS_MASK	0xC0
#define RT5025_STARTLPRESS_SHFT	6
#define RT5025_VSYSLVSHDN_MASK	0x80
#define RT5025_VSYSLVSHDN_SHFT	7
#define RT5025_CABLEIN_MASK	0x03

extern int rt5025_cable_exist(void);

#endif /* #ifndef __LINUX_RT5025_MISC_H */
