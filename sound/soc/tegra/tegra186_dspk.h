/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra186_dspk.h - Definitions for Tegra186 DSPK driver
 *
 * Copyright (c) 2020 NVIDIA CORPORATION. All rights reserved.
 *
 */

#ifndef __TEGRA186_DSPK_H__
#define __TEGRA186_DSPK_H__

/* Register offsets from DSPK BASE */
#define TEGRA186_DSPK_RX_STATUS			0x0c
#define TEGRA186_DSPK_RX_INT_STATUS		0x10
#define TEGRA186_DSPK_RX_INT_MASK		0x14
#define TEGRA186_DSPK_RX_INT_SET		0x18
#define TEGRA186_DSPK_RX_INT_CLEAR		0x1c
#define TEGRA186_DSPK_RX_CIF_CTRL		0x20
#define TEGRA186_DSPK_ENABLE			0x40
#define TEGRA186_DSPK_SOFT_RESET		0x44
#define TEGRA186_DSPK_CG			0x48
#define TEGRA186_DSPK_STATUS			0x4c
#define TEGRA186_DSPK_INT_STATUS		0x50
#define TEGRA186_DSPK_CORE_CTRL			0x60
#define TEGRA186_DSPK_CODEC_CTRL		0x64

/* DSPK CORE CONTROL fields */
#define CH_SEL_SHIFT				8
#define TEGRA186_DSPK_CHANNEL_SELECT_MASK	(0x3 << CH_SEL_SHIFT)
#define DSPK_OSR_SHIFT				4
#define TEGRA186_DSPK_OSR_MASK			(0x3 << DSPK_OSR_SHIFT)
#define LRSEL_POL_SHIFT				0
#define TEGRA186_DSPK_CTRL_LRSEL_POLARITY_MASK	(0x1 << LRSEL_POL_SHIFT)
#define TEGRA186_DSPK_RX_FIFO_DEPTH		64

#define DSPK_OSR_FACTOR				32

/* DSPK interface clock ratio */
#define DSPK_CLK_RATIO				4

enum tegra_dspk_osr {
	DSPK_OSR_32,
	DSPK_OSR_64,
	DSPK_OSR_128,
	DSPK_OSR_256,
};

enum tegra_dspk_ch_sel {
	DSPK_CH_SELECT_LEFT,
	DSPK_CH_SELECT_RIGHT,
	DSPK_CH_SELECT_STEREO,
};

enum tegra_dspk_lrsel {
	DSPK_LRSEL_LEFT,
	DSPK_LRSEL_RIGHT,
};

struct tegra186_dspk {
	unsigned int rx_fifo_th;
	unsigned int osr_val;
	unsigned int lrsel;
	unsigned int ch_sel;
	unsigned int mono_to_stereo;
	unsigned int stereo_to_mono;
	struct clk *clk_dspk;
	struct regmap *regmap;
};

#endif
