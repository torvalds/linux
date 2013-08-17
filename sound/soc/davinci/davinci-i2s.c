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
#include <linux/slab.h>
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
#include "davinci-i2s.h"


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
#define DAVINCI_MCBSP_RCR_RFRLEN2(v)	((v) << 24)
#define DAVINCI_MCBSP_RCR_RPHASE	BIT(31)

#define DAVINCI_MCBSP_XCR_XWDLEN1(v)	((v) << 5)
#define DAVINCI_MCBSP_XCR_XFRLEN1(v)	((v) << 8)
#define DAVINCI_MCBSP_XCR_XDATDLY(v)	((v) << 16)
#define DAVINCI_MCBSP_XCR_XFIG		(1 << 18)
#define DAVINCI_MCBSP_XCR_XWDLEN2(v)	((v) << 21)
#define DAVINCI_MCBSP_XCR_XFRLEN2(v)	((v) << 24)
#define DAVINCI_MCBSP_XCR_XPHASE	BIT(31)

#define DAVINCI_MCBSP_SRGR_FWID(v)	((v) << 8)
#define DAVINCI_MCBSP_SRGR_FPER(v)	((v) << 16)
#define DAVINCI_MCBSP_SRGR_FSGM		(1 << 28)
#define DAVINCI_MCBSP_SRGR_CLKSM	BIT(29)

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

static const unsigned char data_type[SNDRV_PCM_FORMAT_S32_LE + 1] = {
	[SNDRV_PCM_FORMAT_S8]		= 1,
	[SNDRV_PCM_FORMAT_S16_LE]	= 2,
	[SNDRV_PCM_FORMAT_S32_LE]	= 4,
};

static const unsigned char asp_word_length[SNDRV_PCM_FORMAT_S32_LE + 1] = {
	[SNDRV_PCM_FORMAT_S8]		= DAVINCI_MCBSP_WORD_8,
	[SNDRV_PCM_FORMAT_S16_LE]	= DAVINCI_MCBSP_WORD_16,
	[SNDRV_PCM_FORMAT_S32_LE]	= DAVINCI_MCBSP_WORD_32,
};

static const unsigned char double_fmt[SNDRV_PCM_FORMAT_S32_LE + 1] = {
	[SNDRV_PCM_FORMAT_S8]		= SNDRV_PCM_FORMAT_S16_LE,
	[SNDRV_PCM_FORMAT_S16_LE]	= SNDRV_PCM_FORMAT_S32_LE,
};

struct davinci_mcbsp_dev {
	struct device *dev;
	struct davinci_pcm_dma_params	dma_params[2];
	void __iomem			*base;
#define MOD_DSP_A	0
#define MOD_DSP_B	1
	int				mode;
	u32				pcr;
	struct clk			*clk;
	/*
	 * Combining both channels into 1 element will at least double the
	 * amount of time between servicing the dma channel, increase
	 * effiency, and reduce the chance of overrun/underrun. But,
	 * it will result in the left & right channels being swapped.
	 *
	 * If relabeling the left and right channels is not possible,
	 * you may want to let the codec know to swap them back.
	 *
	 * It may allow x10 the amount of time to service dma requests,
	 * if the codec is master and is using an unnecessarily fast bit clock
	 * (ie. tlvaic23b), independent of the sample rate. So, having an
	 * entire frame at once means it can be serviced at the sample rate
	 * instead of the bit clock rate.
	 *
	 * In the now unlikely case that an underrun still
	 * occurs, both the left and right samples will be repeated
	 * so that no pops are heard, and the left and right channels
	 * won't end up being swapped because of the underrun.
	 */
	unsigned enable_channel_combine:1;

