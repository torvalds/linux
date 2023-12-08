/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt5682s.h -- Platform data for RT5682I-VS
 *
 * Copyright 2021 Realtek Microelectronics
 */

#ifndef __LINUX_SND_RT5682S_H
#define __LINUX_SND_RT5682S_H

enum rt5682s_dmic1_data_pin {
	RT5682S_DMIC1_DATA_NULL,
	RT5682S_DMIC1_DATA_GPIO2,
	RT5682S_DMIC1_DATA_GPIO5,
};

enum rt5682s_dmic1_clk_pin {
	RT5682S_DMIC1_CLK_NULL,
	RT5682S_DMIC1_CLK_GPIO1,
	RT5682S_DMIC1_CLK_GPIO3,
};

enum rt5682s_jd_src {
	RT5682S_JD_NULL,
	RT5682S_JD1,
};

enum rt5682s_dai_clks {
	RT5682S_DAI_WCLK_IDX,
	RT5682S_DAI_BCLK_IDX,
	RT5682S_DAI_NUM_CLKS,
};

struct rt5682s_platform_data {

	int ldo1_en; /* GPIO for LDO1_EN */

	enum rt5682s_dmic1_data_pin dmic1_data_pin;
	enum rt5682s_dmic1_clk_pin dmic1_clk_pin;
	enum rt5682s_jd_src jd_src;
	unsigned int dmic_clk_rate;
	unsigned int dmic_delay;
	unsigned int amic_delay;
	bool dmic_clk_driving_high;

	const char *dai_clk_names[RT5682S_DAI_NUM_CLKS];
};

#endif
