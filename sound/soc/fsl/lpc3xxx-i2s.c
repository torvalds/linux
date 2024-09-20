// SPDX-License-Identifier: GPL-2.0-or-later
//
// Author: Kevin Wells <kevin.wells@nxp.com>
//
// Copyright (C) 2008 NXP Semiconductors
// Copyright 2023 Timesys Corporation <piotr.wojtaszczyk@timesys.com>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "lpc3xxx-i2s.h"

#define I2S_PLAYBACK_FLAG 0x1
#define I2S_CAPTURE_FLAG 0x2

#define LPC3XXX_I2S_RATES ( \
	SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | \
	SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000)

#define LPC3XXX_I2S_FORMATS ( \
	SNDRV_PCM_FMTBIT_S8 | \
	SNDRV_PCM_FMTBIT_S16_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)

static void __lpc3xxx_find_clkdiv(u32 *clkx, u32 *clky, int freq, int xbytes, u32 clkrate)
{
	u32 i2srate;
	u32 idxx, idyy;
	u32 savedbitclkrate, diff, trate, baseclk;

	/* Adjust rate for sample size (bits) and 2 channels and offset for
	 * divider in clock output
	 */
	i2srate = (freq / 100) * 2 * (8 * xbytes);
	i2srate = i2srate << 1;
	clkrate = clkrate / 100;
	baseclk = clkrate;
	*clkx = 1;
	*clky = 1;

	/* Find the best divider */
	*clkx = *clky = 0;
	savedbitclkrate = 0;
	diff = ~0;
	for (idxx = 1; idxx < 0xFF; idxx++) {
		for (idyy = 1; idyy < 0xFF; idyy++) {
			trate = (baseclk * idxx) / idyy;
			if (abs(trate - i2srate) < diff) {
				diff = abs(trate - i2srate);
				savedbitclkrate = trate;
				*clkx = idxx;
				*clky = idyy;
			}
		}
	}
}

static int lpc3xxx_i2s_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *cpu_dai)
{
	struct lpc3xxx_i2s_info *i2s_info_p = snd_soc_dai_get_drvdata(cpu_dai);
	struct device *dev = i2s_info_p->dev;
	u32 flag;
	int ret = 0;

	guard(mutex)(&i2s_info_p->lock);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		flag = I2S_PLAYBACK_FLAG;
	else
		flag = I2S_CAPTURE_FLAG;

	if (flag & i2s_info_p->streams_in_use) {
		dev_warn(dev, "I2S channel is busy\n");
		ret = -EBUSY;
		return ret;
	}

	if (i2s_info_p->streams_in_use == 0) {
		ret = clk_prepare_enable(i2s_info_p->clk);
		if (ret) {
			dev_err(dev, "Can't enable clock, err=%d\n", ret);
			return ret;
		}
	}

	i2s_info_p->streams_in_use |= flag;
	return 0;
}

static void lpc3xxx_i2s_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *cpu_dai)
{
	struct lpc3xxx_i2s_info *i2s_info_p = snd_soc_dai_get_drvdata(cpu_dai);
	struct regmap *regs = i2s_info_p->regs;
	const u32 stop_bits = (LPC3XXX_I2S_RESET | LPC3XXX_I2S_STOP);
	u32 flag;

	guard(mutex)(&i2s_info_p->lock);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		flag = I2S_PLAYBACK_FLAG;
		regmap_write(regs, LPC3XXX_REG_I2S_TX_RATE, 0);
		regmap_update_bits(regs, LPC3XXX_REG_I2S_DAO, stop_bits, stop_bits);
	} else {
		flag = I2S_CAPTURE_FLAG;
		regmap_write(regs, LPC3XXX_REG_I2S_RX_RATE, 0);
		regmap_update_bits(regs, LPC3XXX_REG_I2S_DAI, stop_bits, stop_bits);
	}
	i2s_info_p->streams_in_use &= ~flag;

	if (i2s_info_p->streams_in_use == 0)
		clk_disable_unprepare(i2s_info_p->clk);
}

static int lpc3xxx_i2s_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
				      int clk_id, unsigned int freq, int dir)
{
	struct lpc3xxx_i2s_info *i2s_info_p = snd_soc_dai_get_drvdata(cpu_dai);

	/* Will use in HW params later */
	i2s_info_p->freq = freq;

	return 0;
}

