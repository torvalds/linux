/*
 * linux/sound/rt5677.h -- Platform data for RT5677
 *
 * Copyright 2013 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_RT5677_H
#define __LINUX_SND_RT5677_H

struct rt5677_platform_data {
	/* IN1 IN2 can optionally be differential */
	bool in1_diff;
	bool in2_diff;
};

#endif
