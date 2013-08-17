/*
 * linux/sound/soc/ep93xx-i2s.c
 * EP93xx I2S driver
 *
 * Copyright (C) 2010 Ryan Mallon
 *
 * Based on the original driver by:
 *   Copyright (C) 2007 Chase Douglas <chasedouglas@gmail>
 *   Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include <mach/hardware.h>
#include <mach/ep93xx-regs.h>
#include <mach/dma.h>

#include "ep93xx-pcm.h"

#define EP93XX_I2S_TXCLKCFG		0x00
#define EP93XX_I2S_RXCLKCFG		0x04
#define EP93XX_I2S_GLCTRL		0x0C

#define EP93XX_I2S_TXLINCTRLDATA	0x28
#define EP93XX_I2S_TXCTRL		0x2C
#define EP93XX_I2S_TXWRDLEN		0x30
#define EP93XX_I2S_TX0EN		0x34

#define EP93XX_I2S_RXLINCTRLDATA	0x58
#define EP93XX_I2S_RXCTRL		0x5C
#define EP93XX_I2S_RXWRDLEN		0x60
#define EP93XX_I2S_RX0EN		0x64

#define EP93XX_I2S_WRDLEN_16		(0 << 0)
#define EP93XX_I2S_WRDLEN_24		(1 << 0)
#define EP93XX_I2S_WRDLEN_32		(2 << 0)

#define EP93XX_I2S_LINCTRLDATA_R_JUST	(1 << 2) /* Right justify */

#define EP93XX_I2S_CLKCFG_LRS		(1 << 0) /* lrclk polarity */
#define EP93XX_I2S_CLKCFG_CKP		(1 << 1) /* Bit clock polarity */
#define EP93XX_I2S_CLKCFG_REL		(1 << 2) /* First bit transition */
#define EP93XX_I2S_CLKCFG_MASTER	(1 << 3) /* Master mode */
#define EP93XX_I2S_CLKCFG_NBCG		(1 << 4) /* Not bit clock gating */

struct ep93xx_i2s_info {
	struct clk			*mclk;
	struct clk			*sclk;
	struct clk			*lrclk;
	struct ep93xx_pcm_dma_params	*dma_params;
	struct resource			*mem;
	void __iomem			*regs;
};

struct ep93xx_pcm_dma_params ep93xx_i2s_dma_params[] = {
	[SNDRV_PCM_STREAM_PLAYBACK] = {
		.name		= "i2s-pcm-out",
		.dma_port	= EP93XX_DMA_I2S1,
	},
	[SNDRV_PCM_STREAM_CAPTURE] = {
		.name		= "i2s-pcm-in",
		.dma_port	= EP93XX_DMA_I2S1,
	},
};

static inline void ep93xx_i2s_write_reg(struct ep93xx_i2s_info *info,
					unsigned reg, unsigned val)
{
	__raw_writel(val, info->regs + reg);
}

static inline unsigned ep93xx_i2s_read_reg(struct ep93xx_i2s_info *info,
					   unsigned reg)
{
	return __raw_readl(info->regs + reg);
}

static void ep93xx_i2s_enable(struct ep93xx_i2s_info *info, int stream)
{
	unsigned base_reg;
	int i;

	if ((ep93xx_i2s_read_reg(info, EP93XX_I2S_TX0EN) & 0x1) == 0 &&
	    (ep93xx_i2s_read_reg(info, EP93XX_I2S_RX0EN) & 0x1) == 0) {
		/* Enable clocks */
		clk_enable(info->mclk);
		clk_enable(info->sclk);
		clk_enable(info->lrclk);

		/* Enable i2s */
		ep93xx_i2s_write_reg(info, EP93XX_I2S_GLCTRL, 1);
	}

	/* Enable fifos */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		base_reg = EP93XX_I2S_TX0EN;
	else
		base_reg = EP93XX_I2S_RX0EN;
	for (i = 0; i < 3; i++)
		ep93xx_i2s_write_reg(info, base_reg + (i * 4), 1);
}

static void ep93xx_i2s_disable(struct ep93xx_i2s_info *info, int stream)
{
	unsigned base_reg;
	int i;

	/* Disable fifos */
	if (stream == SNDRV_PCM_STREAM_PLAYBACK)
		base_reg = EP93XX_I2S_TX0EN;
	else
		base_reg = EP93XX_I2S_RX0EN;
	for (i = 0; i < 3; i++)
		ep93xx_i2s_write_reg(info, base_reg + (i * 4), 0);

	if ((ep93xx_i2s_read_reg(info, EP93XX_I2S_TX0EN) & 0x1) == 0 &&
	    (ep93xx_i2s_read_reg(info, EP93XX_I2S_RX0EN) & 0x1) == 0) {
		/* Disable i2s */
		ep93xx_i2s_write_reg(info, EP93XX_I2S_GLCTRL, 0);

		/* Disable clocks */
		clk_disable(info->lrclk);
		clk_disable(info->sclk);
		clk_disable(info->mclk);
	}
}

