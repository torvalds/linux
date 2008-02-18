/*
 * ALSA SoC I2S (McBSP) Audio Layer for TI DAVINCI processor
 *
 * Author:      Vladimir Barinov, <vbarinov@ru.mvista.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "davinci-pcm.h"

#define DAVINCI_MCBSP_DRR_REG	0x00
#define DAVINCI_MCBSP_DXR_REG	0x04
#define DAVINCI_MCBSP_SPCR_REG	0x08
#define DAVINCI_MCBSP_RCR_REG	0x0c
#define DAVINCI_MCBSP_XCR_REG	0x10
#define DAVINCI_MCBSP_SRGR_REG	0x14
#define DAVINCI_MCBSP_PCR_REG	0x24

#define DAVINCI_MCBSP_SPCR_RRST		(1 << 0)
#define DAVINCI_MCBSP_SPCR_RINTM(v)	((v) << 4)
#define DAVINCI_MCBSP_SPCR_XRST		(1 << 16)
#define DAVINCI_MCBSP_SPCR_XINTM(v)	((v) << 20)
#define DAVINCI_MCBSP_SPCR_GRST		(1 << 22)
#define DAVINCI_MCBSP_SPCR_FRST		(1 << 23)
#define DAVINCI_MCBSP_SPCR_FREE		(1 << 25)

#define DAVINCI_MCBSP_RCR_RWDLEN1(v)	((v) << 5)
#define DAVINCI_MCBSP_RCR_RFRLEN1(v)	((v) << 8)
#define DAVINCI_MCBSP_RCR_RDATDLY(v)	((v) << 16)
#define DAVINCI_MCBSP_RCR_RWDLEN2(v)	((v) << 21)

#define DAVINCI_MCBSP_XCR_XWDLEN1(v)	((v) << 5)
#define DAVINCI_MCBSP_XCR_XFRLEN1(v)	((v) << 8)
#define DAVINCI_MCBSP_XCR_XDATDLY(v)	((v) << 16)
#define DAVINCI_MCBSP_XCR_XFIG		(1 << 18)
#define DAVINCI_MCBSP_XCR_XWDLEN2(v)	((v) << 21)

#define DAVINCI_MCBSP_SRGR_FWID(v)	((v) << 8)
#define DAVINCI_MCBSP_SRGR_FPER(v)	((v) << 16)
#define DAVINCI_MCBSP_SRGR_FSGM		(1 << 28)

#define DAVINCI_MCBSP_PCR_CLKRP		(1 << 0)
#define DAVINCI_MCBSP_PCR_CLKXP		(1 << 1)
#define DAVINCI_MCBSP_PCR_FSRP		(1 << 2)
#define DAVINCI_MCBSP_PCR_FSXP		(1 << 3)
#define DAVINCI_MCBSP_PCR_CLKRM		(1 << 8)
#define DAVINCI_MCBSP_PCR_CLKXM		(1 << 9)
#define DAVINCI_MCBSP_PCR_FSRM		(1 << 10)
#define DAVINCI_MCBSP_PCR_FSXM		(1 << 11)

#define MOD_REG_BIT(val, mask, set) do { \
	if (set) { \
		val |= mask; \
	} else { \
		val &= ~mask; \
	} \
} while (0)

enum {
	DAVINCI_MCBSP_WORD_8 = 0,
	DAVINCI_MCBSP_WORD_12,
	DAVINCI_MCBSP_WORD_16,
	DAVINCI_MCBSP_WORD_20,
	DAVINCI_MCBSP_WORD_24,
	DAVINCI_MCBSP_WORD_32,
};

static struct davinci_pcm_dma_params davinci_i2s_pcm_out = {
	.name = "I2S PCM Stereo out",
};

static struct davinci_pcm_dma_params davinci_i2s_pcm_in = {
	.name = "I2S PCM Stereo in",
};

struct davinci_mcbsp_dev {
	void __iomem			*base;
	struct clk			*clk;
	struct davinci_pcm_dma_params	*dma_params[2];
};

static inline void davinci_mcbsp_write_reg(struct davinci_mcbsp_dev *dev,
					   int reg, u32 val)
{
	__raw_writel(val, dev->base + reg);
}

static inline u32 davinci_mcbsp_read_reg(struct davinci_mcbsp_dev *dev, int reg)
{
	return __raw_readl(dev->base + reg);
}

static void davinci_mcbsp_start(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct davinci_mcbsp_dev *dev = rtd->dai->cpu_dai->private_data;
	u32 w;

	/* Start the sample generator and enable transmitter/receiver */
	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_GRST, 1);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_XRST, 1);
	else
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_RRST, 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);

	/* Start frame sync */
	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_FRST, 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);
}

static void davinci_mcbsp_stop(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct davinci_mcbsp_dev *dev = rtd->dai->cpu_dai->private_data;
	u32 w;

	/* Reset transmitter/receiver and sample rate/frame sync generators */
	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_GRST |
		       DAVINCI_MCBSP_SPCR_FRST, 0);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_XRST, 0);
	else
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_RRST, 0);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);
}

static int davinci_i2s_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_cpu_dai *cpu_dai = rtd->dai->cpu_dai;
	struct davinci_mcbsp_dev *dev = rtd->dai->cpu_dai->private_data;

	cpu_dai->dma_data = dev->dma_params[substream->stream];

	return 0;
}

