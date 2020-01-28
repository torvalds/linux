// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 BayLibre, SAS.
// Author: Jerome Brunet <jbrunet@baylibre.com>

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset.h>
/*
static const char * const aiu_bus_clk_names[] = { "top", "glue", "pclk", "aoclk", "mclk", "amclk", "clk_i2s",  };
*/
static const char * const aiu_bus_clk_names[] = { "top", "glue", "abuf", "i2s_spdif", "aoclk", "mclk_i958", "clk_i2s", "amclk", "aififo2", "mixer", "iface", "adc",  };

static int aiu_bus_enable_pclks(struct device *dev)
{
	struct clk *clock;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(aiu_bus_clk_names); i++) {
		clock = devm_clk_get(dev, aiu_bus_clk_names[i]);
		if (IS_ERR(clock)) {
			if (PTR_ERR(clock) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get %s clock\n",
					aiu_bus_clk_names[i]);
			return PTR_ERR(clock);
		}

		ret = clk_prepare_enable(clock);
		if (ret) {
			dev_err(dev, "Failed to enable %s clock\n",
				aiu_bus_clk_names[i]);
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

static const struct of_device_id aiu_bus_of_match[] = {
	{
		.compatible = "amlogic,aiu-bus",
		.data = NULL,
	}, {}
};
MODULE_DEVICE_TABLE(of, aiu_bus_of_match);

static int aiu_bus_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	/* Fire and forget bus pclks */
	ret = aiu_bus_enable_pclks(dev);
	if (ret)
		return ret;

	ret = device_reset(dev);
	if (ret) {
		dev_err(dev, "reset failed\n");
		return ret;
	}

	return devm_of_platform_populate(dev);
}

static struct platform_driver aiu_bus_pdrv = {
	.probe = aiu_bus_probe,
	.driver = {
		.name = "meson-aiu-bus",
		.of_match_table = aiu_bus_of_match,
	},
};
module_platform_driver(aiu_bus_pdrv);

MODULE_DESCRIPTION("Amlogic AIU bus driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL v2");
