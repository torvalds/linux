/*
 * Freescale ALSA SoC Digital Audio Interface (SAI) driver.
 *
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software, you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 2 of the License, or(at your
 * option) any later version.
 *
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "fsl_sai.h"

static inline u32 sai_readl(struct fsl_sai *sai,
		const void __iomem *addr)
{
	u32 val;

	val = __raw_readl(addr);

	if (likely(sai->big_endian_regs))
		val = be32_to_cpu(val);
	else
		val = le32_to_cpu(val);
	rmb();

	return val;
}

static inline void sai_writel(struct fsl_sai *sai,
		u32 val, void __iomem *addr)
{
	wmb();
	if (likely(sai->big_endian_regs))
		val = cpu_to_be32(val);
	else
		val = cpu_to_le32(val);

	__raw_writel(val, addr);
}

static int fsl_sai_set_dai_sysclk_tr(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, int fsl_dir)
{
	u32 val_cr2, reg_cr2;
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);

	if (fsl_dir == FSL_FMT_TRANSMITTER)
		reg_cr2 = FSL_SAI_TCR2;
	else
		reg_cr2 = FSL_SAI_RCR2;

	val_cr2 = sai_readl(sai, sai->base + reg_cr2);
	switch (clk_id) {
	case FSL_SAI_CLK_BUS:
		val_cr2 &= ~FSL_SAI_CR2_MSEL_MASK;
		val_cr2 |= FSL_SAI_CR2_MSEL_BUS;
		break;
	case FSL_SAI_CLK_MAST1:
		val_cr2 &= ~FSL_SAI_CR2_MSEL_MASK;
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK1;
		break;
	case FSL_SAI_CLK_MAST2:
		val_cr2 &= ~FSL_SAI_CR2_MSEL_MASK;
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK2;
		break;
	case FSL_SAI_CLK_MAST3:
		val_cr2 &= ~FSL_SAI_CR2_MSEL_MASK;
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK3;
		break;
	default:
		return -EINVAL;
	}
	sai_writel(sai, val_cr2, sai->base + reg_cr2);

	return 0;
}

static int fsl_sai_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, int dir)
{
	int ret;
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);

	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	ret = clk_prepare_enable(sai->clk);
	if (ret)
		return ret;

	sai_writel(sai, 0x0, sai->base + FSL_SAI_RCSR);
	sai_writel(sai, 0x0, sai->base + FSL_SAI_TCSR);
	sai_writel(sai, FSL_SAI_MAXBURST_TX * 2, sai->base + FSL_SAI_TCR1);
	sai_writel(sai, FSL_SAI_MAXBURST_RX - 1, sai->base + FSL_SAI_RCR1);

	ret = fsl_sai_set_dai_sysclk_tr(cpu_dai, clk_id, freq,
					FSL_FMT_TRANSMITTER);
	if (ret) {
		dev_err(cpu_dai->dev,
				"Cannot set SAI's transmitter sysclk: %d\n",
				ret);
		goto err_clk;
	}

	ret = fsl_sai_set_dai_sysclk_tr(cpu_dai, clk_id, freq,
					FSL_FMT_RECEIVER);
	if (ret) {
		dev_err(cpu_dai->dev,
				"Cannot set SAI's receiver sysclk: %d\n",
				ret);
		goto err_clk;
	}

err_clk:
	clk_disable_unprepare(sai->clk);

	return ret;
}

static int fsl_sai_set_dai_fmt_tr(struct snd_soc_dai *cpu_dai,
				unsigned int fmt, int fsl_dir)
{
	u32 val_cr2, val_cr3, val_cr4, reg_cr2, reg_cr3, reg_cr4;
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);

	if (fsl_dir == FSL_FMT_TRANSMITTER) {
		reg_cr2 = FSL_SAI_TCR2;
		reg_cr3 = FSL_SAI_TCR3;
		reg_cr4 = FSL_SAI_TCR4;
	} else {
		reg_cr2 = FSL_SAI_RCR2;
		reg_cr3 = FSL_SAI_RCR3;
		reg_cr4 = FSL_SAI_RCR4;
	}

	val_cr2 = sai_readl(sai, sai->base + reg_cr2);
	val_cr3 = sai_readl(sai, sai->base + reg_cr3);
	val_cr4 = sai_readl(sai, sai->base + reg_cr4);

	if (sai->big_endian_data)
		val_cr4 |= FSL_SAI_CR4_MF;
	else
		val_cr4 &= ~FSL_SAI_CR4_MF;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val_cr4 |= FSL_SAI_CR4_FSE;
		val_cr4 |= FSL_SAI_CR4_FSP;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		val_cr4 |= FSL_SAI_CR4_FSP;
		val_cr2 &= ~FSL_SAI_CR2_BCP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val_cr4 &= ~FSL_SAI_CR4_FSP;
		val_cr2 &= ~FSL_SAI_CR2_BCP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		val_cr4 |= FSL_SAI_CR4_FSP;
		val_cr2 |= FSL_SAI_CR2_BCP;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		val_cr4 &= ~FSL_SAI_CR4_FSP;
		val_cr2 |= FSL_SAI_CR2_BCP;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		val_cr2 |= FSL_SAI_CR2_BCD_MSTR;
		val_cr4 |= FSL_SAI_CR4_FSD_MSTR;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		val_cr2 &= ~FSL_SAI_CR2_BCD_MSTR;
		val_cr4 &= ~FSL_SAI_CR4_FSD_MSTR;
		break;
	default:
		return -EINVAL;
	}

	val_cr3 |= FSL_SAI_CR3_TRCE;

	if (fsl_dir == FSL_FMT_RECEIVER)
		val_cr2 |= FSL_SAI_CR2_SYNC;

	sai_writel(sai, val_cr2, sai->base + reg_cr2);
	sai_writel(sai, val_cr3, sai->base + reg_cr3);
	sai_writel(sai, val_cr4, sai->base + reg_cr4);

	return 0;
}

static int fsl_sai_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	int ret;
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);

	ret = clk_prepare_enable(sai->clk);
	if (ret)
		return ret;

	ret = fsl_sai_set_dai_fmt_tr(cpu_dai, fmt, FSL_FMT_TRANSMITTER);
	if (ret) {
		dev_err(cpu_dai->dev,
				"Cannot set SAI's transmitter format: %d\n",
				ret);
		goto err_clk;
	}

	ret = fsl_sai_set_dai_fmt_tr(cpu_dai, fmt, FSL_FMT_RECEIVER);
	if (ret) {
		dev_err(cpu_dai->dev,
				"Cannot set SAI's receiver format: %d\n",
				ret);
		goto err_clk;
	}

err_clk:
	clk_disable_unprepare(sai->clk);

	return ret;
}

static int fsl_sai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *cpu_dai)
{
	u32 val_cr4, val_cr5, val_mr, reg_cr4, reg_cr5, reg_mr;
	unsigned int channels = params_channels(params);
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	u32 word_width = snd_pcm_format_width(params_format(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		reg_cr4 = FSL_SAI_TCR4;
		reg_cr5 = FSL_SAI_TCR5;
		reg_mr = FSL_SAI_TMR;
	} else {
		reg_cr4 = FSL_SAI_RCR4;
		reg_cr5 = FSL_SAI_RCR5;
		reg_mr = FSL_SAI_RMR;
	}

	val_cr4 = sai_readl(sai, sai->base + reg_cr4);
	val_cr4 &= ~FSL_SAI_CR4_SYWD_MASK;
	val_cr4 &= ~FSL_SAI_CR4_FRSZ_MASK;

	val_cr5 = sai_readl(sai, sai->base + reg_cr5);
	val_cr5 &= ~FSL_SAI_CR5_WNW_MASK;
	val_cr5 &= ~FSL_SAI_CR5_W0W_MASK;
	val_cr5 &= ~FSL_SAI_CR5_FBT_MASK;

	val_cr4 |= FSL_SAI_CR4_SYWD(word_width);
	val_cr5 |= FSL_SAI_CR5_WNW(word_width);
	val_cr5 |= FSL_SAI_CR5_W0W(word_width);

	if (sai->big_endian_data)
		val_cr5 |= FSL_SAI_CR5_FBT(word_width - 1);
	else
		val_cr5 |= FSL_SAI_CR5_FBT(0);

	val_cr4 |= FSL_SAI_CR4_FRSZ(channels);
	if (channels == 2 || channels == 1)
		val_mr = ~0UL - ((1 << channels) - 1);
	else
		return -EINVAL;

	sai_writel(sai, val_cr4, sai->base + reg_cr4);
	sai_writel(sai, val_cr5, sai->base + reg_cr5);
	sai_writel(sai, val_mr, sai->base + reg_mr);

	return 0;
}

static int fsl_sai_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int tcsr, rcsr;

	tcsr = sai_readl(sai, sai->base + FSL_SAI_TCSR);
	rcsr = sai_readl(sai, sai->base + FSL_SAI_RCSR);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		tcsr |= FSL_SAI_CSR_FRDE;
		rcsr &= ~FSL_SAI_CSR_FRDE;
	} else {
		rcsr |= FSL_SAI_CSR_FRDE;
		tcsr &= ~FSL_SAI_CSR_FRDE;
	}

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		tcsr |= FSL_SAI_CSR_TERE;
		rcsr |= FSL_SAI_CSR_TERE;
		sai_writel(sai, rcsr, sai->base + FSL_SAI_RCSR);
		sai_writel(sai, tcsr, sai->base + FSL_SAI_TCSR);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (!(cpu_dai->playback_active || cpu_dai->capture_active)) {
			tcsr &= ~FSL_SAI_CSR_TERE;
			rcsr &= ~FSL_SAI_CSR_TERE;
		}
		sai_writel(sai, tcsr, sai->base + FSL_SAI_TCSR);
		sai_writel(sai, rcsr, sai->base + FSL_SAI_RCSR);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fsl_sai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	int ret;
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);

	ret = clk_prepare_enable(sai->clk);

	return ret;
}

static void fsl_sai_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);

	clk_disable_unprepare(sai->clk);
}

static const struct snd_soc_dai_ops fsl_sai_pcm_dai_ops = {
	.set_sysclk	= fsl_sai_set_dai_sysclk,
	.set_fmt	= fsl_sai_set_dai_fmt,
	.hw_params	= fsl_sai_hw_params,
	.trigger	= fsl_sai_trigger,
	.startup	= fsl_sai_startup,
	.shutdown	= fsl_sai_shutdown,
};

static int fsl_sai_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = dev_get_drvdata(cpu_dai->dev);

	snd_soc_dai_init_dma_data(cpu_dai, &sai->dma_params_tx,
				&sai->dma_params_rx);

	snd_soc_dai_set_drvdata(cpu_dai, sai);

	return 0;
}

static struct snd_soc_dai_driver fsl_sai_dai = {
	.probe = fsl_sai_dai_probe,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = FSL_SAI_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = FSL_SAI_FORMATS,
	},
	.ops = &fsl_sai_pcm_dai_ops,
};

static const struct snd_soc_component_driver fsl_component = {
	.name           = "fsl-sai",
};

static int fsl_sai_probe(struct platform_device *pdev)
{
	int ret;
	struct fsl_sai *sai;
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	sai->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(sai->base))
		return PTR_ERR(sai->base);

	sai->clk = devm_clk_get(&pdev->dev, "sai");
	if (IS_ERR(sai->clk)) {
		dev_err(&pdev->dev, "Cannot get SAI's clock\n");
		return PTR_ERR(sai->clk);
	}

	sai->dma_params_rx.addr = res->start + FSL_SAI_RDR;
	sai->dma_params_tx.addr = res->start + FSL_SAI_TDR;
	sai->dma_params_rx.maxburst = FSL_SAI_MAXBURST_RX;
	sai->dma_params_tx.maxburst = FSL_SAI_MAXBURST_TX;

	sai->big_endian_regs = of_property_read_bool(np, "big-endian-regs");
	sai->big_endian_data = of_property_read_bool(np, "big-endian-data");

	platform_set_drvdata(pdev, sai);

	ret = devm_snd_soc_register_component(&pdev->dev, &fsl_component,
			&fsl_sai_dai, 1);
	if (ret)
		return ret;

	return devm_snd_dmaengine_pcm_register(&pdev->dev, NULL,
			SND_DMAENGINE_PCM_FLAG_NO_RESIDUE);
}

static const struct of_device_id fsl_sai_ids[] = {
	{ .compatible = "fsl,vf610-sai", },
	{ /* sentinel */ }
};

static struct platform_driver fsl_sai_driver = {
	.probe = fsl_sai_probe,
	.driver = {
		.name = "fsl-sai",
		.owner = THIS_MODULE,
		.of_match_table = fsl_sai_ids,
	},
};
module_platform_driver(fsl_sai_driver);

MODULE_DESCRIPTION("Freescale Soc SAI Interface");
MODULE_AUTHOR("Xiubo Li, <Li.Xiubo@freescale.com>");
MODULE_ALIAS("platform:fsl-sai");
MODULE_LICENSE("GPL");
