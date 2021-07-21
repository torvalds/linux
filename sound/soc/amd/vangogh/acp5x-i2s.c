// SPDX-License-Identifier: GPL-2.0+
//
// AMD ALSA SoC PCM Driver
//
// Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/dma-mapping.h>

#include "acp5x.h"

#define DRV_NAME "acp5x_i2s_playcap"

static const struct snd_soc_component_driver acp5x_dai_component = {
	.name = "acp5x-i2s",
};

static struct snd_soc_dai_driver acp5x_i2s_dai = {
	.playback = {
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 8000,
		.rate_max = 96000,
	},
	.capture = {
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 8000,
		.rate_max = 96000,
	},
};

static int acp5x_dai_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct i2s_dev_data *adata;
	int ret;

	adata = devm_kzalloc(&pdev->dev, sizeof(struct i2s_dev_data),
			     GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENOMEM;
	}
	adata->acp5x_base = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (IS_ERR(adata->acp5x_base))
		return PTR_ERR(adata->acp5x_base);

	adata->master_mode = I2S_MASTER_MODE_ENABLE;
	dev_set_drvdata(&pdev->dev, adata);
	ret = devm_snd_soc_register_component(&pdev->dev,
					      &acp5x_dai_component,
					      &acp5x_i2s_dai, 1);
	if (ret)
		dev_err(&pdev->dev, "Fail to register acp i2s dai\n");
	return ret;
}

static struct platform_driver acp5x_dai_driver = {
	.probe = acp5x_dai_probe,
	.driver = {
		.name = "acp5x_i2s_playcap",
	},
};

module_platform_driver(acp5x_dai_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP5.x CPU DAI Driver");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_LICENSE("GPL v2");
