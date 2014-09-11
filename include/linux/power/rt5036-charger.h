/*
 *  include/linux/power/rt5036-charger.h
 *  Include header file for Richtek RT5036 Charger Driver
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef _LINUX_POWER_RT5036_CHARGER_H
#define _LINUX_POWER_RT5036_CHARGER_H

enum {
	CHGEVENT_STAT2ALT,
	CHGEVENT_CHBSTLOWVI = 5,
	CHGEVENT_BSTOLI,
	CHGEVENT_BSTVIMIDOVP,
	CHGEVENT_CHTMRFI = 10,
	CHGEVENT_CHRCHGI,
	CHGEVENT_CHTERMI,
	CHGEVENT_CHBATOVI,
	CHGEVENT_CHRVPI = 15,
	CHGEVENT_BATABSENSE,
	CHGEVENT_CHBADADPI,
	CHGEVENT_VINCHGPLUGOUT,
	CHGEVENT_VINCHGPLUGIN,
	CHGEVENT_PPBATLVI,
	CHGEVENT_IEOCI,
	CHGEVENT_VINOVPI,
	CHGEVENT_MAX,
};

#define RT5036_CHGTEEN_MASK	0x08

#define RT5036_CHGIPREC_MASK	0x18
#define RT5036_CHGIPREC_SHIFT	3

#define RT5036_CHGIEOC_MASK	0x07

#define RT5036_CHGVPREC_MASK	0x0F

#define RT5036_CHGBATLV_MASK	0x07

#define RT5036_CHGVRECHG_MASK	0x0C
#define RT5036_CHGVRECHG_SHIFT	2

#define RT5036_CHGSTAT_MASK	0x30
#define RT5036_CHGSTAT_SHIFT	4
#define RT5036_CHGDIS_MASK	0x01
#define RT5036_CHGAICR_MASK	0xE0
#define RT5036_CHGAICR_SHIFT	5
#define RT5036_CHGICC_MASK	0xF0
#define RT5036_CHGICC_SHIFT	4
#define RT5036_CHGCV_MASK	0xFC
#define RT5036_CHGCV_SHIFT	2
#define RT5036_CHGOPAMODE_MASK	0x01
#define RT5036_CHGOPASTAT_MASK	0x08

#define RT5036_PWRRDY_MASK	0x80
#define RT5036_TSEVENT_MASK	0x0F
#define RT5036_TSWC_MASK	0x06
#define RT5036_TSHC_MASK	0x09
#define RT5036_CCJEITA_MASK	0x80
#define RT5036_CHGOTGEN_MASK	0x40
#define RT5036_BATDEN_MASK	0x80
#define RT5036_TERST_MASK	0x10

#endif /* #ifndef _LINUX_POWER_RT5036_CHARGER_H */
