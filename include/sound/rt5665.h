/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/sound/rt5665.h -- Platform data for RT5665
 *
 * Copyright 2016 Realtek Microelectronics
 */

#ifndef __LINUX_SND_RT5665_H
#define __LINUX_SND_RT5665_H

enum rt5665_dmic1_data_pin {
	RT5665_DMIC1_NULL,
	RT5665_DMIC1_DATA_GPIO4,
	RT5665_DMIC1_DATA_IN2N,
};

enum rt5665_dmic2_data_pin {
	RT5665_DMIC2_NULL,
	RT5665_DMIC2_DATA_GPIO5,
	RT5665_DMIC2_DATA_IN2P,
};

enum rt5665_jd_src {
	RT5665_JD_NULL,
	RT5665_JD1,
};

struct rt5665_platform_data {
	bool in1_diff;
	bool in2_diff;
	bool in3_diff;
	bool in4_diff;

	int ldo1_en; /* GPIO for LDO1_EN */

	enum rt5665_dmic1_data_pin dmic1_data_pin;
	enum rt5665_dmic2_data_pin dmic2_data_pin;
	enum rt5665_jd_src jd_src;

	unsigned int sar_hs_type;
};

#endif

