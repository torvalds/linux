// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2022 Advanced Micro Devices, Inc.
//
// Authors: Ajit Kumar Pandey <AjitKumar.Pandey@amd.com>
//          V sujith kumar Reddy <Vsujithkumar.Reddy@amd.com>
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
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include <asm/amd_node.h>

#include "amd.h"
#include "../mach-config.h"
#include "acp-mach.h"

#define DRV_NAME "acp_asoc_rembrandt"

#define MP1_C2PMSG_69 0x3B10A14
#define MP1_C2PMSG_85 0x3B10A54
#define MP1_C2PMSG_93 0x3B10A74

static struct snd_soc_dai_driver acp_rmb_dai[] = {
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
},
{
	.name = "acp-i2s-hs",
	.id = I2S_HS_INSTANCE,
	.playback = {
		.stream_name = "I2S HS Playback",
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 8,
		.rate_min = 8000,
		.rate_max = 96000,
	},
	.capture = {
		.stream_name = "I2S HS Capture",
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
			   SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 8,
		.rate_min = 8000,
		.rate_max = 48000,
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

static int acp6x_master_clock_generate(struct device *dev)
{
	int data, rc;

	rc = amd_smn_write(0, MP1_C2PMSG_93, 0);
	if (rc)
		return rc;
	rc = amd_smn_write(0, MP1_C2PMSG_85, 0xC4);
	if (rc)
		return rc;
	rc = amd_smn_write(0, MP1_C2PMSG_69, 0x4);
	if (rc)
		return rc;

	return read_poll_timeout(smn_read_register, data, data > 0, DELAY_US,
				 ACP_TIMEOUT, false, MP1_C2PMSG_93);
}

static int rembrandt_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_chip_info *chip;
	u32 ret;

	chip = dev_get_platdata(&pdev->dev);
	if (!chip || !chip->base) {
		dev_err(&pdev->dev, "ACP chip data is NULL\n");
		return -ENODEV;
	}

	if (chip->acp_rev != ACP_RMB_PCI_ID) {
		dev_err(&pdev->dev, "Un-supported ACP Revision %d\n", chip->acp_rev);
		return -ENODEV;
	}

	chip->dev = dev;
	chip->dai_driver = acp_rmb_dai;
	chip->num_dai = ARRAY_SIZE(acp_rmb_dai);

	if (chip->is_i2s_config && chip->rsrc->soc_mclk) {
		ret = acp6x_master_clock_generate(dev);
		if (ret)
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

static void rembrandt_audio_remove(struct platform_device *pdev)
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

static int rmb_pcm_resume(struct device *dev)
{
	struct acp_chip_info *chip = dev_get_drvdata(dev->parent);
	struct acp_stream *stream;
	struct snd_pcm_substream *substream;
	snd_pcm_uframes_t buf_in_frames;
	u64 buf_size;

	if (chip->is_i2s_config && chip->rsrc->soc_mclk)
		acp6x_master_clock_generate(dev);

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

static const struct dev_pm_ops rmb_dma_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(NULL, rmb_pcm_resume)
};

static struct platform_driver rembrandt_driver = {
	.probe = rembrandt_audio_probe,
	.remove = rembrandt_audio_remove,
	.driver = {
		.name = "acp_asoc_rembrandt",
		.pm = pm_ptr(&rmb_dma_pm_ops),
	},
};

module_platform_driver(rembrandt_driver);

MODULE_DESCRIPTION("AMD ACP Rembrandt Driver");
MODULE_IMPORT_NS("SND_SOC_ACP_COMMON");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:" DRV_NAME);
