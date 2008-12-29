/*
 * ALSA SoC I2S (McBSP) Audio Layer for TI DAVINCI processor
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
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
#define DAVINCI_MCBSP_PCR_SCLKME	(1 << 7)
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
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_platform *platform = socdev->card->platform;
	u32 w;
	int ret;

	/* Start the sample generator and enable transmitter/receiver */
	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_GRST, 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* Stop the DMA to avoid data loss */
		/* while the transmitter is out of reset to handle XSYNCERR */
		if (platform->pcm_ops->trigger) {
			ret = platform->pcm_ops->trigger(substream,
				SNDRV_PCM_TRIGGER_STOP);
			if (ret < 0)
				printk(KERN_DEBUG "Playback DMA stop failed\n");
		}

		/* Enable the transmitter */
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_XRST, 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);

		/* wait for any unexpected frame sync error to occur */
		udelay(100);

		/* Disable the transmitter to clear any outstanding XSYNCERR */
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_XRST, 0);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);

		/* Restart the DMA */
		if (platform->pcm_ops->trigger) {
			ret = platform->pcm_ops->trigger(substream,
				SNDRV_PCM_TRIGGER_START);
			if (ret < 0)
				printk(KERN_DEBUG "Playback DMA start failed\n");
		}
		/* Enable the transmitter */
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_XRST, 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);

	} else {

		/* Enable the reciever */
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_SPCR_RRST, 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);
	}


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

static int davinci_i2s_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	struct davinci_mcbsp_dev *dev = rtd->dai->cpu_dai->private_data;

	cpu_dai->dma_data = dev->dma_params[substream->stream];

	return 0;
}

#define DEFAULT_BITPERSAMPLE	16

static int davinci_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				   unsigned int fmt)
{
	struct davinci_mcbsp_dev *dev = cpu_dai->private_data;
	unsigned int pcr;
	unsigned int srgr;
	unsigned int rcr;
	unsigned int xcr;
	srgr = DAVINCI_MCBSP_SRGR_FSGM |
		DAVINCI_MCBSP_SRGR_FPER(DEFAULT_BITPERSAMPLE * 2 - 1) |
		DAVINCI_MCBSP_SRGR_FWID(DEFAULT_BITPERSAMPLE - 1);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* cpu is master */
		pcr = DAVINCI_MCBSP_PCR_FSXM |
			DAVINCI_MCBSP_PCR_FSRM |
			DAVINCI_MCBSP_PCR_CLKXM |
			DAVINCI_MCBSP_PCR_CLKRM;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		/* McBSP CLKR pin is the input for the Sample Rate Generator.
		 * McBSP FSR and FSX are driven by the Sample Rate Generator. */
		pcr = DAVINCI_MCBSP_PCR_SCLKME |
			DAVINCI_MCBSP_PCR_FSXM |
			DAVINCI_MCBSP_PCR_FSRM;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* codec is master */
		pcr = 0;
		break;
	default:
		printk(KERN_ERR "%s:bad master\n", __func__);
		return -EINVAL;
	}

	rcr = DAVINCI_MCBSP_RCR_RFRLEN1(1);
	xcr = DAVINCI_MCBSP_XCR_XFIG | DAVINCI_MCBSP_XCR_XFRLEN1(1);
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		break;
	case SND_SOC_DAIFMT_I2S:
		/* Davinci doesn't support TRUE I2S, but some codecs will have
		 * the left and right channels contiguous. This allows
		 * dsp_a mode to be used with an inverted normal frame clk.
		 * If your codec is master and does not have contiguous
		 * channels, then you will have sound on only one channel.
		 * Try using a different mode, or codec as slave.
		 *
		 * The TLV320AIC33 is an example of a codec where this works.
		 * It has a variable bit clock frequency allowing it to have
		 * valid data on every bit clock.
		 *
		 * The TLV320AIC23 is an example of a codec where this does not
		 * work. It has a fixed bit clock frequency with progressively
		 * more empty bit clock slots between channels as the sample
		 * rate is lowered.
		 */
		fmt ^= SND_SOC_DAIFMT_NB_IF;
	case SND_SOC_DAIFMT_DSP_A:
		rcr |= DAVINCI_MCBSP_RCR_RDATDLY(1);
		xcr |= DAVINCI_MCBSP_XCR_XDATDLY(1);
		break;
	default:
		printk(KERN_ERR "%s:bad format\n", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		/* CLKRP Receive clock polarity,
		 *	1 - sampled on rising edge of CLKR
		 *	valid on rising edge
		 * CLKXP Transmit clock polarity,
		 *	1 - clocked on falling edge of CLKX
		 *	valid on rising edge
		 * FSRP  Receive frame sync pol, 0 - active high
		 * FSXP  Transmit frame sync pol, 0 - active high
		 */
		pcr |= (DAVINCI_MCBSP_PCR_CLKXP | DAVINCI_MCBSP_PCR_CLKRP);
		break;
	case SND_SOC_DAIFMT_IB_IF:
		/* CLKRP Receive clock polarity,
		 *	0 - sampled on falling edge of CLKR
		 *	valid on falling edge
		 * CLKXP Transmit clock polarity,
		 *	0 - clocked on rising edge of CLKX
		 *	valid on falling edge
		 * FSRP  Receive frame sync pol, 1 - active low
		 * FSXP  Transmit frame sync pol, 1 - active low
		 */
		pcr |= (DAVINCI_MCBSP_PCR_FSXP | DAVINCI_MCBSP_PCR_FSRP);
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* CLKRP Receive clock polarity,
		 *	1 - sampled on rising edge of CLKR
		 *	valid on rising edge
		 * CLKXP Transmit clock polarity,
		 *	1 - clocked on falling edge of CLKX
		 *	valid on rising edge
		 * FSRP  Receive frame sync pol, 1 - active low
		 * FSXP  Transmit frame sync pol, 1 - active low
		 */
		pcr |= (DAVINCI_MCBSP_PCR_CLKXP | DAVINCI_MCBSP_PCR_CLKRP |
			DAVINCI_MCBSP_PCR_FSXP | DAVINCI_MCBSP_PCR_FSRP);
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* CLKRP Receive clock polarity,
		 *	0 - sampled on falling edge of CLKR
		 *	valid on falling edge
		 * CLKXP Transmit clock polarity,
		 *	0 - clocked on rising edge of CLKX
		 *	valid on falling edge
		 * FSRP  Receive frame sync pol, 0 - active high
		 * FSXP  Transmit frame sync pol, 0 - active high
		 */
		break;
	default:
		return -EINVAL;
	}
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SRGR_REG, srgr);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, pcr);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_RCR_REG, rcr);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_XCR_REG, xcr);
	return 0;
}

