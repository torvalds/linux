// SPDX-License-Identifier: GPL-2.0+
/*
 * AMD ALSA SoC Pink Sardine PDM Driver
 *
 * Copyright 2022 Advanced Micro Devices, Inc.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "acp62.h"

#define DRV_NAME "acp_ps_pdm_dma"

static struct snd_soc_dai_driver acp62_pdm_dai_driver = {
	.name = "acp_ps_pdm_dma.0",
	.capture = {
		.rates = SNDRV_PCM_RATE_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 48000,
		.rate_max = 48000,
	},
};

static const struct snd_soc_component_driver acp62_pdm_component = {
	.name		= DRV_NAME,
};

static int acp62_pdm_audio_probe(struct platform_device *pdev)
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

	adata->acp62_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!adata->acp62_base)
		return -ENOMEM;

	adata->capture_stream = NULL;

	dev_set_drvdata(&pdev->dev, adata);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp62_pdm_component,
						 &acp62_pdm_dai_driver, 1);
	if (status) {
		dev_err(&pdev->dev, "Fail to register acp pdm dai\n");

		return -ENODEV;
	}
	return 0;
}

static struct platform_driver acp62_pdm_dma_driver = {
	.probe = acp62_pdm_audio_probe,
	.driver = {
		.name = "acp_ps_pdm_dma",
	},
};

module_platform_driver(acp62_pdm_dma_driver);

MODULE_AUTHOR("Syed.SabaKareem@amd.com");
MODULE_DESCRIPTION("AMD PINK SARDINE PDM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