	unsigned int fmt;
	int clk_div;
	int clk_input_pin;
	bool i2s_accurate_sck;
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
	struct snd_soc_platform *platform = rtd->platform;
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
		if (platform->driver->ops->trigger) {
			int ret = platform->driver->ops->trigger(substream,
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
		if (platform->driver->ops->trigger) {
			int ret = platform->driver->ops->trigger(substream,
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
	struct davinci_mcbsp_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int pcr;
	unsigned int srgr;
	bool inv_fs = false;
	/* Attention srgr is updated by hw_params! */
	srgr = DAVINCI_MCBSP_SRGR_FSGM |
		DAVINCI_MCBSP_SRGR_FPER(DEFAULT_BITPERSAMPLE * 2 - 1) |
		DAVINCI_MCBSP_SRGR_FWID(DEFAULT_BITPERSAMPLE - 1);

	dev->fmt = fmt;
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
		pcr = DAVINCI_MCBSP_PCR_FSRM | DAVINCI_MCBSP_PCR_FSXM;
		/*
		 * Selection of the clock input pin that is the
		 * input for the Sample Rate Generator.
		 * McBSP FSR and FSX are driven by the Sample Rate
		 * Generator.
		 */
		switch (dev->clk_input_pin) {
		case MCBSP_CLKS:
			pcr |= DAVINCI_MCBSP_PCR_CLKXM |
				DAVINCI_MCBSP_PCR_CLKRM;
			break;
		case MCBSP_CLKR:
			pcr |= DAVINCI_MCBSP_PCR_SCLKME;
			break;
		default:
			dev_err(dev->dev, "bad clk_input_pin\n");
			return -EINVAL;
		}

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
		inv_fs = true;
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
	if (inv_fs == true)
		pcr ^= (DAVINCI_MCBSP_PCR_FSXP | DAVINCI_MCBSP_PCR_FSRP);
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SRGR_REG, srgr);
	dev->pcr = pcr;
	davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_PCR_REG, pcr);
	return 0;
}

static int davinci_i2s_dai_set_clkdiv(struct snd_soc_dai *cpu_dai,
				int div_id, int div)
{
	struct davinci_mcbsp_dev *dev = snd_soc_dai_get_drvdata(cpu_dai);

	if (div_id != DAVINCI_MCBSP_CLKGDV)
		return -ENODEV;

	dev->clk_div = div;
	return 0;
}

static int davinci_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = snd_soc_dai_get_drvdata(dai);
	struct davinci_pcm_dma_params *dma_params =
					&dev->dma_params[substream->stream];
	struct snd_interval *i = NULL;
	int mcbsp_word_length, master;
	unsigned int rcr, xcr, srgr, clk_div, freq, framesize;
	u32 spcr;
	snd_pcm_format_t fmt;
	unsigned element_cnt = 1;

	/* general line settings */
	spcr = davinci_mcbsp_read_reg(dev, DAVINCI_MCBSP_SPCR_REG);
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		spcr |= DAVINCI_MCBSP_SPCR_RINTM(3) | DAVINCI_MCBSP_SPCR_FREE;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
	} else {
		spcr |= DAVINCI_MCBSP_SPCR_XINTM(3) | DAVINCI_MCBSP_SPCR_FREE;
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_SPCR_REG, spcr);
	}

	master = dev->fmt & SND_SOC_DAIFMT_MASTER_MASK;
	fmt = params_format(params);
	mcbsp_word_length = asp_word_length[fmt];

	switch (master) {
	case SND_SOC_DAIFMT_CBS_CFS:
		freq = clk_get_rate(dev->clk);
		srgr = DAVINCI_MCBSP_SRGR_FSGM |
		       DAVINCI_MCBSP_SRGR_CLKSM;
		srgr |= DAVINCI_MCBSP_SRGR_FWID(mcbsp_word_length *
						8 - 1);
		if (dev->i2s_accurate_sck) {
			clk_div = 256;
			do {
				framesize = (freq / (--clk_div)) /
				params->rate_num *
					params->rate_den;
			} while (((framesize < 33) || (framesize > 4095)) &&
				 (clk_div));
			clk_div--;
			srgr |= DAVINCI_MCBSP_SRGR_FPER(framesize - 1);
		} else {
			/* symmetric waveforms */
			clk_div = freq / (mcbsp_word_length * 16) /
				  params->rate_num * params->rate_den;
			srgr |= DAVINCI_MCBSP_SRGR_FPER(mcbsp_word_length *
							16 - 1);
		}
		clk_div &= 0xFF;
		srgr |= clk_div;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		srgr = DAVINCI_MCBSP_SRGR_FSGM;
		clk_div = dev->clk_div - 1;
		srgr |= DAVINCI_MCBSP_SRGR_FWID(mcbsp_word_length * 8 - 1);
		srgr |= DAVINCI_MCBSP_SRGR_FPER(mcbsp_word_length * 16 - 1);
		clk_div &= 0xFF;
		srgr |= clk_div;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		/* Clock and frame sync given from external sources */
		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_SAMPLE_BITS);
		srgr = DAVINCI_MCBSP_SRGR_FSGM;
		srgr |= DAVINCI_MCBSP_SRGR_FWID(snd_interval_value(i) - 1);
		pr_debug("%s - %d  FWID set: re-read srgr = %X\n",
			__func__, __LINE__, snd_interval_value(i) - 1);

		i = hw_param_interval(params, SNDRV_PCM_HW_PARAM_FRAME_BITS);
		srgr |= DAVINCI_MCBSP_SRGR_FPER(snd_interval_value(i) - 1);
		break;
	default:
		return -EINVAL;
	}
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
	fmt = params_format(params);
	if ((fmt > SNDRV_PCM_FORMAT_S32_LE) || !data_type[fmt]) {
		printk(KERN_WARNING "davinci-i2s: unsupported PCM format\n");
		return -EINVAL;
	}

	if (params_channels(params) == 2) {
		element_cnt = 2;
		if (double_fmt[fmt] && dev->enable_channel_combine) {
			element_cnt = 1;
			fmt = double_fmt[fmt];
		}
		switch (master) {
		case SND_SOC_DAIFMT_CBS_CFS:
		case SND_SOC_DAIFMT_CBS_CFM:
			rcr |= DAVINCI_MCBSP_RCR_RFRLEN2(0);
			xcr |= DAVINCI_MCBSP_XCR_XFRLEN2(0);
			rcr |= DAVINCI_MCBSP_RCR_RPHASE;
			xcr |= DAVINCI_MCBSP_XCR_XPHASE;
			break;
		case SND_SOC_DAIFMT_CBM_CFM:
		case SND_SOC_DAIFMT_CBM_CFS:
			rcr |= DAVINCI_MCBSP_RCR_RFRLEN2(element_cnt - 1);
			xcr |= DAVINCI_MCBSP_XCR_XFRLEN2(element_cnt - 1);
			break;
		default:
			return -EINVAL;
		}
	}
	dma_params->acnt = dma_params->data_type = data_type[fmt];
	dma_params->fifo_level = 0;
	mcbsp_word_length = asp_word_length[fmt];

	switch (master) {
	case SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		rcr |= DAVINCI_MCBSP_RCR_RFRLEN1(0);
		xcr |= DAVINCI_MCBSP_XCR_XFRLEN1(0);
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
		rcr |= DAVINCI_MCBSP_RCR_RFRLEN1(element_cnt - 1);
		xcr |= DAVINCI_MCBSP_XCR_XFRLEN1(element_cnt - 1);
		break;
	default:
		return -EINVAL;
	}

	rcr |= DAVINCI_MCBSP_RCR_RWDLEN1(mcbsp_word_length) |
		DAVINCI_MCBSP_RCR_RWDLEN2(mcbsp_word_length);
	xcr |= DAVINCI_MCBSP_XCR_XWDLEN1(mcbsp_word_length) |
		DAVINCI_MCBSP_XCR_XWDLEN2(mcbsp_word_length);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_XCR_REG, xcr);
	else
		davinci_mcbsp_write_reg(dev, DAVINCI_MCBSP_RCR_REG, rcr);

	pr_debug("%s - %d  srgr=%X\n", __func__, __LINE__, srgr);
	pr_debug("%s - %d  xcr=%X\n", __func__, __LINE__, xcr);
	pr_debug("%s - %d  rcr=%X\n", __func__, __LINE__, rcr);
	return 0;
}

