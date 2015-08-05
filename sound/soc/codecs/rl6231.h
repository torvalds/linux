/*
 * rl6231.h - RL6231 class device shared support
 *
 * Copyright 2014 Realtek Semiconductor Corp.
 *
 * Author: Oder Chiou <oder_chiou@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __RL6231_H__
#define __RL6231_H__

#define RL6231_PLL_INP_MAX	40000000
#define RL6231_PLL_INP_MIN	256000
#define RL6231_PLL_N_MAX	0x1ff
#define RL6231_PLL_K_MAX	0x1f
#define RL6231_PLL_M_MAX	0xf

struct rl6231_pll_code {
	bool m_bp; /* Indicates bypass m code or not. */
	int m_code;
	int n_code;
	int k_code;
};

int rl6231_calc_dmic_clk(int rate);
int rl6231_pll_calc(const unsigned int freq_in,
	const unsigned int freq_out, struct rl6231_pll_code *pll_code);
int rl6231_get_clk_info(int sclk, int rate);
int rl6231_get_pre_div(struct regmap *map, unsigned int reg, int sft);

#endif /* __RL6231_H__ */
