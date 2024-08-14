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

#include "amd.h"
#include "../mach-config.h"
#include "acp-mach.h"

#define DRV_NAME "acp_asoc_rembrandt"

#define MP1_C2PMSG_69 0x3B10A14
#define MP1_C2PMSG_85 0x3B10A54
#define MP1_C2PMSG_93 0x3B10A74
#define HOST_BRIDGE_ID 0x14B5

static struct acp_resource rsrc = {
	.offset = 0,
	.no_of_ctrls = 2,
	.irqp_used = 1,
	.soc_mclk = true,
	.irq_reg_offset = 0x1a00,
	.scratch_reg_offset = 0x12800,
	.sram_pte_offset = 0x03802800,
};

static struct snd_soc_acpi_codecs amp_rt1019 = {
	.num_codecs = 1,
	.codecs = {"10EC1019"}
};

static struct snd_soc_acpi_codecs amp_max = {
	.num_codecs = 1,
	.codecs = {"MX98360A"}
};

static struct snd_soc_acpi_mach snd_soc_acpi_amd_rmb_acp_machines[] = {
	{
		.id = "10508825",
		.drv_name = "rmb-nau8825-max",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &amp_max,
	},
	{
		.id = "AMDI0007",
		.drv_name = "rembrandt-acp",
	},
	{
		.id = "RTL5682",
		.drv_name = "rmb-rt5682s-rt1019",
		.machine_quirk = snd_soc_acpi_codec_list,
		.quirk_data = &amp_rt1019,
	},
	{},
};

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
	int data = 0;
	struct pci_dev *smn_dev;

	smn_dev = pci_get_device(PCI_VENDOR_ID_AMD, HOST_BRIDGE_ID, NULL);
	if (!smn_dev) {
		dev_err(dev, "Failed to get host bridge device\n");
		return -ENODEV;
	}

	smn_write(smn_dev, MP1_C2PMSG_93, 0);
	smn_write(smn_dev, MP1_C2PMSG_85, 0xC4);
	smn_write(smn_dev, MP1_C2PMSG_69, 0x4);
	read_poll_timeout(smn_read, data, data, DELAY_US,
			  ACP_TIMEOUT, false, smn_dev, MP1_C2PMSG_93);
	return 0;
}

static int rembrandt_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_chip_info *chip;
	struct acp_dev_data *adata;
	struct resource *res;
	u32 ret;

	chip = dev_get_platdata(&pdev->dev);
	if (!chip || !chip->base) {
		dev_err(&pdev->dev, "ACP chip data is NULL\n");
		return -ENODEV;
	}

	if (chip->acp_rev != ACP6X_DEV) {
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
	adata->dai_driver = acp_rmb_dai;
	adata->num_dai = ARRAY_SIZE(acp_rmb_dai);
	adata->rsrc = &rsrc;
	adata->platform = REMBRANDT;
	adata->flag = chip->flag;
	adata->is_i2s_config = chip->is_i2s_config;
	adata->machines = snd_soc_acpi_amd_rmb_acp_machines;
	acp_machine_select(adata);

	dev_set_drvdata(dev, adata);

	if (chip->is_i2s_config && rsrc.soc_mclk) {
		ret = acp6x_master_clock_generate(dev);
		if (ret)
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

static void rembrandt_audio_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);

	acp_disable_interrupts(adata);
	acp_platform_unregister(dev);
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused rmb_pcm_resume(struct device *dev)
{
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	struct acp_stream *stream;
	struct snd_pcm_substream *substream;
	snd_pcm_uframes_t buf_in_frames;
	u64 buf_size;

	if (adata->is_i2s_config && adata->rsrc->soc_mclk)
		acp6x_master_clock_generate(dev);

	spin_lock(&adata->acp_lock);
	list_for_each_entry(stream, &adata->stream_list, list) {
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
	spin_unlock(&adata->acp_lock);
	return 0;
}

static const struct dev_pm_ops rmb_dma_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, rmb_pcm_resume)
};

static struct platform_driver rembrandt_driver = {
	.probe = rembrandt_audio_probe,
	.remove_new = rembrandt_audio_remove,
	.driver = {
		.name = "acp_asoc_rembrandt",
		.pm = &rmb_dma_pm_ops,
	},
};

module_platform_driver(rembrandt_driver);

MODULE_DESCRIPTION("AMD ACP Rembrandt Driver");
MODULE_IMPORT_NS(SND_SOC_ACP_COMMON);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:" DRV_NAME);
