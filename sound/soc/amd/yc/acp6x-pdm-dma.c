// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD ALSA SoC Yellow Carp PDM Driver
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "acp6x.h"

#define DRV_NAME "acp_yc_pdm_dma"

static struct snd_soc_dai_driver acp6x_pdm_dai_driver = {
	.capture = {
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 48000,
		.rate_max = 48000,
	},
};

static const struct snd_soc_component_driver acp6x_pdm_component = {
	.name		= DRV_NAME,
};

static int acp6x_pdm_audio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct pdm_dev_data *adata;
	int status;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENODEV;
	}

	adata = devm_kzalloc(&pdev->dev, sizeof(*adata), GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	adata->acp6x_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!adata->acp6x_base)
		return -ENOMEM;

	adata->capture_stream = NULL;

	dev_set_drvdata(&pdev->dev, adata);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp6x_pdm_component,
						 &acp6x_pdm_dai_driver, 1);
	if (status) {
		dev_err(&pdev->dev, "Fail to register acp pdm dai\n");

		return -ENODEV;
	}
	return 0;
}

static struct platform_driver acp6x_pdm_dma_driver = {
	.probe = acp6x_pdm_audio_probe,
	.driver = {
		.name = "acp_yc_pdm_dma",
	},
};

module_platform_driver(acp6x_pdm_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP6x YC PDM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
