/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * tegra_asoc_utils.h - Definitions for Tegra DAS driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010,2012 - NVIDIA, Inc.
 */

#ifndef __TEGRA_ASOC_UTILS_H__
#define __TEGRA_ASOC_UTILS_H__

struct clk;
struct device;

enum tegra_asoc_utils_soc {
	TEGRA_ASOC_UTILS_SOC_TEGRA20,
	TEGRA_ASOC_UTILS_SOC_TEGRA30,
	TEGRA_ASOC_UTILS_SOC_TEGRA114,
	TEGRA_ASOC_UTILS_SOC_TEGRA124,
};

struct tegra_asoc_utils_data {
	struct device *dev;
	enum tegra_asoc_utils_soc soc;
	struct clk *clk_pll_a;
	struct clk *clk_pll_a_out0;
	struct clk *clk_cdev1;
	int set_baseclock;
	int set_mclk;
};

int tegra_asoc_utils_set_rate(struct tegra_asoc_utils_data *data, int srate,
			      int mclk);
int tegra_asoc_utils_set_ac97_rate(struct tegra_asoc_utils_data *data);
int tegra_asoc_utils_init(struct tegra_asoc_utils_data *data,
			  struct device *dev);

#endif
