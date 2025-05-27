/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Register definitions for Cirrus Logic CS48L32
 *
 * Copyright (C) 2017-2018, 2020, 2022, 2025 Cirrus Logic, Inc. and
 *               Cirrus Logic International Semiconductor Ltd.
 */

#ifndef CS48L32_H
#define CS48L32_H

/* pll_id for snd_soc_component_set_pll() */
#define CS48L32_FLL1_REFCLK			1

/* source for snd_soc_component_set_pll() */
#define CS48L32_FLL_SRC_NONE			-1
#define CS48L32_FLL_SRC_MCLK1			0
#define CS48L32_FLL_SRC_PDMCLK			5
#define CS48L32_FLL_SRC_ASP1_BCLK		8
#define CS48L32_FLL_SRC_ASP2_BCLK		9
#define CS48L32_FLL_SRC_ASP1_FSYNC		12
#define CS48L32_FLL_SRC_ASP2_FSYNC		13

/* clk_id for snd_soc_component_set_sysclk() and snd_soc_dai_set_sysclk() */
#define CS48L32_CLK_SYSCLK_1			1
#define CS48L32_CLK_SYSCLK_2			2
#define CS48L32_CLK_SYSCLK_3			3
#define CS48L32_CLK_SYSCLK_4			4
#define CS48L32_CLK_DSPCLK			7
#define CS48L32_CLK_PDM_FLLCLK			13

/* source for snd_soc_component_set_sysclk() */
#define CS48L32_CLK_SRC_MCLK1			0x0
#define CS48L32_CLK_SRC_FLL1			0x4
#define CS48L32_CLK_SRC_ASP1_BCLK		0x8
#define CS48L32_CLK_SRC_ASP2_BCLK		0x9

struct cs48l32 {
	struct regmap *regmap;
	struct device *dev;
	struct gpio_desc *reset_gpio;
	struct clk *mclk1;
	struct regulator_bulk_data core_supplies[2];
	struct regulator *vdd_d;
	int irq;
};
#endif
