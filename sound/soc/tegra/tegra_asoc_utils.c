/*
 * tegra_asoc_utils.c - Harmony machine ASoC driver
 *
 * Author: Stephen Warren <swarren@nvidia.com>
 * Copyright (C) 2010 - NVIDIA, Inc.
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
#include <linux/err.h>
#include <linux/kernel.h>

#include "tegra_asoc_utils.h"

#define PREFIX "ASoC Tegra: "

static struct clk *clk_pll_a;
static struct clk *clk_pll_a_out0;
static struct clk *clk_cdev1;

static int set_baseclock, set_mclk;

int tegra_asoc_utils_set_rate(int srate, int mclk, int *mclk_change)
{
	int new_baseclock;
	int err;

	switch (srate) {
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		new_baseclock = 56448000;
		break;
	case 8000:
	case 16000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		new_baseclock = 73728000;
		break;
	default:
		return -EINVAL;
	}

	*mclk_change = ((new_baseclock != set_baseclock) ||
			(mclk != set_mclk));
	if (!*mclk_change)
	    return 0;

	set_baseclock = 0;
	set_mclk = 0;

	clk_disable(clk_cdev1);
	clk_disable(clk_pll_a_out0);
	clk_disable(clk_pll_a);

	err = clk_set_rate(clk_pll_a, new_baseclock);
	if (err) {
		pr_err(PREFIX "Can't set pll_a rate: %d\n", err);
		return err;
	}

	err = clk_set_rate(clk_pll_a_out0, mclk);
	if (err) {
		pr_err(PREFIX "Can't set pll_a_out0 rate: %d\n", err);
		return err;
	}

	/* Don't set cdev1 rate; its locked to pll_a_out0 */

	err = clk_enable(clk_pll_a);
	if (err) {
		pr_err(PREFIX "Can't enable pll_a: %d\n", err);
		return err;
	}

	err = clk_enable(clk_pll_a_out0);
	if (err) {
		pr_err(PREFIX "Can't enable pll_a_out0: %d\n", err);
		return err;
	}

	err = clk_enable(clk_cdev1);
	if (err) {
		pr_err(PREFIX "Can't enable cdev1: %d\n", err);
		return err;
	}

	set_baseclock = new_baseclock;
	set_mclk = mclk;

	return 0;
}

int tegra_asoc_utils_init(void)
{
	int ret;

	clk_pll_a = clk_get_sys(NULL, "pll_a");
	if (IS_ERR_OR_NULL(clk_pll_a)) {
		pr_err(PREFIX "Can't retrieve clk pll_a\n");
		ret = PTR_ERR(clk_pll_a);
		goto err;
	}

	clk_pll_a_out0 = clk_get_sys(NULL, "pll_a_out0");
	if (IS_ERR_OR_NULL(clk_pll_a_out0)) {
		pr_err(PREFIX "Can't retrieve clk pll_a_out0\n");
		ret = PTR_ERR(clk_pll_a_out0);
		goto err;
	}

	clk_cdev1 = clk_get_sys(NULL, "cdev1");
	if (IS_ERR_OR_NULL(clk_cdev1)) {
		pr_err(PREFIX "Can't retrieve clk cdev1\n");
		ret = PTR_ERR(clk_cdev1);
		goto err;
	}

	return 0;

err:
	if (!IS_ERR_OR_NULL(clk_cdev1))
		clk_put(clk_cdev1);
	if (!IS_ERR_OR_NULL(clk_pll_a_out0))
		clk_put(clk_pll_a_out0);
	if (!IS_ERR_OR_NULL(clk_pll_a))
		clk_put(clk_pll_a);
	return ret;
}

void tegra_asoc_utils_fini(void)
{
	clk_put(clk_cdev1);
	clk_put(clk_pll_a_out0);
	clk_put(clk_pll_a);
}

