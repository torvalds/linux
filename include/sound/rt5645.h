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

	bool dmic_en;
	unsigned int dmic1_data_pin;
	/* 0 = IN2N; 1 = GPIO5; 2 = GPIO11 */
	unsigned int dmic2_data_pin;
	/* 0 = IN2P; 1 = GPIO6; 2 = GPIO10; 3 = GPIO12 */

	unsigned int hp_det_gpio;
	bool gpio_hp_det_active_high;

	/* true if codec's jd function is used */
	bool en_jd_func;
	unsigned int jd_mode;
};

#endif
