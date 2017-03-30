/*
 * Copyright (C) 2017 BayLibre, SAS
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>

#include "audio-core.h"

#define DRV_NAME "meson-gx-audio-core"

static const char * const acore_clock_names[] = { "aiu_top",
						  "aiu_glue",
						  "audin" };

static int meson_acore_init_clocks(struct device *dev)
{
	struct clk *clock;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(acore_clock_names); i++) {
		clock = devm_clk_get(dev, acore_clock_names[i]);
		if (IS_ERR(clock)) {
			if (PTR_ERR(clock) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s clock\n",
					acore_clock_names[i]);
			return PTR_ERR(clock);
		}

		ret = clk_prepare_enable(clock);
		if (ret) {
			dev_err(dev, "Failed to enable %s clock\n",
				acore_clock_names[i]);
			return ret;
		}

		ret = devm_add_action_or_reset(dev,
				(void(*)(void *))clk_disable_unprepare,
				clock);
		if (ret)
			return ret;
	}

	return 0;
}

static const char * const acore_reset_names[] = { "aiu",
						  "audin" };

static int meson_acore_init_resets(struct device *dev)
{
	struct reset_control *reset;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(acore_reset_names); i++) {
		reset = devm_reset_control_get_exclusive(dev,
							 acore_reset_names[i]);
		if (IS_ERR(reset)) {
			if (PTR_ERR(reset) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s reset\n",
					acore_reset_names[i]);
			return PTR_ERR(reset);
		}

		ret = reset_control_reset(reset);
		if (ret) {
			dev_err(dev, "Failed to pulse %s reset\n",
				acore_reset_names[i]);
			return ret;
		}
	}

	return 0;
}

static const struct regmap_config meson_acore_regmap_config = {
	.reg_bits       = 32,
	.val_bits       = 32,
	.reg_stride     = 4,
};

static const struct mfd_cell meson_acore_devs[] = {
	{
		.name = "meson-aiu-i2s",
		.of_compatible = "amlogic,meson-aiu-i2s",
	},
	{
		.name = "meson-aiu-spdif",
		.of_compatible = "amlogic,meson-aiu-spdif",
	},
};

static int meson_acore_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_audio_core_data *data;
	struct resource *res;
	void __iomem *regs;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	platform_set_drvdata(pdev, data);

	ret = meson_acore_init_clocks(dev);
	if (ret)
		return ret;

	ret = meson_acore_init_resets(dev);
	if (ret)
		return ret;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "aiu");
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	data->aiu = devm_regmap_init_mmio(dev, regs,
					  &meson_acore_regmap_config);
	if (IS_ERR(data->aiu)) {
		dev_err(dev, "Couldn't create the AIU regmap\n");
		return PTR_ERR(data->aiu);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "audin");
	regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	data->audin = devm_regmap_init_mmio(dev, regs,
					    &meson_acore_regmap_config);
	if (IS_ERR(data->audin)) {
		dev_err(dev, "Couldn't create the AUDIN regmap\n");
		return PTR_ERR(data->audin);
	}

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, meson_acore_devs,
				    ARRAY_SIZE(meson_acore_devs), NULL, 0,
				    NULL);
}

static const struct of_device_id meson_acore_of_match[] = {
	{ .compatible = "amlogic,meson-gx-audio-core", },
	{}
};
MODULE_DEVICE_TABLE(of, meson_acore_of_match);

static struct platform_driver meson_acore_pdrv = {
	.probe = meson_acore_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = meson_acore_of_match,
	},
};
module_platform_driver(meson_acore_pdrv);

MODULE_DESCRIPTION("Meson Audio Core Driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
