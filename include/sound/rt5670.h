/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt5670.h -- Platform data for RT5670
 *
 * Copyright 2014 Realtek Microelectronics
 */

#ifndef __LINUX_SND_RT5670_H
#define __LINUX_SND_RT5670_H

struct rt5670_platform_data {
	int jd_mode;
	bool in2_diff;
	bool dev_gpio;

	bool dmic_en;
	unsigned int dmic1_data_pin;
	/* 0 = GPIO6; 1 = IN2P; 3 = GPIO7*/
	unsigned int dmic2_data_pin;
	/* 0 = GPIO8; 1 = IN3N; */
	unsigned int dmic3_data_pin;
	/* 0 = GPIO9; 1 = GPIO10; 2 = GPIO5*/
};

#endif
