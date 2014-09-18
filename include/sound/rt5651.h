/*
 * linux/sound/rt286.h -- Platform data for RT286
 *
 * Copyright 2013 Realtek Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_RT5651_H
#define __LINUX_SND_RT5651_H

struct rt5651_platform_data {
	/* IN2 can optionally be differential */
	bool in2_diff;

	bool dmic_en;
};

#endif
