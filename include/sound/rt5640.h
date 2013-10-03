/*
 * linux/sound/rt5640.h -- Platform data for RT5640
 *
 * Copyright 2011 Realtek Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_RT5640_H
#define __LINUX_SND_RT5640_H

struct rt5640_platform_data {
	/* IN1 & IN2 can optionally be differential */
	bool in1_diff;
	bool in2_diff;

	int ldo1_en; /* GPIO for LDO1_EN */
};

#endif
