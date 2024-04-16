/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra210_dmic.h - Definitions for Tegra210 DMIC driver
 *
 * Copyright (c) 2020 NVIDIA CORPORATION.  All rights reserved.
 *
 */

#ifndef __TEGRA210_DMIC_H__
#define __TEGRA210_DMIC_H__

/* Register offsets from DMIC BASE */
#define TEGRA210_DMIC_TX_STATUS				0x0c
#define TEGRA210_DMIC_TX_INT_STATUS			0x10
#define TEGRA210_DMIC_TX_INT_MASK			0x14
#define TEGRA210_DMIC_TX_INT_SET			0x18
#define TEGRA210_DMIC_TX_INT_CLEAR			0x1c
#define TEGRA210_DMIC_TX_CIF_CTRL			0x20
#define TEGRA210_DMIC_ENABLE				0x40
#define TEGRA210_DMIC_SOFT_RESET			0x44
#define TEGRA210_DMIC_CG				0x48
#define TEGRA210_DMIC_STATUS				0x4c
#define TEGRA210_DMIC_INT_STATUS			0x50
#define TEGRA210_DMIC_CTRL				0x64
#define TEGRA210_DMIC_DBG_CTRL				0x70
#define TEGRA210_DMIC_DCR_BIQUAD_0_COEF_4		0x88
#define TEGRA210_DMIC_LP_FILTER_GAIN			0x8c
#define TEGRA210_DMIC_LP_BIQUAD_0_COEF_0		0x90
#define TEGRA210_DMIC_LP_BIQUAD_0_COEF_1		0x94
#define TEGRA210_DMIC_LP_BIQUAD_0_COEF_2		0x98
#define TEGRA210_DMIC_LP_BIQUAD_0_COEF_3		0x9c
#define TEGRA210_DMIC_LP_BIQUAD_0_COEF_4		0xa0
#define TEGRA210_DMIC_LP_BIQUAD_1_COEF_0		0xa4
#define TEGRA210_DMIC_LP_BIQUAD_1_COEF_1		0xa8
#define TEGRA210_DMIC_LP_BIQUAD_1_COEF_2		0xac
#define TEGRA210_DMIC_LP_BIQUAD_1_COEF_3		0xb0
#define TEGRA210_DMIC_LP_BIQUAD_1_COEF_4		0xb4

/* Fields in TEGRA210_DMIC_CTRL */
#define CH_SEL_SHIFT					8
#define TEGRA210_DMIC_CTRL_CHANNEL_SELECT_MASK		(0x3 << CH_SEL_SHIFT)
#define LRSEL_POL_SHIFT					4
#define TEGRA210_DMIC_CTRL_LRSEL_POLARITY_MASK		(0x1 << LRSEL_POL_SHIFT)
#define OSR_SHIFT					0
#define TEGRA210_DMIC_CTRL_OSR_MASK			(0x3 << OSR_SHIFT)

#define DMIC_OSR_FACTOR					64

#define DEFAULT_GAIN_Q23				0x800000

/* Max boost gain factor used for mixer control */
#define MAX_BOOST_GAIN 25599

enum tegra_dmic_ch_select {
	DMIC_CH_SELECT_LEFT,
	DMIC_CH_SELECT_RIGHT,
	DMIC_CH_SELECT_STEREO,
};

enum tegra_dmic_osr {
	DMIC_OSR_64,
	DMIC_OSR_128,
	DMIC_OSR_256,
};

enum tegra_dmic_lrsel {
	DMIC_LRSEL_LEFT,
	DMIC_LRSEL_RIGHT,
};

struct tegra210_dmic {
	struct clk *clk_dmic;
	struct regmap *regmap;
	unsigned int mono_to_stereo;
	unsigned int stereo_to_mono;
	unsigned int boost_gain;
	unsigned int ch_select;
	unsigned int osr_val;
	unsigned int lrsel;
};

#endif
