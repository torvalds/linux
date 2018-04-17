/*
 *  skl-ssp-clk.h - Skylake ssp clock information and ipc structure
 *
 *  Copyright (C) 2017 Intel Corp
 *  Author: Jaikrishna Nemallapudi <jaikrishnax.nemallapudi@intel.com>
 *  Author: Subhransu S. Prusty <subhransu.s.prusty@intel.com>
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#ifndef SOUND_SOC_SKL_SSP_CLK_H
#define SOUND_SOC_SKL_SSP_CLK_H

#define SKL_MAX_SSP		6
/* xtal/cardinal/pll, parent of ssp clocks and mclk */
#define SKL_MAX_CLK_SRC		3
#define SKL_MAX_SSP_CLK_TYPES	3 /* mclk, sclk, sclkfs */

#define SKL_MAX_CLK_CNT		(SKL_MAX_SSP * SKL_MAX_SSP_CLK_TYPES)

/* Max number of configurations supported for each clock */
#define SKL_MAX_CLK_RATES	10

#define SKL_SCLK_OFS		SKL_MAX_SSP
#define SKL_SCLKFS_OFS		(SKL_SCLK_OFS + SKL_MAX_SSP)

enum skl_clk_type {
	SKL_MCLK,
	SKL_SCLK,
	SKL_SCLK_FS,
};

enum skl_clk_src_type {
	SKL_XTAL,
	SKL_CARDINAL,
	SKL_PLL,
};

struct skl_clk_parent_src {
	u8 clk_id;
	const char *name;
	unsigned long rate;
	const char *parent_name;
};

struct skl_clk_rate_cfg_table {
	unsigned long rate;
	void *config;
};

/*
 * rate for mclk will be in rates[0]. For sclk and sclkfs, rates[] store
 * all possible clocks ssp can generate for that platform.
 */
struct skl_ssp_clk {
	const char *name;
	const char *parent_name;
	struct skl_clk_rate_cfg_table rate_cfg[SKL_MAX_CLK_RATES];
};

struct skl_clk_pdata {
	struct skl_clk_parent_src *parent_clks;
	int num_clks;
	struct skl_ssp_clk *ssp_clks;
	void *pvt_data;
};

#endif /* SOUND_SOC_SKL_SSP_CLK_H */
