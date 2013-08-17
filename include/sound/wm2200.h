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

struct wm2200_pdata {
	int reset;      /** GPIO controlling /RESET, if any */
	int ldo_ena;    /** GPIO controlling LODENA, if any */
	int irq_flags;

	int gpio_defaults[4];

	enum wm2200_in_mode in_mode[3];
	enum wm2200_dmic_sup dmic_sup[3];

	int micbias_cfg[2];  /** Register value to configure MICBIAS */
};

#endif
