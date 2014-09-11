/*
 *  include/linux/mfd/rt5036/rt5036-misc.h
 *  Include header file for Richtek RT5036 Misc option
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef _LINUX_MFD_RT5036_MISC_H
#define _LINUX_MFD_RT5036_MISC_H

enum {
	MISCEVENT_PWRONLP = 3,
	MISCEVENT_PWRONSP,
	MISCEVENT_PWRONF,
	MISCEVENT_PWRONR,
	MISCEVENT_KPSHDN,
	MISCEVENT_VDDALV,
	MISCEVNET_OTM,
	MISCEVENT_PMICSYSLV = 13,
	MISCEVENT_LSW2LV,
	MISCEVENT_LSW1LV,
	MISCEVENT_LDO4LV,
	MISCEVENT_LDO3LV,
	MISCEVENT_LDO2LV,
	MISCEVENT_LDO1LV,
	MISCEVENT_BUCK4LV,
	MISCEVENT_BUCK3LV,
	MISCEVENT_BUCK2LV,
	MISCEVENT_BUCK1LV,
	MISCEVENT_MAX,
};

#define RT5036_SHDNPRESS_MASK	0x0C
#define RT5036_SHDNPRESS_SHIFT	2

#define RT5036_STBEN_MASK	0x06
#define RT5036_STBEN_SHIFT	1

#define RT5036_LPSHDNEN_MASK	0x04
#define RT5036_CHIPSHDN_MASK	0x80
#define RT5036_SYSUVLO_SHIFT	5
#define RT5036_SYSUVLO_MASK	0xE0
#define RT5036_SYSLVENSHDN_MASK	0x20
#define RT5036_PWRRDY_MASK	0x80

extern int rt5036_vin_exist(void);
extern void rt5036_chip_shutdown(void);

#endif /* #ifndef _LINUX_MFD_RT5036_MISC_H */
