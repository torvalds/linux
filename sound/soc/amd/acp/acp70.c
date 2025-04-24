// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Advanced Micro Devices, Inc.
//
// Authors: Syed Saba kareem <syed.sabakareem@amd.com>
/*
 * Hardware interface for ACP7.0 block
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include "amd.h"
#include "acp-mach.h"

#include <asm/amd_node.h>

#define DRV_NAME "acp_asoc_acp70"

#define CLK7_CLK0_DFS_CNTL_N1		0X0006C1A4
#define CLK0_DIVIDER			0X19

static struct snd_soc_dai_driver acp70_dai[] = {
{
	.name = "acp-i2s-sp",
	.id = I2S_SP_INSTANCE,
	.playback = {
		.stream_name = "I2S SP Playback",
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 192000,
	},
	.capture = {
		.stream_name = "I2S SP Capture",
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 192000,
	},
	.ops = &asoc_acp_cpu_dai_ops,
},
{
	.name = "acp-i2s-bt",
	.id = I2S_BT_INSTANCE,
	.playback = {
		.stream_name = "I2S BT Playback",
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 192000,
	},
	.capture = {
		.stream_name = "I2S BT Capture",
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 192000,
	},
	.ops = &asoc_acp_cpu_dai_ops,
},
{
	.name = "acp-i2s-hs",
	.id = I2S_HS_INSTANCE,
	.playback = {
		.stream_name = "I2S HS Playback",
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 192000,
	},
	.capture = {
		.stream_name = "I2S HS Capture",
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 192000,
	},
	.ops = &asoc_acp_cpu_dai_ops,
},
{
	.name = "acp-pdm-dmic",
	.id = DMIC_INSTANCE,
	.capture = {
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 8000,
		.rate_max = 48000,
	},
	.ops = &acp_dmic_dai_ops,
},
};

static int acp_acp70_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_chip_info *chip;
	int ret;

	chip = dev_get_platdata(&pdev->dev);
	if (!chip || !chip->base) {
		dev_err(&pdev->dev, "ACP chip data is NULL\n");
		return -ENODEV;
	}

	switch (chip->acp_rev) {
	case ACP70_PCI_ID:
	case ACP71_PCI_ID:
		break;
	default:
		dev_err(&pdev->dev, "Un-supported ACP Revision %d\n", chip->acp_rev);
		return -ENODEV;
	}

	chip->dev = dev;
	chip->dai_driver = acp70_dai;
	chip->num_dai = ARRAY_SIZE(acp70_dai);

	/* Set clk7 DFS clock divider register value to get mclk as 196.608MHz*/
	ret = amd_smn_write(0, CLK7_CLK0_DFS_CNTL_N1, CLK0_DIVIDER);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set I2S master clock as 196.608MHz\n");
		return ret;
	}
	ret = acp_hw_en_interrupts(chip);
	if (ret) {
		dev_err(dev, "ACP en-interrupts failed\n");
		return ret;
	}
	acp_platform_register(dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, ACP_SUSPEND_DELAY_MS);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	return 0;
}

static void acp_acp70_audio_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_chip_info *chip = dev_get_platdata(dev);
	int ret;

	ret = acp_hw_dis_interrupts(chip);
	if (ret)
		dev_err(dev, "ACP dis-interrupts failed\n");

	acp_platform_unregister(dev);
	pm_runtime_disable(&pdev->dev);
}

static int acp70_pcm_resume(struct device *dev)
{
	struct acp_chip_info *chip = dev_get_platdata(dev);
	struct acp_stream *stream;
	struct snd_pcm_substream *substream;
	snd_pcm_uframes_t buf_in_frames;
	u64 buf_size;

	spin_lock(&chip->acp_lock);
	list_for_each_entry(stream, &chip->stream_list, list) {
		substream = stream->substream;
		if (substream && substream->runtime) {
			buf_in_frames = (substream->runtime->buffer_size);
			buf_size = frames_to_bytes(substream->runtime, buf_in_frames);
			config_pte_for_stream(chip, stream);
			config_acp_dma(chip, stream, buf_size);
			if (stream->dai_id)
				restore_acp_i2s_params(substream, chip, stream);
			else
				restore_acp_pdm_params(substream, chip);
		}
	}
	spin_unlock(&chip->acp_lock);
	return 0;
}

static const struct dev_pm_ops acp70_dma_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(NULL, acp70_pcm_resume)
};

static struct platform_driver acp70_driver = {
	.probe = acp_acp70_audio_probe,
	.remove = acp_acp70_audio_remove,
	.driver = {
		.name = "acp_asoc_acp70",
		.pm = pm_ptr(&acp70_dma_pm_ops),
	},
};

module_platform_driver(acp70_driver);

MODULE_DESCRIPTION("AMD ACP ACP70 Driver");
MODULE_IMPORT_NS("SND_SOC_ACP_COMMON");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:" DRV_NAME);
