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

#include "amd.h"

#define DRV_NAME "acp_asoc_rembrandt"

#define ACP6X_PGFSM_CONTROL			0x1024
#define ACP6X_PGFSM_STATUS			0x1028

#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK	0x00010001

#define ACP_PGFSM_CNTL_POWER_ON_MASK		0x01
#define ACP_PGFSM_CNTL_POWER_OFF_MASK		0x00
#define ACP_PGFSM_STATUS_MASK			0x03
#define ACP_POWERED_ON				0x00
#define ACP_POWER_ON_IN_PROGRESS		0x01
#define ACP_POWERED_OFF				0x02
#define ACP_POWER_OFF_IN_PROGRESS		0x03

#define ACP_ERROR_MASK				0x20000000
#define ACP_EXT_INTR_STAT_CLEAR_MASK		0xFFFFFFFF


static int rmb_acp_init(void __iomem *base);
static int rmb_acp_deinit(void __iomem *base);

static struct acp_resource rsrc = {
	.offset = 0,
	.no_of_ctrls = 2,
	.irqp_used = 1,
	.soc_mclk = true,
	.irq_reg_offset = 0x1a00,
	.i2s_pin_cfg_offset = 0x1440,
	.i2s_mode = 0x0a,
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
	.probe = &asoc_acp_i2s_probe,
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

static int acp6x_power_on(void __iomem *base)
{
	u32 val;
	int timeout;

	val = readl(base + ACP6X_PGFSM_STATUS);

	if (val == ACP_POWERED_ON)
		return 0;

	if ((val & ACP_PGFSM_STATUS_MASK) !=
				ACP_POWER_ON_IN_PROGRESS)
		writel(ACP_PGFSM_CNTL_POWER_ON_MASK,
		       base + ACP6X_PGFSM_CONTROL);
	timeout = 0;
	while (++timeout < 500) {
		val = readl(base + ACP6X_PGFSM_STATUS);
		if (!val)
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static int acp6x_reset(void __iomem *base)
{
	u32 val;
	int timeout;

	writel(1, base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = readl(base + ACP_SOFT_RESET);
		if (val & ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK)
			break;
		cpu_relax();
	}
	writel(0, base + ACP_SOFT_RESET);
	timeout = 0;
	while (++timeout < 500) {
		val = readl(base + ACP_SOFT_RESET);
		if (!val)
			return 0;
		cpu_relax();
	}
	return -ETIMEDOUT;
}

static void acp6x_enable_interrupts(struct acp_dev_data *adata)
{
	struct acp_resource *rsrc = adata->rsrc;
	u32 ext_intr_ctrl;

	writel(0x01, ACP_EXTERNAL_INTR_ENB(adata));
	ext_intr_ctrl = readl(ACP_EXTERNAL_INTR_CNTL(adata, rsrc->irqp_used));
	ext_intr_ctrl |= ACP_ERROR_MASK;
	writel(ext_intr_ctrl, ACP_EXTERNAL_INTR_CNTL(adata, rsrc->irqp_used));
}

static void acp6x_disable_interrupts(struct acp_dev_data *adata)
{
	struct acp_resource *rsrc = adata->rsrc;

	writel(ACP_EXT_INTR_STAT_CLEAR_MASK,
	       ACP_EXTERNAL_INTR_STAT(adata, rsrc->irqp_used));
	writel(0x00, ACP_EXTERNAL_INTR_ENB(adata));
}

static int rmb_acp_init(void __iomem *base)
{
	int ret;

	/* power on */
	ret = acp6x_power_on(base);
	if (ret) {
		pr_err("ACP power on failed\n");
		return ret;
	}
	writel(0x01, base + ACP_CONTROL);

	/* Reset */
	ret = acp6x_reset(base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}

	return 0;
}

static int rmb_acp_deinit(void __iomem *base)
{
	int ret = 0;

	/* Reset */
	ret = acp6x_reset(base);
	if (ret) {
		pr_err("ACP reset failed\n");
		return ret;
	}

	writel(0x00, base + ACP_CONTROL);
	return 0;
}

static int rembrandt_audio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_chip_info *chip;
	struct acp_dev_data *adata;
	struct resource *res;

	chip = dev_get_platdata(&pdev->dev);
	if (!chip || !chip->base) {
		dev_err(&pdev->dev, "ACP chip data is NULL\n");
		return -ENODEV;
	}

	if (chip->acp_rev != ACP6X_DEV) {
		dev_err(&pdev->dev, "Un-supported ACP Revision %d\n", chip->acp_rev);
		return -ENODEV;
	}

	rmb_acp_init(chip->base);

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

	adata->machines = snd_soc_acpi_amd_rmb_acp_machines;
	acp_machine_select(adata);

	dev_set_drvdata(dev, adata);
	acp6x_enable_interrupts(adata);
	acp_platform_register(dev);

	return 0;
}

static void rembrandt_audio_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct acp_dev_data *adata = dev_get_drvdata(dev);
	struct acp_chip_info *chip = dev_get_platdata(dev);

	rmb_acp_deinit(chip->base);

	acp6x_disable_interrupts(adata);
	acp_platform_unregister(dev);
}

static struct platform_driver rembrandt_driver = {
	.probe = rembrandt_audio_probe,
	.remove_new = rembrandt_audio_remove,
	.driver = {
		.name = "acp_asoc_rembrandt",
	},
};

module_platform_driver(rembrandt_driver);

MODULE_DESCRIPTION("AMD ACP Rembrandt Driver");
MODULE_IMPORT_NS(SND_SOC_ACP_COMMON);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:" DRV_NAME);
