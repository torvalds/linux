/*
 * tegra_asoc_utils.c - Harmony machine ASoC driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010,2012 - NVIDIA, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
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
	clk_disable_unprepare(data->clk_pll_a_out0);
	clk_disable_unprepare(data->clk_pll_a);

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

	err = clk_prepare_enable(data->clk_pll_a);
	if (err) {
		dev_err(data->dev, "Can't enable pll_a: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(data->clk_pll_a_out0);
	if (err) {
		dev_err(data->dev, "Can't enable pll_a_out0: %d\n", err);
		return err;
	}

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
	clk_disable_unprepare(data->clk_pll_a_out0);
	clk_disable_unprepare(data->clk_pll_a);

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

	err = clk_prepare_enable(data->clk_pll_a);
	if (err) {
		dev_err(data->dev, "Can't enable pll_a: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(data->clk_pll_a_out0);
	if (err) {
		dev_err(data->dev, "Can't enable pll_a_out0: %d\n", err);
		return err;
	}

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
	int ret;

	data->dev = dev;

	if (of_machine_is_compatible("nvidia,tegra20"))
		data->soc = TEGRA_ASOC_UTILS_SOC_TEGRA20;
	else if (of_machine_is_compatible("nvidia,tegra30"))
		data->soc = TEGRA_ASOC_UTILS_SOC_TEGRA30;
	else if (of_machine_is_compatible("nvidia,tegra114"))
		data->soc = TEGRA_ASOC_UTILS_SOC_TEGRA114;
	else {
		dev_err(data->dev, "SoC unknown to Tegra ASoC utils\n");
		return -EINVAL;
	}

	data->clk_pll_a = clk_get(dev, "pll_a");
	if (IS_ERR(data->clk_pll_a)) {
		dev_err(data->dev, "Can't retrieve clk pll_a\n");
		ret = PTR_ERR(data->clk_pll_a);
		goto err;
	}

	data->clk_pll_a_out0 = clk_get(dev, "pll_a_out0");
	if (IS_ERR(data->clk_pll_a_out0)) {
		dev_err(data->dev, "Can't retrieve clk pll_a_out0\n");
		ret = PTR_ERR(data->clk_pll_a_out0);
		goto err_put_pll_a;
	}

	data->clk_cdev1 = clk_get(dev, "mclk");
	if (IS_ERR(data->clk_cdev1)) {
		dev_err(data->dev, "Can't retrieve clk cdev1\n");
		ret = PTR_ERR(data->clk_cdev1);
		goto err_put_pll_a_out0;
	}

	ret = tegra_asoc_utils_set_rate(data, 44100, 256 * 44100);
	if (ret)
		goto err_put_cdev1;

	return 0;

err_put_cdev1:
	clk_put(data->clk_cdev1);
err_put_pll_a_out0:
	clk_put(data->clk_pll_a_out0);
err_put_pll_a:
	clk_put(data->clk_pll_a);
err:
	return ret;
}
EXPORT_SYMBOL_GPL(tegra_asoc_utils_init);

void tegra_asoc_utils_fini(struct tegra_asoc_utils_data *data)
{
	clk_put(data->clk_cdev1);
	clk_put(data->clk_pll_a_out0);
	clk_put(data->clk_pll_a);
}
EXPORT_SYMBOL_GPL(tegra_asoc_utils_fini);

MODULE_AUTHOR("Stephen Warren <swarren@nvidia.com>");
MODULE_DESCRIPTION("Tegra ASoC utility code");
MODULE_LICENSE("GPL");
