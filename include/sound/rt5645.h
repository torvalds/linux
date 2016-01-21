/*
 * linux/sound/rt5645.h -- Platform data for RT5645
 *
 * Copyright 2013 Realtek Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_RT5645_H
#define __LINUX_SND_RT5645_H

struct rt5645_platform_data {
	/* IN2 can optionally be differential */
	bool in2_diff;

	unsigned int dmic1_data_pin;
	/* 0 = IN2N; 1 = GPIO5; 2 = GPIO11 */
	unsigned int dmic2_data_pin;
	/* 0 = IN2P; 1 = GPIO6; 2 = GPIO10; 3 = GPIO12 */

	unsigned int jd_mode;
	/* Invert JD when jack insert */
	bool jd_invert;
};

#endif
