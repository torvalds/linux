// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2021 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//

/*
 * Hardware interface for Renoir ACP block
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/dma-mapping.h>

#include "amd.h"

#define DRV_NAME "acp_asoc_renoir"

static struct snd_soc_dai_driver acp_renoir_dai[] = {
{
	.name = "acp-i2s-sp",
	.id = I2S_SP_INSTANCE,
	.playback = {
		.stream_name = "I2S SP Playback",
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 8,
		.rate_min = 8000,
		.rate_max = 96000,
	},
	.capture = {
		.stream_name = "I2S SP Capture",
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 8000,
		.rate_max = 48000,
	},
	.ops = &asoc_acp_cpu_dai_ops,
	.probe = &asoc_acp_i2s_probe,
},
{
	.name = "acp-i2s-bt",
	.id = I2S_BT_INSTANCE,
	.playback = {
		.stream_name = "I2S BT Playback",
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 8,
		.rate_min = 8000,
		.rate_max = 96000,
	},
	.capture = {
		.stream_name = "I2S BT Capture",
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 8000,
		.rate_max = 48000,
	},
	.ops = &asoc_acp_cpu_dai_ops,
	.probe = &asoc_acp_i2s_probe,
},
};

static int renoir_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_dev_data *adata;
	struct resource *res;

	adata = devm_kzalloc(dev, sizeof(struct acp_dev_data), GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "acp_mem");
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENODEV;
	}

	adata->acp_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!adata->acp_base)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "acp_dai_irq");
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_IRQ FAILED\n");
		return -ENODEV;
	}

	adata->i2s_irq = res->start;
	adata->dev = dev;
	adata->dai_driver = acp_renoir_dai;
	adata->num_dai = ARRAY_SIZE(acp_renoir_dai);

	adata->machines = snd_soc_acpi_amd_acp_machines;
	acp_machine_select(adata);

	dev_set_drvdata(dev, adata);
	acp_platform_register(dev);

	return 0;
}

static int renoir_audio_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	acp_platform_unregister(dev);
	return 0;
}

static struct platform_driver renoir_driver = {
	.probe = renoir_audio_probe,
	.remove = renoir_audio_remove,
	.driver = {
		.name = "acp_asoc_renoir",
	},
};

module_platform_driver(renoir_driver);

MODULE_DESCRIPTION("AMD ACP Renoir Driver");
MODULE_IMPORT_NS(SND_SOC_ACP_COMMON);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:" DRV_NAME);