static int davinci_i2s_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = snd_soc_dai_get_drvdata(dai);
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	davinci_mcbsp_stop(dev, playback);
	return 0;
}

static int davinci_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			       struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = snd_soc_dai_get_drvdata(dai);
	int ret = 0;
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

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

static int davinci_i2s_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_set_dma_data(dai, substream, dev->dma_params);
	return 0;
}

static void davinci_i2s_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct davinci_mcbsp_dev *dev = snd_soc_dai_get_drvdata(dai);
	int playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	davinci_mcbsp_stop(dev, playback);
}

#define DAVINCI_I2S_RATES	SNDRV_PCM_RATE_8000_96000

static const struct snd_soc_dai_ops davinci_i2s_dai_ops = {
	.startup	= davinci_i2s_startup,
	.shutdown	= davinci_i2s_shutdown,
	.prepare	= davinci_i2s_prepare,
	.trigger	= davinci_i2s_trigger,
	.hw_params	= davinci_i2s_hw_params,
	.set_fmt	= davinci_i2s_set_dai_fmt,
	.set_clkdiv	= davinci_i2s_dai_set_clkdiv,

};

static struct snd_soc_dai_driver davinci_i2s_dai = {
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

static int davinci_i2s_probe(struct platform_device *pdev)
{
	struct snd_platform_data *pdata = pdev->dev.platform_data;
	struct davinci_mcbsp_dev *dev;
	struct resource *mem, *ioarea, *res;
	enum dma_event_q asp_chan_q = EVENTQ_0;
	enum dma_event_q ram_chan_q = EVENTQ_1;
	int ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	ioarea = devm_request_mem_region(&pdev->dev, mem->start,
					 resource_size(mem),
					 pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "McBSP region already claimed\n");
		return -EBUSY;
	}

