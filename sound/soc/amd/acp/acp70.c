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

#define DRV_NAME "acp_asoc_acp70"

#define CLK7_CLK0_DFS_CNTL_N1		0X0006C1A4
#define CLK0_DIVIDER			0X19

static struct acp_resource rsrc = {
	.offset = 0,
	.no_of_ctrls = 2,
	.irqp_used = 1,
	.soc_mclk = true,
	.irq_reg_offset = 0x1a00,
	.scratch_reg_offset = 0x10000,
	.sram_pte_offset = 0x03800000,
};

static struct snd_soc_acpi_mach snd_soc_acpi_amd_acp70_acp_machines[] = {
	{
		.id = "AMDI0029",
		.drv_name = "acp70-acp",
	},
	{},
};

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

static int acp70_i2s_master_clock_generate(struct acp_dev_data *adata)
{
	struct pci_dev *smn_dev;
	u32 device_id;

	if (adata->platform == ACP70)
		device_id = 0x1507;
	else if (adata->platform == ACP71)
		device_id = 0x1122;
	else
		return -ENODEV;

	smn_dev = pci_get_device(PCI_VENDOR_ID_AMD, device_id, NULL);

	if (!smn_dev)
		return -ENODEV;

	/* Set clk7 DFS clock divider register value to get mclk as 196.608MHz*/
	smn_write(smn_dev, CLK7_CLK0_DFS_CNTL_N1, CLK0_DIVIDER);

	return 0;
}

static int acp_acp70_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_chip_info *chip;
	struct acp_dev_data *adata;
	struct resource *res;
	int ret;

	chip = dev_get_platdata(&pdev->dev);
	if (!chip || !chip->base) {
		dev_err(&pdev->dev, "ACP chip data is NULL\n");
		return -ENODEV;
	}

	switch (chip->acp_rev) {
	case ACP70_DEV:
	case ACP71_DEV:
		break;
	default:
		dev_err(&pdev->dev, "Un-supported ACP Revision %d\n", chip->acp_rev);
		return -ENODEV;
	}

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
	adata->dai_driver = acp70_dai;
	adata->num_dai = ARRAY_SIZE(acp70_dai);
	adata->rsrc = &rsrc;
	adata->machines = snd_soc_acpi_amd_acp70_acp_machines;
	if (chip->acp_rev == ACP70_DEV)
		adata->platform = ACP70;
	else
		adata->platform = ACP71;

	adata->flag = chip->flag;
	acp_machine_select(adata);

	dev_set_drvdata(dev, adata);

	ret = acp70_i2s_master_clock_generate(adata);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set I2S master clock as 196.608MHz\n");
		return ret;
	}
	acp_enable_interrupts(adata);
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
	struct acp_dev_data *adata = dev_get_drvdata(dev);

	acp_disable_interrupts(adata);
	acp_platform_unregister(dev);
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused acp70_pcm_resume(struct device *dev)
{
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	struct acp_stream *stream;
	struct snd_pcm_substream *substream;
	snd_pcm_uframes_t buf_in_frames;
	u64 buf_size;

	spin_lock(&adata->acp_lock);
	list_for_each_entry(stream, &adata->stream_list, list) {
		if (stream) {
			substream = stream->substream;
			if (substream && substream->runtime) {
				buf_in_frames = (substream->runtime->buffer_size);
				buf_size = frames_to_bytes(substream->runtime, buf_in_frames);
				config_pte_for_stream(adata, stream);
				config_acp_dma(adata, stream, buf_size);
				if (stream->dai_id)
					restore_acp_i2s_params(substream, adata, stream);
				else
					restore_acp_pdm_params(substream, adata);
			}
		}
	}
	spin_unlock(&adata->acp_lock);
	return 0;
}

static const struct dev_pm_ops acp70_dma_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, acp70_pcm_resume)
};

static struct platform_driver acp70_driver = {
	.probe = acp_acp70_audio_probe,
	.remove = acp_acp70_audio_remove,
	.driver = {
		.name = "acp_asoc_acp70",
		.pm = &acp70_dma_pm_ops,
	},
};

module_platform_driver(acp70_driver);

MODULE_DESCRIPTION("AMD ACP ACP70 Driver");
MODULE_IMPORT_NS(SND_SOC_ACP_COMMON);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:" DRV_NAME);