static int ep93xx_i2s_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct ep93xx_i2s_info *info = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;

	snd_soc_dai_set_dma_data(cpu_dai, substream,
				 &info->dma_params[substream->stream]);
	return 0;
}

static void ep93xx_i2s_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct ep93xx_i2s_info *info = snd_soc_dai_get_drvdata(dai);

	ep93xx_i2s_disable(info, substream->stream);
}

static int ep93xx_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				  unsigned int fmt)
{
	struct ep93xx_i2s_info *info = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int clk_cfg, lin_ctrl;

	clk_cfg  = ep93xx_i2s_read_reg(info, EP93XX_I2S_RXCLKCFG);
	lin_ctrl = ep93xx_i2s_read_reg(info, EP93XX_I2S_RXLINCTRLDATA);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		clk_cfg |= EP93XX_I2S_CLKCFG_REL;
		lin_ctrl &= ~EP93XX_I2S_LINCTRLDATA_R_JUST;
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		clk_cfg &= ~EP93XX_I2S_CLKCFG_REL;
		lin_ctrl &= ~EP93XX_I2S_LINCTRLDATA_R_JUST;
		break;

	case SND_SOC_DAIFMT_RIGHT_J:
		clk_cfg &= ~EP93XX_I2S_CLKCFG_REL;
		lin_ctrl |= EP93XX_I2S_LINCTRLDATA_R_JUST;
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* CPU is master */
		clk_cfg |= EP93XX_I2S_CLKCFG_MASTER;
		break;

	case SND_SOC_DAIFMT_CBM_CFM:
		/* Codec is master */
		clk_cfg &= ~EP93XX_I2S_CLKCFG_MASTER;
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/* Negative bit clock, lrclk low on left word */
		clk_cfg &= ~(EP93XX_I2S_CLKCFG_CKP | EP93XX_I2S_CLKCFG_REL);
		break;

	case SND_SOC_DAIFMT_NB_IF:
		/* Negative bit clock, lrclk low on right word */
		clk_cfg &= ~EP93XX_I2S_CLKCFG_CKP;
		clk_cfg |= EP93XX_I2S_CLKCFG_REL;
		break;

	case SND_SOC_DAIFMT_IB_NF:
		/* Positive bit clock, lrclk low on left word */
		clk_cfg |= EP93XX_I2S_CLKCFG_CKP;
		clk_cfg &= ~EP93XX_I2S_CLKCFG_REL;
		break;

	case SND_SOC_DAIFMT_IB_IF:
		/* Positive bit clock, lrclk low on right word */
		clk_cfg |= EP93XX_I2S_CLKCFG_CKP | EP93XX_I2S_CLKCFG_REL;
		break;
	}

	/* Write new register values */
	ep93xx_i2s_write_reg(info, EP93XX_I2S_RXCLKCFG, clk_cfg);
	ep93xx_i2s_write_reg(info, EP93XX_I2S_TXCLKCFG, clk_cfg);
	ep93xx_i2s_write_reg(info, EP93XX_I2S_RXLINCTRLDATA, lin_ctrl);
	ep93xx_i2s_write_reg(info, EP93XX_I2S_TXLINCTRLDATA, lin_ctrl);
	return 0;
}

static int ep93xx_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct ep93xx_i2s_info *info = snd_soc_dai_get_drvdata(dai);
	unsigned word_len, div, sdiv, lrdiv;
	int err;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		word_len = EP93XX_I2S_WRDLEN_16;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		word_len = EP93XX_I2S_WRDLEN_24;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		word_len = EP93XX_I2S_WRDLEN_32;
		break;

	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ep93xx_i2s_write_reg(info, EP93XX_I2S_TXWRDLEN, word_len);
	else
		ep93xx_i2s_write_reg(info, EP93XX_I2S_RXWRDLEN, word_len);

	/*
	 * EP93xx I2S module can be setup so SCLK / LRCLK value can be
	 * 32, 64, 128. MCLK / SCLK value can be 2 and 4.
	 * We set LRCLK equal to `rate' and minimum SCLK / LRCLK 
	 * value is 64, because our sample size is 32 bit * 2 channels.
	 * I2S standard permits us to transmit more bits than
	 * the codec uses.
	 */
	div = clk_get_rate(info->mclk) / params_rate(params);
	sdiv = 4;
	if (div > (256 + 512) / 2) {
		lrdiv = 128;
	} else {
		lrdiv = 64;
		if (div < (128 + 256) / 2)
			sdiv = 2;
	}

	err = clk_set_rate(info->sclk, clk_get_rate(info->mclk) / sdiv);
	if (err)
		return err;

	err = clk_set_rate(info->lrclk, clk_get_rate(info->sclk) / lrdiv);
	if (err)
		return err;

	ep93xx_i2s_enable(info, substream->stream);
	return 0;
}

static int ep93xx_i2s_set_sysclk(struct snd_soc_dai *cpu_dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct ep93xx_i2s_info *info = snd_soc_dai_get_drvdata(cpu_dai);

	if (dir == SND_SOC_CLOCK_IN || clk_id != 0)
		return -EINVAL;

	return clk_set_rate(info->mclk, freq);
}

