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

#include <mach/asp.h>

#include "davinci-pcm.h"


/*
 * NOTE:  terminology here is confusing.
 *
 *  - This driver supports the "Audio Serial Port" (ASP),
 *    found on dm6446, dm355, and other DaVinci chips.
 *
 *  - But it labels it a "Multi-channel Buffered Serial Port"
 *    (McBSP) as on older chips like the dm642 ... which was
 *    backward-compatible, possibly explaining that confusion.
 *
 *  - OMAP chips have a controller called McBSP, which is
 *    incompatible with the DaVinci flavor of McBSP.
 *
 *  - Newer DaVinci chips have a controller called McASP,
 *    incompatible with ASP and with either McBSP.
 *
 * In short:  this uses ASP to implement I2S, not McBSP.
 * And it won't be the only DaVinci implemention of I2S.
 */
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
#define DAVINCI_MCBSP_RCR_RFIG		(1 << 18)
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

enum {
	DAVINCI_MCBSP_WORD_8 = 0,
	DAVINCI_MCBSP_WORD_12,
	DAVINCI_MCBSP_WORD_16,
	DAVINCI_MCBSP_WORD_20,
	DAVINCI_MCBSP_WORD_24,
	DAVINCI_MCBSP_WORD_32,
};

struct davinci_mcbsp_dev {
	/*
	 * dma_params must be first because rtd->dai->cpu_dai->private_data
	 * is cast to a pointer of an array of struct davinci_pcm_dma_params in
	 * davinci_pcm_open.
	 */
	struct davinci_pcm_dma_params	dma_params[2];
	void __iomem			*base;
#define MOD_DSP_A	0
#define MOD_DSP_B	1
	int				mode;
	u32				pcr;
	struct clk			*clk;
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

static void toggle_clock(struct davinci_mcbsp_dev *dev, int playback)
{
	u32 m = playback ? DAVINCI_MCBSP_PCR_CLKXP : DAVINCI_MCBSP_PCR_CLKRP;
	/* The clock needs to toggle to complete reset.
	 * So, fake it by toggling the clk polarity.
	 */
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, dev->pcr ^ m);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, dev->pcr);
}

static void davinci_mcbsp_start(struct davinci_mcbsp_dev *dev,
		struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_platform *platform = socdev->card->platform;
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	u32 spcr;
	u32 mask = playback ? DAVINCI_MCBSP_SPCR_XRST : DAVINCI_MCBSP_SPCR_RRST;
	spcr = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	if (spcr & mask) {
		/* start off disabled */
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG,
				spcr & ~mask);
		toggle_clock(dev, playback);
	}
	if (dev->pcr & (DAVINCI_MCBSP_PCR_FSXM | DAVINCI_MCBSP_PCR_FSRM |
			DAVINCI_MCBSP_PCR_CLKXM | DAVINCI_MCBSP_PCR_CLKRM)) {
		/* Start the sample generator */
		spcr |= DAVINCI_MCBSP_SPCR_GRST;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
	}

	if (playback) {
		/* Stop the DMA to avoid data loss */
		/* while the transmitter is out of reset to handle XSYNCERR */
		if (platform->pcm_ops->trigger) {
			int ret = platform->pcm_ops->trigger(substream,
				SNDRV_PCM_TRIGGER_STOP);
			if (ret < 0)
				printk(KERN_DEBUG "Playback DMA stop failed\n");
		}

		/* Enable the transmitter */
		spcr = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
		spcr |= DAVINCI_MCBSP_SPCR_XRST;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);

		/* wait for any unexpected frame sync error to occur */
		udelay(100);

		/* Disable the transmitter to clear any outstanding XSYNCERR */
		spcr = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
		spcr &= ~DAVINCI_MCBSP_SPCR_XRST;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
		toggle_clock(dev, playback);

		/* Restart the DMA */
		if (platform->pcm_ops->trigger) {
			int ret = platform->pcm_ops->trigger(substream,
				SNDRV_PCM_TRIGGER_START);
			if (ret < 0)
				printk(KERN_DEBUG "Playback DMA start failed\n");
		}
	}

	/* Enable transmitter or receiver */
	spcr = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	spcr |= mask;

	if (dev->pcr & (DAVINCI_MCBSP_PCR_FSXM | DAVINCI_MCBSP_PCR_FSRM)) {
		/* Start frame sync */
		spcr |= DAVINCI_MCBSP_SPCR_FRST;
	}
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
}

