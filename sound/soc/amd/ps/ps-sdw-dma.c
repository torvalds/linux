// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD ALSA SoC Pink Sardine SoundWire DMA Driver
 *
 * Copyright 2023 Advanced Micro Devices, Inc.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include "acp63.h"

#define DRV_NAME "amd_ps_sdw_dma"

static const struct snd_soc_component_driver acp63_sdw_component = {
	.name		= DRV_NAME,
};

static int acp63_sdw_platform_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct sdw_dma_dev_data *sdw_data;
	struct acp63_dev_data *acp_data;
	struct device *parent;
	int status;

	parent = pdev->dev.parent;
	acp_data = dev_get_drvdata(parent);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENODEV;
	}

	sdw_data = devm_kzalloc(&pdev->dev, sizeof(*sdw_data), GFP_KERNEL);
	if (!sdw_data)
		return -ENOMEM;

	sdw_data->acp_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!sdw_data->acp_base)
		return -ENOMEM;

	sdw_data->acp_lock = &acp_data->acp_lock;
	dev_set_drvdata(&pdev->dev, sdw_data);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp63_sdw_component,
						 NULL, 0);
	if (status)
		dev_err(&pdev->dev, "Fail to register sdw dma component\n");

	return status;
}

static struct platform_driver acp63_sdw_dma_driver = {
	.probe = acp63_sdw_platform_probe,
	.driver = {
		.name = "amd_ps_sdw_dma",
	},
};

module_platform_driver(acp63_sdw_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP6.3 PS SDW DMA Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRV_NAME);
