/*
 * linux/sound/wm2200.h -- Platform data for WM2200
 *
 * Copyright 2012 Wolfson Microelectronics. PLC.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_WM2200_H
#define __LINUX_SND_WM2200_H

#define WM2200_GPIO_SET 0x10000
#define WM2200_MAX_MICBIAS 2

enum wm2200_in_mode {
	WM2200_IN_SE = 0,
	WM2200_IN_DIFF = 1,
	WM2200_IN_DMIC = 2,
};

enum wm2200_dmic_sup {
	WM2200_DMIC_SUP_MICVDD = 0,
	WM2200_DMIC_SUP_MICBIAS1 = 1,
	WM2200_DMIC_SUP_MICBIAS2 = 2,
};

enum wm2200_mbias_lvl {
	WM2200_MBIAS_LVL_1V5 = 1,
	WM2200_MBIAS_LVL_1V8 = 2,
	WM2200_MBIAS_LVL_1V9 = 3,
	WM2200_MBIAS_LVL_2V0 = 4,
	WM2200_MBIAS_LVL_2V2 = 5,
	WM2200_MBIAS_LVL_2V4 = 6,
	WM2200_MBIAS_LVL_2V5 = 7,
	WM2200_MBIAS_LVL_2V6 = 8,
};

struct wm2200_micbias {
	enum wm2200_mbias_lvl mb_lvl;      /** Regulated voltage */
	unsigned int discharge:1;          /** Actively discharge */
	unsigned int fast_start:1;         /** Enable aggressive startup ramp rate */
	unsigned int bypass:1;             /** Use bypass mode */
};

struct wm2200_pdata {
	int reset;      /** GPIO controlling /RESET, if any */
	int ldo_ena;    /** GPIO controlling LODENA, if any */
	int irq_flags;

	int gpio_defaults[4];

	enum wm2200_in_mode in_mode[3];
	enum wm2200_dmic_sup dmic_sup[3];

	/** MICBIAS configurations */
	struct wm2200_micbias micbias[WM2200_MAX_MICBIAS];
};

#endif