static int davinci_i2s_set_dai_fmt(struct snd_soc_cpu_dai *cpu_dai,
				   unsigned int fmt)
{
	struct davinci_mcbsp_dev *dev = cpu_dai->private_data;
	u32 w;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG,
					DAVINCI_MCBSP_PCR_FSXM |
					DAVINCI_MCBSP_PCR_FSRM |
					DAVINCI_MCBSP_PCR_CLKXM |
					DAVINCI_MCBSP_PCR_CLKRM);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SRGR_REG,
					DAVINCI_MCBSP_SRGR_FSGM);
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, 0);
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_NF:
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_PCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_PCR_CLKXP |
			       DAVINCI_MCBSP_PCR_CLKRP, 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, w);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_PCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_PCR_FSXP |
			       DAVINCI_MCBSP_PCR_FSRP, 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, w);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_PCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_PCR_CLKXP |
			       DAVINCI_MCBSP_PCR_CLKRP |
			       DAVINCI_MCBSP_PCR_FSXP |
			       DAVINCI_MCBSP_PCR_FSRP, 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, w);
		break;
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int davinci_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct davinci_pcm_dma_params *dma_params = rtd->dai->cpu_dai->dma_data;
	struct davinci_mcbsp_dev *dev = rtd->dai->cpu_dai->private_data;
	struct snd_interval *i = NULL;
	int mcbsp_word_length;
	u32 w;

	/* general line settings */
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG,
				DAVINCI_MCBSP_SPCR_RINTM(3) |
				DAVINCI_MCBSP_SPCR_XINTM(3) |
				DAVINCI_MCBSP_SPCR_FREE);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_RCR_REG,
				DAVINCI_MCBSP_RCR_RFRLEN1(1) |
				DAVINCI_MCBSP_RCR_RDATDLY(1));
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_XCR_REG,
				DAVINCI_MCBSP_XCR_XFRLEN1(1) |
				DAVINCI_MCBSP_XCR_XDATDLY(1) |
				DAVINCI_MCBSP_XCR_XFIG);

	i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SRGR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_SRGR_FWID(snd_interval_value(i) - 1), 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SRGR_REG, w);

	i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_FRAME_BITS);
	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SRGR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_SRGR_FPER(snd_interval_value(i) - 1), 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SRGR_REG, w);

	/* Determine xfer data type */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		dma_params->data_type = 1;
		mcbsp_word_length = DAVINCI_MCBSP_WORD_8;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		dma_params->data_type = 2;
		mcbsp_word_length = DAVINCI_MCBSP_WORD_16;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		dma_params->data_type = 4;
		mcbsp_word_length = DAVINCI_MCBSP_WORD_32;
		break;
	default:
		printk(KERN_WARNING "davinci-i2s: unsupported PCM format");
		return -EINVAL;
	}

	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_RCR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_RCR_RWDLEN1(mcbsp_word_length) |
		       DAVINCI_MCBSP_RCR_RWDLEN2(mcbsp_word_length), 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_RCR_REG, w);

	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_XCR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_XCR_XWDLEN1(mcbsp_word_length) |
		       DAVINCI_MCBSP_XCR_XWDLEN2(mcbsp_word_length), 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_XCR_REG, w);

	return 0;
}

static int davinci_i2s_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		davinci_mcbsp_start(substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		davinci_mcbsp_stop(substream);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int davinci_i2s_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_machine *machine = socdev->machine;
	struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[pdev->id].cpu_dai;
	struct davinci_mcbsp_dev *dev;
	struct resource *mem, *ioarea;
	struct evm_snd_platform_data *pdata;
	int ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	ioarea = request_mem_region(mem->start, (mem->end - mem->start) + 1,
				    pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "McBSP region already claimed\n");
		return -EBUSY;
	}

	dev = kzalloc(sizeof(struct davinci_mcbsp_dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err_release_region;
	}

	cpu_dai->private_data = dev;

	dev->clk = clk_get(&pdev->dev, "McBSPCLK");
	if (IS_ERR(dev->clk)) {
		ret = -ENODEV;
		goto err_free_mem;
	}
	clk_enable(dev->clk);

	dev->base = (void __iomem *)IO_ADDRESS(mem->start);
	pdata = pdev->dev.platform_data;

	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK] = &davinci_i2s_pcm_out;
	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK]->channel = pdata->tx_dma_ch;
	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK]->dma_addr =
	    (dma_addr_t)(io_v2p(dev->base) + DAVINCI_MCBSP_DXR_REG);

	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE] = &davinci_i2s_pcm_in;
	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE]->channel = pdata->rx_dma_ch;
	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE]->dma_addr =
	    (dma_addr_t)(io_v2p(dev->base) + DAVINCI_MCBSP_DRR_REG);

	return 0;

err_free_mem:
	kfree(dev);
err_release_region:
	release_mem_region(mem->start, (mem->end - mem->start) + 1);

	return ret;
}

static void davinci_i2s_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_machine *machine = socdev->machine;
	struct snd_soc_cpu_dai *cpu_dai = machine->dai_link[pdev->id].cpu_dai;
	struct davinci_mcbsp_dev *dev = cpu_dai->private_data;
	struct resource *mem;

	clk_disable(dev->clk);
	clk_put(dev->clk);
	dev->clk = NULL;

	kfree(dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, (mem->end - mem->start) + 1);
}

#define DAVINCI_I2S_RATES	SNDRV_PCM_RATE_8000_96000

struct snd_soc_cpu_dai davinci_i2s_dai = {
	.name = "davinci-i2s",
	.id = 0,
	.type = SND_SOC_DAI_I2S,
	.probe = davinci_i2s_probe,
	.remove = davinci_i2s_remove,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = DAVINCI_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = DAVINCI_I2S_RATES,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,},
	.ops = {
		.startup = davinci_i2s_startup,
		.trigger = davinci_i2s_trigger,
		.hw_params = davinci_i2s_hw_params,},
	.dai_ops = {
		.set_fmt = davinci_i2s_set_dai_fmt,
	},
};
EXPORT_SYMBOL_GPL(davinci_i2s_dai);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI I2S (McBSP) SoC Interface");
MODULE_LICENSE("GPL");
