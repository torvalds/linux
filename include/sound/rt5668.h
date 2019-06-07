/*
 * linux/sound/rt5668.h -- Platform data for RT5668
 *
 * Copyright 2018 Realtek Microelectronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_SND_RT5668_H
#define __LINUX_SND_RT5668_H

enum rt5668_dmic1_data_pin {
	RT5668_DMIC1_NULL,
	RT5668_DMIC1_DATA_GPIO2,
	RT5668_DMIC1_DATA_GPIO5,
};

enum rt5668_dmic1_clk_pin {
	RT5668_DMIC1_CLK_GPIO1,
	RT5668_DMIC1_CLK_GPIO3,
};

enum rt5668_jd_src {
	RT5668_JD_NULL,
	RT5668_JD1,
};

struct rt5668_platform_data {

	int ldo1_en; /* GPIO for LDO1_EN */

	enum rt5668_dmic1_data_pin dmic1_data_pin;
	enum rt5668_dmic1_clk_pin dmic1_clk_pin;
	enum rt5668_jd_src jd_src;
};

#endif