	dev = devm_kzalloc(&pdev->dev, sizeof(struct davinci_mcbsp_dev),
			   GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	if (pdata) {
		dev->enable_channel_combine = pdata->enable_channel_combine;
		dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK].sram_size =
			pdata->sram_size_playback;
		dev->dma_params[SNDRV_PCM_STREAM_CAPTURE].sram_size =
			pdata->sram_size_capture;
		dev->clk_input_pin = pdata->clk_input_pin;
		dev->i2s_accurate_sck = pdata->i2s_accurate_sck;
		asp_chan_q = pdata->asp_chan_q;
		ram_chan_q = pdata->ram_chan_q;
	}

	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK].asp_chan_q	= asp_chan_q;
	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK].ram_chan_q	= ram_chan_q;
	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE].asp_chan_q	= asp_chan_q;
	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE].ram_chan_q	= ram_chan_q;

	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk))
		return -ENODEV;
	clk_enable(dev->clk);

	dev->base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (!dev->base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_release_clk;
	}

	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK].dma_addr =
	    (dma_addr_t)(mem->start + DAVINCI_MCBSP_DXR_REG);

	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE].dma_addr =
	    (dma_addr_t)(mem->start + DAVINCI_MCBSP_DRR_REG);

	/* first TX, then RX */
	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		dev_err(&pdev->dev, "no DMA resource\n");
		ret = -ENXIO;
		goto err_release_clk;
	}
	dev->dma_params[SNDRV_PCM_STREAM_PLAYBACK].channel = res->start;

	res = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (!res) {
		dev_err(&pdev->dev, "no DMA resource\n");
		ret = -ENXIO;
		goto err_release_clk;
	}
	dev->dma_params[SNDRV_PCM_STREAM_CAPTURE].channel = res->start;
	dev->dev = &pdev->dev;

	dev_set_drvdata(&pdev->dev, dev);

	ret = snd_soc_register_dai(&pdev->dev, &davinci_i2s_dai);
	if (ret != 0)
		goto err_release_clk;

	return 0;

err_release_clk:
	clk_disable(dev->clk);
	clk_put(dev->clk);
	return ret;
}

static int davinci_i2s_remove(struct platform_device *pdev)
{
	struct davinci_mcbsp_dev *dev = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_dai(&pdev->dev);
	clk_disable(dev->clk);
	clk_put(dev->clk);
	dev->clk = NULL;

	return 0;
}

static struct platform_driver davinci_mcbsp_driver = {
	.probe		= davinci_i2s_probe,
	.remove		= davinci_i2s_remove,
	.driver		= {
		.name	= "davinci-mcbsp",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(davinci_mcbsp_driver);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI I2S (McBSP) SoC Interface");
MODULE_LICENSE("GPL");
