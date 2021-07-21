// SPDX-License-Identifier: GPL-2.0+
//
// AMD ALSA SoC PCM Driver
//
// Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "acp5x.h"

#define DRV_NAME "acp5x_i2s_dma"

static const struct snd_soc_component_driver acp5x_i2s_component = {
	.name		= DRV_NAME,
};

static int acp5x_audio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct i2s_dev_data *adata;
	int status;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "platform_data not retrieved\n");
		return -ENODEV;
	}
	irqflags = *((unsigned int *)(pdev->dev.platform_data));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENODEV;
	}

	adata = devm_kzalloc(&pdev->dev, sizeof(*adata), GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	adata->acp5x_base = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (!adata->acp5x_base)
		return -ENOMEM;
	dev_set_drvdata(&pdev->dev, adata);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp5x_i2s_component,
						 NULL, 0);
	if (status)
		dev_err(&pdev->dev, "Fail to register acp i2s component\n");

	return status;
}

static struct platform_driver acp5x_dma_driver = {
	.probe = acp5x_audio_probe,
	.driver = {
		.name = "acp5x_i2s_dma",
	},
};

module_platform_driver(acp5x_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP 5.x PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