static void davinci_mcbsp_stop(struct davinci_mcbsp_dev *dev, int playback)
{
	u32 spcr;

	/* Reset transmitter/receiver and sample rate/frame sync generators */
	spcr = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	spcr &= ~(DAVINCI_MCBSP_SPCR_GRST | DAVINCI_MCBSP_SPCR_FRST);
	spcr &= playback ? ~DAVINCI_MCBSP_SPCR_XRST : ~DAVINCI_MCBSP_SPCR_RRST;
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
	toggle_clock(dev, playback);
}

#define DEFAULT_BITPERSAMPLE	16

static int davinci_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				   unsigned int fmt)
{
	struct davinci_mcbsp_dev *dev = cpu_dai->private_data;
	unsigned int pcr;
	unsigned int srgr;
	srgr = DAVINCI_MCBSP_SRGR_FSGM |
		DAVINCI_MCBSP_SRGR_FPER(DEFAULT_BITPERSAMPLE * 2 - 1) |
		DAVINCI_MCBSP_SRGR_FWID(DEFAULT_BITPERSAMPLE - 1);

	/* set master/slave audio interface */
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

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
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
		dev->mode = MOD_DSP_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		dev->mode = MOD_DSP_B;
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
	dev->pcr = pcr;
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, pcr);
	return 0;
}

static int davinci_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = dai->private_data;
	struct davinci_pcm_dma_params *dma_params =
					&dev->dma_params[substream->stream];
	struct snd_interval *i = NULL;
	int mcbsp_word_length;
	unsigned int rcr, xcr, srgr;
	u32 spcr;

	/* general line settings */
	spcr = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		spcr |= DAVINCI_MCBSP_SPCR_RINTM(3) | DAVINCI_MCBSP_SPCR_FREE;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
	} else {
		spcr |= DAVINCI_MCBSP_SPCR_XINTM(3) | DAVINCI_MCBSP_SPCR_FREE;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
	}

	i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
	srgr = DAVINCI_MCBSP_SRGR_FSGM;
	srgr |= DAVINCI_MCBSP_SRGR_FWID(snd_interval_value(i) - 1);

	i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_FRAME_BITS);
	srgr |= DAVINCI_MCBSP_SRGR_FPER(snd_interval_value(i) - 1);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SRGR_REG, srgr);

	rcr = DAVINCI_MCBSP_RCR_RFIG;
	xcr = DAVINCI_MCBSP_XCR_XFIG;
	if (dev->mode == MOD_DSP_B) {
		rcr |= DAVINCI_MCBSP_RCR_RDATDLY(0);
		xcr |= DAVINCI_MCBSP_XCR_XDATDLY(0);
	} else {
		rcr |= DAVINCI_MCBSP_RCR_RDATDLY(1);
		xcr |= DAVINCI_MCBSP_XCR_XDATDLY(1);
	}
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

	dma_params->acnt  = dma_params->data_type;
	rcr |= DAVINCI_MCBSP_RCR_RFRLEN1(1);
	xcr |= DAVINCI_MCBSP_XCR_XFRLEN1(1);

	rcr |= DAVINCI_MCBSP_RCR_RWDLEN1(mcbsp_word_length) |
		DAVINCI_MCBSP_RCR_RWDLEN2(mcbsp_word_length);
	xcr |= DAVINCI_MCBSP_XCR_XWDLEN1(mcbsp_word_length) |
		DAVINCI_MCBSP_XCR_XWDLEN2(mcbsp_word_length);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_XCR_REG, xcr);
	else
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_RCR_REG, rcr);
	return 0;
}

