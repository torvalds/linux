/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt5645.h -- Platform data for RT5645
 *
 * Copyright 2013 Realtek Microelectronics
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
	/* Use level triggered irq */
	bool level_trigger_irq;
	/* Invert JD1_1 status polarity */
	bool inv_jd1_1;
	/* Invert HP detect status polarity */
	bool inv_hp_pol;

	/* Value to asign to snd_soc_card.long_name */
	const char *long_name;
};

#endif
