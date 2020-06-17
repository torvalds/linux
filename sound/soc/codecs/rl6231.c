// SPDX-License-Identifier: GPL-2.0-only
/*
 * rl6231.c - RL6231 class device shared support
 *
 * Copyright 2014 Realtek Semiconductor Corp.
 *
 * Author: Oder Chiou <oder_chiou@realtek.com>
 */

#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/gcd.h>
#include "rl6231.h"

/**
 * rl6231_get_pre_div - Return the value of pre divider.
 *
 * @map: map for setting.
 * @reg: register.
 * @sft: shift.
 *
 * Return the value of pre divider from given register value.
 * Return negative error code for unexpected register value.
 */
int rl6231_get_pre_div(struct regmap *map, unsigned int reg, int sft)
{
	int pd, val;

	regmap_read(map, reg, &val);

	val = (val >> sft) & 0x7;

	switch (val) {
	case 0:
	case 1:
	case 2:
	case 3:
		pd = val + 1;
		break;
	case 4:
		pd = 6;
		break;
	case 5:
		pd = 8;
		break;
	case 6:
		pd = 12;
		break;
	case 7:
		pd = 16;
		break;
	default:
		pd = -EINVAL;
		break;
	}

	return pd;
}
EXPORT_SYMBOL_GPL(rl6231_get_pre_div);

/**
 * rl6231_calc_dmic_clk - Calculate the frequency divider parameter of dmic.
 *
 * @rate: base clock rate.
 *
 * Choose divider parameter that gives the highest possible DMIC frequency in
 * 1MHz - 3MHz range.
 */
int rl6231_calc_dmic_clk(int rate)
{
	static const int div[] = {2, 3, 4, 6, 8, 12};
	int i;

	if (rate < 1000000 * div[0]) {
		pr_warn("Base clock rate %d is too low\n", rate);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(div); i++) {
		if ((div[i] % 3) == 0)
			continue;
		/* find divider that gives DMIC frequency below 1.536MHz */
		if (1536000 * div[i] >= rate)
			return i;
	}

	pr_warn("Base clock rate %d is too high\n", rate);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(rl6231_calc_dmic_clk);

struct pll_calc_map {
	unsigned int pll_in;
	unsigned int pll_out;
	int k;
	int n;
	int m;
	bool m_bp;
	bool k_bp;
};

static const struct pll_calc_map pll_preset_table[] = {
	{19200000,  4096000,  23, 14, 1, false, false},
	{19200000,  24576000,  3, 30, 3, false, false},
	{3840000,   24576000,  3, 30, 0, true, false},
};

static unsigned int find_best_div(unsigned int in,
	unsigned int max, unsigned int div)
{
	unsigned int d;

	if (in <= max)
		return 1;

	d = in / max;
	if (in % max)
		d++;

	while (div % d != 0)
		d++;


	return d;
}

/**
 * rl6231_pll_calc - Calcualte PLL M/N/K code.
 * @freq_in: external clock provided to codec.
 * @freq_out: target clock which codec works on.
 * @pll_code: Pointer to structure with M, N, K, m_bypass and k_bypass flag.
 *
 * Calcualte M/N/K code to configure PLL for codec.
 *
 * Returns 0 for success or negative error code.
 */
int rl6231_pll_calc(const unsigned int freq_in,
	const unsigned int freq_out, struct rl6231_pll_code *pll_code)
{
	int max_n = RL6231_PLL_N_MAX, max_m = RL6231_PLL_M_MAX;
	int i, k, n_t;
	int k_t, min_k, max_k, n = 0, m = 0, m_t = 0;
	unsigned int red, pll_out, in_t, out_t, div, div_t;
	unsigned int red_t = abs(freq_out - freq_in);
	unsigned int f_in, f_out, f_max;
	bool m_bypass = false, k_bypass = false;

	if (RL6231_PLL_INP_MAX < freq_in || RL6231_PLL_INP_MIN > freq_in)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(pll_preset_table); i++) {
		if (freq_in == pll_preset_table[i].pll_in &&
			freq_out == pll_preset_table[i].pll_out) {
			k = pll_preset_table[i].k;
			m = pll_preset_table[i].m;
			n = pll_preset_table[i].n;
			m_bypass = pll_preset_table[i].m_bp;
			k_bypass = pll_preset_table[i].k_bp;
			pr_debug("Use preset PLL parameter table\n");
			goto code_find;
		}
	}

	min_k = 80000000 / freq_out - 2;
	max_k = 150000000 / freq_out - 2;
	if (max_k > RL6231_PLL_K_MAX)
		max_k = RL6231_PLL_K_MAX;
	if (min_k > RL6231_PLL_K_MAX)
		min_k = max_k = RL6231_PLL_K_MAX;
	div_t = gcd(freq_in, freq_out);
	f_max = 0xffffffff / RL6231_PLL_N_MAX;
	div = find_best_div(freq_in, f_max, div_t);
	f_in = freq_in / div;
	f_out = freq_out / div;
	k = min_k;
	if (min_k < -1)
		min_k = -1;
	for (k_t = min_k; k_t <= max_k; k_t++) {
		for (n_t = 0; n_t <= max_n; n_t++) {
			in_t = f_in * (n_t + 2);
			pll_out = f_out * (k_t + 2);
			if (in_t == pll_out) {
				m_bypass = true;
				n = n_t;
				k = k_t;
				goto code_find;
			}
			out_t = in_t / (k_t + 2);
			red = abs(f_out - out_t);
			if (red < red_t) {
				m_bypass = true;
				n = n_t;
				m = 0;
				k = k_t;
				if (red == 0)
					goto code_find;
				red_t = red;
			}
			for (m_t = 0; m_t <= max_m; m_t++) {
				out_t = in_t / ((m_t + 2) * (k_t + 2));
				red = abs(f_out - out_t);
				if (red < red_t) {
					m_bypass = false;
					n = n_t;
					m = m_t;
					k = k_t;
					if (red == 0)
						goto code_find;
					red_t = red;
				}
			}
		}
	}
	pr_debug("Only get approximation about PLL\n");

code_find:
	if (k == -1) {
		k_bypass = true;
		k = 0;
	}

	pll_code->m_bp = m_bypass;
	pll_code->k_bp = k_bypass;
	pll_code->m_code = m;
	pll_code->n_code = n;
	pll_code->k_code = k;
	return 0;
}
EXPORT_SYMBOL_GPL(rl6231_pll_calc);

int rl6231_get_clk_info(int sclk, int rate)
{
	int i;
	static const int pd[] = {1, 2, 3, 4, 6, 8, 12, 16};

	if (sclk <= 0 || rate <= 0)
		return -EINVAL;

	rate = rate << 8;
	for (i = 0; i < ARRAY_SIZE(pd); i++)
		if (sclk == rate * pd[i])
			return i;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(rl6231_get_clk_info);

MODULE_DESCRIPTION("RL6231 class device shared support");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
