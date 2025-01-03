// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license. When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2023 Advanced Micro Devices, Inc.
//
// Authors: Syed Saba kareem <syed.sabakareem@amd.com>
/*
 * Hardware interface for ACP6.3 block
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
#include "../mach-config.h"

#define DRV_NAME "acp_asoc_acp63"

#define CLK_PLL_PWR_REQ_N0		0X0006C2C0
#define CLK_SPLL_FIELD_2_N0		0X0006C114
#define CLK_PLL_REQ_N0			0X0006C0DC
#define CLK_DFSBYPASS_CONTR		0X0006C2C8
#define CLK_DFS_CNTL_N0			0X0006C1A4

#define PLL_AUTO_STOP_REQ		BIT(4)
#define PLL_AUTO_START_REQ		BIT(0)
#define PLL_FRANCE_EN			BIT(4)
#define EXIT_DPF_BYPASS_0		BIT(16)
#define EXIT_DPF_BYPASS_1		BIT(17)
#define CLK0_DIVIDER			0X30

union clk_pll_req_no {
	struct {
		u32 fb_mult_int : 9;
		u32 reserved : 3;
		u32 pll_spine_div : 4;
		u32 gb_mult_frac : 16;
	} bitfields, bits;
	u32 clk_pll_req_no_reg;
};

static struct acp_resource rsrc = {
	.offset = 0,
	.no_of_ctrls = 2,
	.irqp_used = 1,
	.soc_mclk = true,
	.irq_reg_offset = 0x1a00,
	.scratch_reg_offset = 0x12800,
	.sram_pte_offset = 0x03802800,
};

static struct snd_soc_acpi_mach snd_soc_acpi_amd_acp63_acp_machines[] = {
	{
		.id = "AMDI0052",
		.drv_name = "acp63-acp",
	},
	{},
};

static struct snd_soc_dai_driver acp63_dai[] = {
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

static int acp63_i2s_master_clock_generate(struct acp_dev_data *adata)
{
	u32 data;
	union clk_pll_req_no clk_pll;
	struct pci_dev *smn_dev;

	smn_dev = pci_get_device(PCI_VENDOR_ID_AMD, 0x14E8, NULL);
	if (!smn_dev)
		return -ENODEV;

	/* Clk5 pll register values to get mclk as 196.6MHz*/
	clk_pll.bits.fb_mult_int = 0x31;
	clk_pll.bits.pll_spine_div = 0;
	clk_pll.bits.gb_mult_frac = 0x26E9;

	data = smn_read(smn_dev, CLK_PLL_PWR_REQ_N0);
	smn_write(smn_dev, CLK_PLL_PWR_REQ_N0, data | PLL_AUTO_STOP_REQ);

	data = smn_read(smn_dev, CLK_SPLL_FIELD_2_N0);
	if (data & PLL_FRANCE_EN)
		smn_write(smn_dev, CLK_SPLL_FIELD_2_N0, data | PLL_FRANCE_EN);

	smn_write(smn_dev, CLK_PLL_REQ_N0, clk_pll.clk_pll_req_no_reg);

	data = smn_read(smn_dev, CLK_PLL_PWR_REQ_N0);
	smn_write(smn_dev, CLK_PLL_PWR_REQ_N0, data | PLL_AUTO_START_REQ);

	data = smn_read(smn_dev, CLK_DFSBYPASS_CONTR);
	smn_write(smn_dev, CLK_DFSBYPASS_CONTR, data | EXIT_DPF_BYPASS_0);
	smn_write(smn_dev, CLK_DFSBYPASS_CONTR, data | EXIT_DPF_BYPASS_1);

	smn_write(smn_dev, CLK_DFS_CNTL_N0, CLK0_DIVIDER);
	return 0;
}

static int acp63_audio_probe(struct platform_device *pdev)
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

	if (chip->acp_rev != ACP63_PCI_ID) {
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
	adata->dai_driver = acp63_dai;
	adata->num_dai = ARRAY_SIZE(acp63_dai);
	adata->rsrc = &rsrc;
	adata->acp_rev = chip->acp_rev;
	adata->flag = chip->flag;
	adata->is_i2s_config = chip->is_i2s_config;
	adata->machines = snd_soc_acpi_amd_acp63_acp_machines;
	acp_machine_select(adata);
	dev_set_drvdata(dev, adata);

	if (chip->is_i2s_config && rsrc.soc_mclk) {
		ret = acp63_i2s_master_clock_generate(adata);
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

static void acp63_audio_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);

	acp_disable_interrupts(adata);
	acp_platform_unregister(dev);
	pm_runtime_disable(&pdev->dev);
}

static int __maybe_unused acp63_pcm_resume(struct device *dev)
{
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	struct acp_stream *stream;
	struct snd_pcm_substream *substream;
	snd_pcm_uframes_t buf_in_frames;
	u64 buf_size;

	if (adata->is_i2s_config && adata->rsrc->soc_mclk)
		acp63_i2s_master_clock_generate(adata);

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

static const struct dev_pm_ops acp63_dma_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, acp63_pcm_resume)
};

static struct platform_driver acp63_driver = {
	.probe = acp63_audio_probe,
	.remove = acp63_audio_remove,
	.driver = {
		.name = "acp_asoc_acp63",
		.pm = &acp63_dma_pm_ops,
	},
};

module_platform_driver(acp63_driver);

MODULE_DESCRIPTION("AMD ACP acp63 Driver");
MODULE_IMPORT_NS(SND_SOC_ACP_COMMON);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:" DRV_NAME);