static int lpc3xxx_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct lpc3xxx_i2s_info *i2s_info_p = snd_soc_dai_get_drvdata(cpu_dai);
	struct device *dev = i2s_info_p->dev;

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S) {
		dev_warn(dev, "unsupported bus format %d\n", fmt);
		return -EINVAL;
	}

	if ((fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) != SND_SOC_DAIFMT_BP_FP) {
		dev_warn(dev, "unsupported clock provider %d\n", fmt);
		return -EINVAL;
	}

	return 0;
}

static int lpc3xxx_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *cpu_dai)
{
	struct lpc3xxx_i2s_info *i2s_info_p = snd_soc_dai_get_drvdata(cpu_dai);
	struct device *dev = i2s_info_p->dev;
	struct regmap *regs = i2s_info_p->regs;
	int xfersize;
	u32 tmp, clkx, clky;

	tmp = LPC3XXX_I2S_RESET | LPC3XXX_I2S_STOP;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		tmp |= LPC3XXX_I2S_WW8 | LPC3XXX_I2S_WS_HP(LPC3XXX_I2S_WW8_HP);
		xfersize = 1;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
		tmp |= LPC3XXX_I2S_WW16 | LPC3XXX_I2S_WS_HP(LPC3XXX_I2S_WW16_HP);
		xfersize = 2;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		tmp |= LPC3XXX_I2S_WW32 | LPC3XXX_I2S_WS_HP(LPC3XXX_I2S_WW32_HP);
		xfersize = 4;
		break;

	default:
		dev_warn(dev, "Unsupported audio data format %d\n", params_format(params));
		return -EINVAL;
	}

	if (params_channels(params) == 1)
		tmp |= LPC3XXX_I2S_MONO;

	__lpc3xxx_find_clkdiv(&clkx, &clky, i2s_info_p->freq, xfersize, i2s_info_p->clkrate);

	dev_dbg(dev, "Stream                : %s\n",
		substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "playback" : "capture");
	dev_dbg(dev, "Desired clock rate    : %d\n", i2s_info_p->freq);
	dev_dbg(dev, "Base clock rate       : %d\n", i2s_info_p->clkrate);
	dev_dbg(dev, "Transfer size (bytes) : %d\n", xfersize);
	dev_dbg(dev, "Clock divider (x)     : %d\n", clkx);
	dev_dbg(dev, "Clock divider (y)     : %d\n", clky);
	dev_dbg(dev, "Channels              : %d\n", params_channels(params));
	dev_dbg(dev, "Data format           : %s\n", "I2S");

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		regmap_write(regs, LPC3XXX_REG_I2S_DMA1,
			     LPC3XXX_I2S_DMA1_TX_EN | LPC3XXX_I2S_DMA0_TX_DEPTH(4));
		regmap_write(regs, LPC3XXX_REG_I2S_TX_RATE, (clkx << 8) | clky);
		regmap_write(regs, LPC3XXX_REG_I2S_DAO, tmp);
	} else {
		regmap_write(regs, LPC3XXX_REG_I2S_DMA0,
			     LPC3XXX_I2S_DMA0_RX_EN | LPC3XXX_I2S_DMA1_RX_DEPTH(4));
		regmap_write(regs, LPC3XXX_REG_I2S_RX_RATE, (clkx << 8) | clky);
		regmap_write(regs, LPC3XXX_REG_I2S_DAI, tmp);
	}

	return 0;
}

static int lpc3xxx_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *cpu_dai)
{
	struct lpc3xxx_i2s_info *i2s_info_p = snd_soc_dai_get_drvdata(cpu_dai);
	struct regmap *regs = i2s_info_p->regs;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(regs, LPC3XXX_REG_I2S_DAO,
					   LPC3XXX_I2S_STOP, LPC3XXX_I2S_STOP);
		else
			regmap_update_bits(regs, LPC3XXX_REG_I2S_DAI,
					   LPC3XXX_I2S_STOP, LPC3XXX_I2S_STOP);
		break;

	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			regmap_update_bits(regs, LPC3XXX_REG_I2S_DAO,
					   (LPC3XXX_I2S_RESET | LPC3XXX_I2S_STOP), 0);
		else
			regmap_update_bits(regs, LPC3XXX_REG_I2S_DAI,
					   (LPC3XXX_I2S_RESET | LPC3XXX_I2S_STOP), 0);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int lpc3xxx_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct lpc3xxx_i2s_info *i2s_info_p = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &i2s_info_p->playback_dma_config,
				  &i2s_info_p->capture_dma_config);
	return 0;
}

