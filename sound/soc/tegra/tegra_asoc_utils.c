// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra_asoc_utils.c - Harmony machine ASoC driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010,2012 - NVIDIA, Inc.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>

#include "tegra_asoc_utils.h"

int tegra_asoc_utils_set_rate(struct tegra_asoc_utils_data *data, int srate,
			      int mclk)
{
	int new_baseclock;
	bool clk_change;
	int err;

	switch (srate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		if (data->soc == TEGRA_ASOC_UTILS_SOC_TEGRA20)
			new_baseclock = 56448000;
		else if (data->soc == TEGRA_ASOC_UTILS_SOC_TEGRA30)
			new_baseclock = 564480000;
		else
			new_baseclock = 282240000;
		break;
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		if (data->soc == TEGRA_ASOC_UTILS_SOC_TEGRA20)
			new_baseclock = 73728000;
		else if (data->soc == TEGRA_ASOC_UTILS_SOC_TEGRA30)
			new_baseclock = 552960000;
		else
			new_baseclock = 368640000;
		break;
	default:
		return -EINVAL;
	}

	clk_change = ((new_baseclock != data->set_baseclock) ||
			(mclk != data->set_mclk));
	if (!clk_change)
		return 0;

	data->set_baseclock = 0;
	data->set_mclk = 0;

	clk_disable_unprepare(data->clk_cdev1);

	err = clk_set_rate(data->clk_pll_a, new_baseclock);
	if (err) {
		dev_err(data->dev, "Can't set pll_a rate: %d\n", err);
		return err;
	}

	err = clk_set_rate(data->clk_pll_a_out0, mclk);
	if (err) {
		dev_err(data->dev, "Can't set pll_a_out0 rate: %d\n", err);
		return err;
	}

	/* Don't set cdev1/extern1 rate; it's locked to pll_a_out0 */

	err = clk_prepare_enable(data->clk_cdev1);
	if (err) {
		dev_err(data->dev, "Can't enable cdev1: %d\n", err);
		return err;
	}

	data->set_baseclock = new_baseclock;
	data->set_mclk = mclk;

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_asoc_utils_set_rate);

int tegra_asoc_utils_set_ac97_rate(struct tegra_asoc_utils_data *data)
{
	const int pll_rate = 73728000;
	const int ac97_rate = 24576000;
	int err;

	clk_disable_unprepare(data->clk_cdev1);

	/*
	 * AC97 rate is fixed at 24.576MHz and is used for both the host
	 * controller and the external codec
	 */
	err = clk_set_rate(data->clk_pll_a, pll_rate);
	if (err) {
		dev_err(data->dev, "Can't set pll_a rate: %d\n", err);
		return err;
	}

	err = clk_set_rate(data->clk_pll_a_out0, ac97_rate);
	if (err) {
		dev_err(data->dev, "Can't set pll_a_out0 rate: %d\n", err);
		return err;
	}

	/* Don't set cdev1/extern1 rate; it's locked to pll_a_out0 */

	err = clk_prepare_enable(data->clk_cdev1);
	if (err) {
		dev_err(data->dev, "Can't enable cdev1: %d\n", err);
		return err;
	}

	data->set_baseclock = pll_rate;
	data->set_mclk = ac97_rate;

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_asoc_utils_set_ac97_rate);

int tegra_asoc_utils_init(struct tegra_asoc_utils_data *data,
			  struct device *dev)
{
	struct clk *clk_out_1, *clk_extern1;
	int ret;

	data->dev = dev;

	if (of_machine_is_compatible("nvidia,tegra20"))
		data->soc = TEGRA_ASOC_UTILS_SOC_TEGRA20;
	else if (of_machine_is_compatible("nvidia,tegra30"))
		data->soc = TEGRA_ASOC_UTILS_SOC_TEGRA30;
	else if (of_machine_is_compatible("nvidia,tegra114"))
		data->soc = TEGRA_ASOC_UTILS_SOC_TEGRA114;
	else if (of_machine_is_compatible("nvidia,tegra124"))
		data->soc = TEGRA_ASOC_UTILS_SOC_TEGRA124;
	else {
		dev_err(data->dev, "SoC unknown to Tegra ASoC utils\n");
		return -EINVAL;
	}

	data->clk_pll_a = devm_clk_get(dev, "pll_a");
	if (IS_ERR(data->clk_pll_a)) {
		dev_err(data->dev, "Can't retrieve clk pll_a\n");
		return PTR_ERR(data->clk_pll_a);
	}

	data->clk_pll_a_out0 = devm_clk_get(dev, "pll_a_out0");
	if (IS_ERR(data->clk_pll_a_out0)) {
		dev_err(data->dev, "Can't retrieve clk pll_a_out0\n");
		return PTR_ERR(data->clk_pll_a_out0);
	}

	data->clk_cdev1 = devm_clk_get(dev, "mclk");
	if (IS_ERR(data->clk_cdev1)) {
		dev_err(data->dev, "Can't retrieve clk cdev1\n");
		return PTR_ERR(data->clk_cdev1);
	}

	/*
	 * If clock parents are not set in DT, configure here to use clk_out_1
	 * as mclk and extern1 as parent for Tegra30 and higher.
	 */
	if (!of_find_property(dev->of_node, "assigned-clock-parents", NULL) &&
	    data->soc > TEGRA_ASOC_UTILS_SOC_TEGRA20) {
		dev_warn(data->dev,
			 "Configuring clocks for a legacy device-tree\n");
		dev_warn(data->dev,
			 "Please update DT to use assigned-clock-parents\n");
		clk_extern1 = devm_clk_get(dev, "extern1");
		if (IS_ERR(clk_extern1)) {
			dev_err(data->dev, "Can't retrieve clk extern1\n");
			return PTR_ERR(clk_extern1);
		}

		ret = clk_set_parent(clk_extern1, data->clk_pll_a_out0);
		if (ret < 0) {
			dev_err(data->dev,
				"Set parent failed for clk extern1\n");
			return ret;
		}

		clk_out_1 = devm_clk_get(dev, "pmc_clk_out_1");
		if (IS_ERR(clk_out_1)) {
			dev_err(data->dev, "Can't retrieve pmc_clk_out_1\n");
			return PTR_ERR(clk_out_1);
		}

		ret = clk_set_parent(clk_out_1, clk_extern1);
		if (ret < 0) {
			dev_err(data->dev,
				"Set parent failed for pmc_clk_out_1\n");
			return ret;
		}

		data->clk_cdev1 = clk_out_1;
	}

	/*
	 * FIXME: There is some unknown dependency between audio mclk disable
	 * and suspend-resume functionality on Tegra30, although audio mclk is
	 * only needed for audio.
	 */
	ret = clk_prepare_enable(data->clk_cdev1);
	if (ret) {
		dev_err(data->dev, "Can't enable cdev1: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(tegra_asoc_utils_init);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra ASoC utility code");
MODULE_LICENSE("GPL");