static int davinci_i2s_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = dai->private_data;
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	davinci_mcbsp_stop(dev, playback);
	if ((dev->pcr & DAVINCI_MCBSP_PCR_FSXM) == 0) {
		/* codec is master */
		davinci_mcbsp_start(dev, substream);
	}
	return 0;
}

static int davinci_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = dai->private_data;
	int ret = 0;
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	if ((dev->pcr & DAVINCI_MCBSP_PCR_FSXM) == 0)
		return 0;	/* return if codec is master */

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		davinci_mcbsp_start(dev, substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		davinci_mcbsp_stop(dev, playback);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static void davinci_i2s_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = dai->private_data;
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	davinci_mcbsp_stop(dev, playback);
}

#define DAVINCI_I2S_RATES	SNDRV_PCM_RATE_8000_96000

static struct snd_soc_dai_ops davinci_i2s_dai_ops = {
	.shutdown	= davinci_i2s_shutdown,
	.prepare	= davinci_i2s_prepare,
	.trigger	= davinci_i2s_trigger,
	.hw_params	= davinci_i2s_hw_params,
	.set_fmt	= davinci_i2s_set_dai_fmt,

};

struct snd_soc_dai davinci_i2s_dai = {
	.name = "davinci-i2s",
	.id = 0,
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
	.ops = &davinci_i2s_dai_ops,

};
EXPORT_SYMBOL_GPL(davinci_i2s_dai);

static int davinci_i2s_probe(struct platform_device *pdev)
{
	struct snd_platform_data *pdata = pdev->dev.platform_data;
	struct davinci_mcbsp_dev *dev;
	struct resource *mem, *ioarea, *res;
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

	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		ret = -ENODEV;
		goto err_free_mem;
	}
	clk_enable(dev->clk);

	dev->base = (void __iomem *)IO_ADDRESS(mem->start);

	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK].dma_addr =
	    (dma_addr_t)(io_v2p(dev->base) + DAVINCI_MCBSP_DXR_REG);

	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE].dma_addr =
	    (dma_addr_t)(io_v2p(dev->base) + DAVINCI_MCBSP_DRR_REG);

	/* first TX, then RX */
	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		dev_err(&pdev->dev, "no DMA resource\n");
		ret = -ENXIO;
		goto err_free_mem;
	}
	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK].channel = res->start;

	res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!res) {
		dev_err(&pdev->dev, "no DMA resource\n");
		ret = -ENXIO;
		goto err_free_mem;
	}
	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE].channel = res->start;

	davinci_i2s_dai.private_data = dev;
	ret = snd_soc_register_dai(&davinci_i2s_dai);
	if (ret != 0)
		goto err_free_mem;

	return 0;

err_free_mem:
	kfree(dev);
err_release_region:
	release_mem_region(mem->start, (mem->end - mem->start) + 1);

	return ret;
}

static int davinci_i2s_remove(struct platform_device *pdev)
{
	struct davinci_mcbsp_dev *dev = davinci_i2s_dai.private_data;
	struct resource *mem;

	snd_soc_unregister_dai(&davinci_i2s_dai);
	clk_disable(dev->clk);
	clk_put(dev->clk);
	dev->clk = NULL;
	kfree(dev);
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, (mem->end - mem->start) + 1);

	return 0;
}

static struct platform_driver davinci_mcbsp_driver = {
	.probe		= davinci_i2s_probe,
	.remove		= davinci_i2s_remove,
	.driver		= {
		.name	= "davinci-asp",
		.owner	= THIS_MODULE,
	},
};

static int __init davinci_i2s_init(void)
{
	return platform_driver_register(&davinci_mcbsp_driver);
}
module_init(davinci_i2s_init);

static void __exit davinci_i2s_exit(void)
{
	platform_driver_unregister(&davinci_mcbsp_driver);
}
module_exit(davinci_i2s_exit);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI I2S (McBSP) SoC Interface");
MODULE_LICENSE("GPL");