const struct snd_soc_dai_ops lpc3xxx_i2s_dai_ops = {
	.probe	= lpc3xxx_i2s_dai_probe,
	.startup = lpc3xxx_i2s_startup,
	.shutdown = lpc3xxx_i2s_shutdown,
	.trigger = lpc3xxx_i2s_trigger,
	.hw_params = lpc3xxx_i2s_hw_params,
	.set_sysclk = lpc3xxx_i2s_set_dai_sysclk,
	.set_fmt = lpc3xxx_i2s_set_dai_fmt,
};

struct snd_soc_dai_driver lpc3xxx_i2s_dai_driver = {
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = LPC3XXX_I2S_RATES,
		.formats = LPC3XXX_I2S_FORMATS,
		},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = LPC3XXX_I2S_RATES,
		.formats = LPC3XXX_I2S_FORMATS,
		},
	.ops = &lpc3xxx_i2s_dai_ops,
	.symmetric_rate = 1,
	.symmetric_channels = 1,
	.symmetric_sample_bits = 1,
};

static const struct snd_soc_component_driver lpc32xx_i2s_component = {
	.name = "lpc32xx-i2s",
	.legacy_dai_naming = 1,
};

static const struct regmap_config lpc32xx_i2s_regconfig = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = LPC3XXX_REG_I2S_RX_RATE,
};

static int lpc32xx_i2s_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lpc3xxx_i2s_info *i2s_info_p;
	struct resource *res;
	void __iomem *iomem;
	int ret;

	i2s_info_p = devm_kzalloc(dev, sizeof(*i2s_info_p), GFP_KERNEL);
	if (!i2s_info_p)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2s_info_p);
	i2s_info_p->dev = dev;

	iomem = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(iomem))
		return dev_err_probe(dev, PTR_ERR(iomem), "Can't map registers\n");

	i2s_info_p->regs = devm_regmap_init_mmio(dev, iomem, &lpc32xx_i2s_regconfig);
	if (IS_ERR(i2s_info_p->regs))
		return dev_err_probe(dev, PTR_ERR(i2s_info_p->regs),
				     "failed to init register map: %pe\n", i2s_info_p->regs);

	i2s_info_p->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(i2s_info_p->clk))
		return dev_err_probe(dev, PTR_ERR(i2s_info_p->clk), "Can't get clock\n");

	i2s_info_p->clkrate = clk_get_rate(i2s_info_p->clk);
	if (i2s_info_p->clkrate == 0)
		return dev_err_probe(dev, -EINVAL, "Invalid returned clock rate\n");

	mutex_init(&i2s_info_p->lock);

	ret = devm_snd_soc_register_component(dev, &lpc32xx_i2s_component,
					      &lpc3xxx_i2s_dai_driver, 1);
	if (ret)
		return dev_err_probe(dev, ret, "Can't register cpu_dai component\n");

	i2s_info_p->playback_dma_config.addr = (dma_addr_t)(res->start + LPC3XXX_REG_I2S_TX_FIFO);
	i2s_info_p->playback_dma_config.maxburst = 4;

	i2s_info_p->capture_dma_config.addr = (dma_addr_t)(res->start + LPC3XXX_REG_I2S_RX_FIFO);
	i2s_info_p->capture_dma_config.maxburst = 4;

	ret = lpc3xxx_pcm_register(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "Can't register pcm component\n");

	return 0;
}

static const struct of_device_id lpc32xx_i2s_match[] = {
	{ .compatible = "nxp,lpc3220-i2s" },
	{},
};
MODULE_DEVICE_TABLE(of, lpc32xx_i2s_match);

static struct platform_driver lpc32xx_i2s_driver = {
	.probe = lpc32xx_i2s_probe,
	.driver		= {
		.name	= "lpc3xxx-i2s",
		.of_match_table = lpc32xx_i2s_match,
	},
};

module_platform_driver(lpc32xx_i2s_driver);

MODULE_AUTHOR("Kevin Wells <kevin.wells@nxp.com>");
MODULE_AUTHOR("Piotr Wojtaszczyk <piotr.wojtaszczyk@timesys.com>");
MODULE_DESCRIPTION("ASoC LPC3XXX I2S interface");
MODULE_LICENSE("GPL");