static int davinci_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct davinci_pcm_dma_params *dma_params = rtd->dai->cpu_dai->dma_data;
	struct davinci_mcbsp_dev *dev = rtd->dai->cpu_dai->private_data;
	struct snd_interval *i = NULL;
	int mcbsp_word_length;
	u32 w;

	/* general line settings */
	w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		w |= DAVINCI_MCBSP_SPCR_RINTM(3) | DAVINCI_MCBSP_SPCR_FREE;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);
	} else {
		w |= DAVINCI_MCBSP_SPCR_XINTM(3) | DAVINCI_MCBSP_SPCR_FREE;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, w);
	}

	i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
	w = DAVINCI_MCBSP_SRGR_FSGM;
	MOD_REG_BIT(w, DAVINCI_MCBSP_SRGR_FWID(snd_interval_value(i) - 1), 1);

	i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_FRAME_BITS);
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
		printk(KERN_WARNING "davinci-i2s: unsupported PCM format\n");
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_RCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_RCR_RWDLEN1(mcbsp_word_length) |
			       DAVINCI_MCBSP_RCR_RWDLEN2(mcbsp_word_length), 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_RCR_REG, w);

	} else {
		w = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_XCR_REG);
		MOD_REG_BIT(w, DAVINCI_MCBSP_XCR_XWDLEN1(mcbsp_word_length) |
			       DAVINCI_MCBSP_XCR_XWDLEN2(mcbsp_word_length), 1);
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_XCR_REG, w);

	}
	return 0;
}

static int davinci_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
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

static int davinci_i2s_probe(struct platform_device *pdev,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_card *card = socdev->card;
	struct snd_soc_dai *cpu_dai = card->dai_link[pdev->id].cpu_dai;
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

static void davinci_i2s_remove(struct platform_device *pdev,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_card *card = socdev->card;
	struct snd_soc_dai *cpu_dai = card->dai_link[pdev->id].cpu_dai;
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

struct snd_soc_dai davinci_i2s_dai = {
	.name = "davinci-i2s",
	.id = 0,
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
		.hw_params = davinci_i2s_hw_params,
		.set_fmt = davinci_i2s_set_dai_fmt,
	},
};
EXPORT_SYMBOL_GPL(davinci_i2s_dai);

static int __init davinci_i2s_init(void)
{
	return snd_soc_register_dai(&davinci_i2s_dai);
}
module_init(davinci_i2s_init);

static void __exit davinci_i2s_exit(void)
{
	snd_soc_unregister_dai(&davinci_i2s_dai);
}
module_exit(davinci_i2s_exit);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI I2S (McBSP) SoC Interface");
MODULE_LICENSE("GPL");
