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

enum rt5677_dmic2_clk {
	RT5677_DMIC_CLK1 = 0,
	RT5677_DMIC_CLK2 = 1,
};


struct rt5677_platform_data {
	/* IN1/IN2/LOUT1/LOUT2/LOUT3 can optionally be differential */
	bool in1_diff;
	bool in2_diff;
	bool lout1_diff;
	bool lout2_diff;
	bool lout3_diff;
	/* DMIC2 clock source selection */
	enum rt5677_dmic2_clk dmic2_clk_pin;

	/* configures GPIO, 0 - floating, 1 - pulldown, 2 - pullup */
	u8 gpio_config[6];
};

#endif
