// SPDX-License-Identifier: GPL-2.0
//
// Analog Devices ADAU7118 8 channel PDM-to-I2S/TDM Converter Standalone Hw
// driver
//
// Copyright 2019 Analog Devices Inc.

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>

#include "adau7118.h"

static int adau7118_probe_hw(struct platform_device *pdev)
{
	return adau7118_probe(&pdev->dev, NULL, true);
}

static const struct of_device_id adau7118_of_match[] = {
	{ .compatible = "adi,adau7118" },
	{}
};
MODULE_DEVICE_TABLE(of, adau7118_of_match);

static const struct platform_device_id adau7118_id[] = {
	{ .name	= "adau7118" },
	{ }
};
MODULE_DEVICE_TABLE(platform, adau7118_id);

static struct platform_driver adau7118_driver_hw = {
	.driver = {
		.name = "adau7118",
		.of_match_table = adau7118_of_match,
	},
	.probe = adau7118_probe_hw,
	.id_table = adau7118_id,
};
module_platform_driver(adau7118_driver_hw);

MODULE_AUTHOR("Nuno Sa <nuno.sa@analog.com>");
MODULE_DESCRIPTION("ADAU7118 8 channel PDM-to-I2S/TDM Converter driver for standalone hw mode");
MODULE_LICENSE("GPL");