#ifdef CONFIG_PM
static int ep93xx_i2s_suspend(struct snd_soc_dai *dai)
{
	struct ep93xx_i2s_info *info = snd_soc_dai_get_drvdata(dai);

	if (!dai->active)
		return 0;

	ep93xx_i2s_disable(info, SNDRV_PCM_STREAM_PLAYBACK);
	ep93xx_i2s_disable(info, SNDRV_PCM_STREAM_CAPTURE);

	return 0;
}

static int ep93xx_i2s_resume(struct snd_soc_dai *dai)
{
	struct ep93xx_i2s_info *info = snd_soc_dai_get_drvdata(dai);

	if (!dai->active)
		return 0;

	ep93xx_i2s_enable(info, SNDRV_PCM_STREAM_PLAYBACK);
	ep93xx_i2s_enable(info, SNDRV_PCM_STREAM_CAPTURE);

	return 0;
}
#else
#define ep93xx_i2s_suspend	NULL
#define ep93xx_i2s_resume	NULL
#endif

static const struct snd_soc_dai_ops ep93xx_i2s_dai_ops = {
	.startup	= ep93xx_i2s_startup,
	.shutdown	= ep93xx_i2s_shutdown,
	.hw_params	= ep93xx_i2s_hw_params,
	.set_sysclk	= ep93xx_i2s_set_sysclk,
	.set_fmt	= ep93xx_i2s_set_dai_fmt,
};

#define EP93XX_I2S_FORMATS (SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver ep93xx_i2s_dai = {
	.symmetric_rates= 1,
	.suspend	= ep93xx_i2s_suspend,
	.resume		= ep93xx_i2s_resume,
	.playback	= {
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= EP93XX_I2S_FORMATS,
	},
	.capture	= {
		 .channels_min	= 2,
		 .channels_max	= 2,
		 .rates		= SNDRV_PCM_RATE_8000_192000,
		 .formats	= EP93XX_I2S_FORMATS,
	},
	.ops		= &ep93xx_i2s_dai_ops,
};

static int ep93xx_i2s_probe(struct platform_device *pdev)
{
	struct ep93xx_i2s_info *info;
	struct resource *res;
	int err;

	info = kzalloc(sizeof(struct ep93xx_i2s_info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto fail;
	}

	dev_set_drvdata(&pdev->dev, info);
	info->dma_params = ep93xx_i2s_dma_params;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENODEV;
		goto fail_free_info;
	}

	info->mem = request_mem_region(res->start, resource_size(res),
				       pdev->name);
	if (!info->mem) {
		err = -EBUSY;
		goto fail_free_info;
	}

	info->regs = ioremap(info->mem->start, resource_size(info->mem));
	if (!info->regs) {
		err = -ENXIO;
		goto fail_release_mem;
	}

	info->mclk = clk_get(&pdev->dev, "mclk");
	if (IS_ERR(info->mclk)) {
		err = PTR_ERR(info->mclk);
		goto fail_unmap_mem;
	}

	info->sclk = clk_get(&pdev->dev, "sclk");
	if (IS_ERR(info->sclk)) {
		err = PTR_ERR(info->sclk);
		goto fail_put_mclk;
	}

	info->lrclk = clk_get(&pdev->dev, "lrclk");
	if (IS_ERR(info->lrclk)) {
		err = PTR_ERR(info->lrclk);
		goto fail_put_sclk;
	}

	err = snd_soc_register_dai(&pdev->dev, &ep93xx_i2s_dai);
	if (err)
		goto fail_put_lrclk;

	return 0;

fail_put_lrclk:
	clk_put(info->lrclk);
fail_put_sclk:
	clk_put(info->sclk);
fail_put_mclk:
	clk_put(info->mclk);
fail_unmap_mem:
	iounmap(info->regs);
fail_release_mem:
	release_mem_region(info->mem->start, resource_size(info->mem));
fail_free_info:
	kfree(info);
fail:
	return err;
}

static int __devexit ep93xx_i2s_remove(struct platform_device *pdev)
{
	struct ep93xx_i2s_info *info = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_dai(&pdev->dev);
	clk_put(info->lrclk);
	clk_put(info->sclk);
	clk_put(info->mclk);
	iounmap(info->regs);
	release_mem_region(info->mem->start, resource_size(info->mem));
	kfree(info);
	return 0;
}

static struct platform_driver ep93xx_i2s_driver = {
	.probe	= ep93xx_i2s_probe,
	.remove	= __devexit_p(ep93xx_i2s_remove),
	.driver	= {
		.name	= "ep93xx-i2s",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(ep93xx_i2s_driver);

MODULE_ALIAS("platform:ep93xx-i2s");
MODULE_AUTHOR("Ryan Mallon");
MODULE_DESCRIPTION("EP93XX I2S driver");
MODULE_LICENSE("GPL");
