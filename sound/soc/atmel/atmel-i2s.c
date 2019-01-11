/*
 * Driver for Atmel I2S controller
 *
 * Copyright (C) 2015 Atmel Corporation
 *
 * Author: Cyrille Pitchen <cyrille.pitchen@atmel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#define ATMEL_I2SC_MAX_TDM_CHANNELS	8

/*
 * ---- I2S Controller Register map ----
 */
#define ATMEL_I2SC_CR		0x0000	/* Control Register */
#define ATMEL_I2SC_MR		0x0004	/* Mode Register */
#define ATMEL_I2SC_SR		0x0008	/* Status Register */
#define ATMEL_I2SC_SCR		0x000c	/* Status Clear Register */
#define ATMEL_I2SC_SSR		0x0010	/* Status Set Register */
#define ATMEL_I2SC_IER		0x0014	/* Interrupt Enable Register */
#define ATMEL_I2SC_IDR		0x0018	/* Interrupt Disable Register */
#define ATMEL_I2SC_IMR		0x001c	/* Interrupt Mask Register */
#define ATMEL_I2SC_RHR		0x0020	/* Receiver Holding Register */
#define ATMEL_I2SC_THR		0x0024	/* Transmitter Holding Register */
#define ATMEL_I2SC_VERSION	0x0028	/* Version Register */

/*
 * ---- Control Register (Write-only) ----
 */
#define ATMEL_I2SC_CR_RXEN	BIT(0)	/* Receiver Enable */
#define ATMEL_I2SC_CR_RXDIS	BIT(1)	/* Receiver Disable */
#define ATMEL_I2SC_CR_CKEN	BIT(2)	/* Clock Enable */
#define ATMEL_I2SC_CR_CKDIS	BIT(3)	/* Clock Disable */
#define ATMEL_I2SC_CR_TXEN	BIT(4)	/* Transmitter Enable */
#define ATMEL_I2SC_CR_TXDIS	BIT(5)	/* Transmitter Disable */
#define ATMEL_I2SC_CR_SWRST	BIT(7)	/* Software Reset */

/*
 * ---- Mode Register (Read/Write) ----
 */
#define ATMEL_I2SC_MR_MODE_MASK		GENMASK(0, 0)
#define ATMEL_I2SC_MR_MODE_SLAVE	(0 << 0)
#define ATMEL_I2SC_MR_MODE_MASTER	(1 << 0)

#define ATMEL_I2SC_MR_DATALENGTH_MASK		GENMASK(4, 2)
#define ATMEL_I2SC_MR_DATALENGTH_32_BITS	(0 << 2)
#define ATMEL_I2SC_MR_DATALENGTH_24_BITS	(1 << 2)
#define ATMEL_I2SC_MR_DATALENGTH_20_BITS	(2 << 2)
#define ATMEL_I2SC_MR_DATALENGTH_18_BITS	(3 << 2)
#define ATMEL_I2SC_MR_DATALENGTH_16_BITS	(4 << 2)
#define ATMEL_I2SC_MR_DATALENGTH_16_BITS_COMPACT	(5 << 2)
#define ATMEL_I2SC_MR_DATALENGTH_8_BITS		(6 << 2)
#define ATMEL_I2SC_MR_DATALENGTH_8_BITS_COMPACT	(7 << 2)

#define ATMEL_I2SC_MR_FORMAT_MASK	GENMASK(7, 6)
#define ATMEL_I2SC_MR_FORMAT_I2S	(0 << 6)
#define ATMEL_I2SC_MR_FORMAT_LJ		(1 << 6)  /* Left Justified */
#define ATMEL_I2SC_MR_FORMAT_TDM	(2 << 6)
#define ATMEL_I2SC_MR_FORMAT_TDMLJ	(3 << 6)

/* Left audio samples duplicated to right audio channel */
#define ATMEL_I2SC_MR_RXMONO		BIT(8)

/* Receiver uses one DMA channel ... */
#define ATMEL_I2SC_MR_RXDMA_MASK	GENMASK(9, 9)
#define ATMEL_I2SC_MR_RXDMA_SINGLE	(0 << 9)  /* for all audio channels */
#define ATMEL_I2SC_MR_RXDMA_MULTIPLE	(1 << 9)  /* per audio channel */

/* I2SDO output of I2SC is internally connected to I2SDI input */
#define ATMEL_I2SC_MR_RXLOOP		BIT(10)

/* Left audio samples duplicated to right audio channel */
#define ATMEL_I2SC_MR_TXMONO		BIT(12)

/* Transmitter uses one DMA channel ... */
#define ATMEL_I2SC_MR_TXDMA_MASK	GENMASK(13, 13)
#define ATMEL_I2SC_MR_TXDMA_SINGLE	(0 << 13)  /* for all audio channels */
#define ATMEL_I2SC_MR_TXDME_MULTIPLE	(1 << 13)  /* per audio channel */

/* x sample transmitted when underrun */
#define ATMEL_I2SC_MR_TXSAME_MASK	GENMASK(14, 14)
#define ATMEL_I2SC_MR_TXSAME_ZERO	(0 << 14)  /* Zero sample */
#define ATMEL_I2SC_MR_TXSAME_PREVIOUS	(1 << 14)  /* Previous sample */

/* Audio Clock to I2SC Master Clock ratio */
#define ATMEL_I2SC_MR_IMCKDIV_MASK	GENMASK(21, 16)
#define ATMEL_I2SC_MR_IMCKDIV(div) \
	(((div) << 16) & ATMEL_I2SC_MR_IMCKDIV_MASK)

/* Master Clock to fs ratio */
#define ATMEL_I2SC_MR_IMCKFS_MASK	GENMASK(29, 24)
#define ATMEL_I2SC_MR_IMCKFS(fs) \
	(((fs) << 24) & ATMEL_I2SC_MR_IMCKFS_MASK)

/* Master Clock mode */
#define ATMEL_I2SC_MR_IMCKMODE_MASK	GENMASK(30, 30)
/* 0: No master clock generated (selected clock drives I2SCK pin) */
#define ATMEL_I2SC_MR_IMCKMODE_I2SCK	(0 << 30)
/* 1: master clock generated (internally generated clock drives I2SMCK pin) */
#define ATMEL_I2SC_MR_IMCKMODE_I2SMCK	(1 << 30)

/* Slot Width */
/* 0: slot is 32 bits wide for DATALENGTH = 18/20/24 bits. */
/* 1: slot is 24 bits wide for DATALENGTH = 18/20/24 bits. */
#define ATMEL_I2SC_MR_IWS		BIT(31)

/*
 * ---- Status Registers ----
 */
#define ATMEL_I2SC_SR_RXEN	BIT(0)	/* Receiver Enabled */
#define ATMEL_I2SC_SR_RXRDY	BIT(1)	/* Receive Ready */
#define ATMEL_I2SC_SR_RXOR	BIT(2)	/* Receive Overrun */

#define ATMEL_I2SC_SR_TXEN	BIT(4)	/* Transmitter Enabled */
#define ATMEL_I2SC_SR_TXRDY	BIT(5)	/* Transmit Ready */
#define ATMEL_I2SC_SR_TXUR	BIT(6)	/* Transmit Underrun */

/* Receive Overrun Channel */
#define ATMEL_I2SC_SR_RXORCH_MASK	GENMASK(15, 8)
#define ATMEL_I2SC_SR_RXORCH(ch)	(1 << (((ch) & 0x7) + 8))

/* Transmit Underrun Channel */
#define ATMEL_I2SC_SR_TXURCH_MASK	GENMASK(27, 20)
#define ATMEL_I2SC_SR_TXURCH(ch)	(1 << (((ch) & 0x7) + 20))

/*
 * ---- Interrupt Enable/Disable/Mask Registers ----
 */
#define ATMEL_I2SC_INT_RXRDY	ATMEL_I2SC_SR_RXRDY
#define ATMEL_I2SC_INT_RXOR	ATMEL_I2SC_SR_RXOR
#define ATMEL_I2SC_INT_TXRDY	ATMEL_I2SC_SR_TXRDY
#define ATMEL_I2SC_INT_TXUR	ATMEL_I2SC_SR_TXUR

static const struct regmap_config atmel_i2s_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = ATMEL_I2SC_VERSION,
};

struct atmel_i2s_gck_param {
	int		fs;
	unsigned long	mck;
	int		imckdiv;
	int		imckfs;
};

#define I2S_MCK_12M288		12288000UL
#define I2S_MCK_11M2896		11289600UL

/* mck = (32 * (imckfs+1) / (imckdiv+1)) * fs */
static const struct atmel_i2s_gck_param gck_params[] = {
	/* mck = 12.288MHz */
	{  8000, I2S_MCK_12M288, 0, 47},	/* mck = 1536 fs */
	{ 16000, I2S_MCK_12M288, 1, 47},	/* mck =  768 fs */
	{ 24000, I2S_MCK_12M288, 3, 63},	/* mck =  512 fs */
	{ 32000, I2S_MCK_12M288, 3, 47},	/* mck =  384 fs */
	{ 48000, I2S_MCK_12M288, 7, 63},	/* mck =  256 fs */
	{ 64000, I2S_MCK_12M288, 7, 47},	/* mck =  192 fs */
	{ 96000, I2S_MCK_12M288, 7, 31},	/* mck =  128 fs */
	{192000, I2S_MCK_12M288, 7, 15},	/* mck =   64 fs */

	/* mck = 11.2896MHz */
	{ 11025, I2S_MCK_11M2896, 1, 63},	/* mck = 1024 fs */
	{ 22050, I2S_MCK_11M2896, 3, 63},	/* mck =  512 fs */
	{ 44100, I2S_MCK_11M2896, 7, 63},	/* mck =  256 fs */
	{ 88200, I2S_MCK_11M2896, 7, 31},	/* mck =  128 fs */
	{176400, I2S_MCK_11M2896, 7, 15},	/* mck =   64 fs */
};

struct atmel_i2s_dev;

struct atmel_i2s_caps {
	int	(*mck_init)(struct atmel_i2s_dev *, struct device_node *np);
};

struct atmel_i2s_dev {
	struct device				*dev;
	struct regmap				*regmap;
	struct clk				*pclk;
	struct clk				*gclk;
	struct clk				*aclk;
	struct snd_dmaengine_dai_dma_data	playback;
	struct snd_dmaengine_dai_dma_data	capture;
	unsigned int				fmt;
	const struct atmel_i2s_gck_param	*gck_param;
	const struct atmel_i2s_caps		*caps;
};

static irqreturn_t atmel_i2s_interrupt(int irq, void *dev_id)
{
	struct atmel_i2s_dev *dev = dev_id;
	unsigned int sr, imr, pending, ch, mask;
	irqreturn_t ret = IRQ_NONE;

	regmap_read(dev->regmap, ATMEL_I2SC_SR, &sr);
	regmap_read(dev->regmap, ATMEL_I2SC_IMR, &imr);
	pending = sr & imr;

	if (!pending)
		return IRQ_NONE;

	if (pending & ATMEL_I2SC_INT_RXOR) {
		mask = ATMEL_I2SC_SR_RXOR;

		for (ch = 0; ch < ATMEL_I2SC_MAX_TDM_CHANNELS; ++ch) {
			if (sr & ATMEL_I2SC_SR_RXORCH(ch)) {
				mask |= ATMEL_I2SC_SR_RXORCH(ch);
				dev_err(dev->dev,
					"RX overrun on channel %d\n", ch);
			}
		}
		regmap_write(dev->regmap, ATMEL_I2SC_SCR, mask);
		ret = IRQ_HANDLED;
	}

	if (pending & ATMEL_I2SC_INT_TXUR) {
		mask = ATMEL_I2SC_SR_TXUR;

		for (ch = 0; ch < ATMEL_I2SC_MAX_TDM_CHANNELS; ++ch) {
			if (sr & ATMEL_I2SC_SR_TXURCH(ch)) {
				mask |= ATMEL_I2SC_SR_TXURCH(ch);
				dev_err(dev->dev,
					"TX underrun on channel %d\n", ch);
			}
		}
		regmap_write(dev->regmap, ATMEL_I2SC_SCR, mask);
		ret = IRQ_HANDLED;
	}

	return ret;
}

#define ATMEL_I2S_RATES		SNDRV_PCM_RATE_8000_192000

#define ATMEL_I2S_FORMATS	(SNDRV_PCM_FMTBIT_S8 |		\
				 SNDRV_PCM_FMTBIT_S16_LE |	\
				 SNDRV_PCM_FMTBIT_S18_3LE |	\
				 SNDRV_PCM_FMTBIT_S20_3LE |	\
				 SNDRV_PCM_FMTBIT_S24_3LE |	\
				 SNDRV_PCM_FMTBIT_S24_LE |	\
				 SNDRV_PCM_FMTBIT_S32_LE)

static int atmel_i2s_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct atmel_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);

	dev->fmt = fmt;
	return 0;
}

static int atmel_i2s_prepare(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct atmel_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	bool is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	unsigned int rhr, sr = 0;

	if (is_playback) {
		regmap_read(dev->regmap, ATMEL_I2SC_SR, &sr);
		if (sr & ATMEL_I2SC_SR_RXRDY) {
			/*
			 * The RX Ready flag should not be set. However if here,
			 * we flush (read) the Receive Holding Register to start
			 * from a clean state.
			 */
			dev_dbg(dev->dev, "RXRDY is set\n");
			regmap_read(dev->regmap, ATMEL_I2SC_RHR, &rhr);
		}
	}

	return 0;
}

static int atmel_i2s_get_gck_param(struct atmel_i2s_dev *dev, int fs)
{
	int i, best;

	if (!dev->gclk || !dev->aclk) {
		dev_err(dev->dev, "cannot generate the I2S Master Clock\n");
		return -EINVAL;
	}

	/*
	 * Find the best possible settings to generate the I2S Master Clock
	 * from the PLL Audio.
	 */
	dev->gck_param = NULL;
	best = INT_MAX;
	for (i = 0; i < ARRAY_SIZE(gck_params); ++i) {
		const struct atmel_i2s_gck_param *gck_param = &gck_params[i];
		int val = abs(fs - gck_param->fs);

		if (val < best) {
			best = val;
			dev->gck_param = gck_param;
		}
	}

	return 0;
}

static int atmel_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct atmel_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	bool is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	unsigned int mr = 0;
	int ret;

	switch (dev->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		mr |= ATMEL_I2SC_MR_FORMAT_I2S;
		break;

	default:
		dev_err(dev->dev, "unsupported bus format\n");
		return -EINVAL;
	}

	switch (dev->fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		/* codec is slave, so cpu is master */
		mr |= ATMEL_I2SC_MR_MODE_MASTER;
		ret = atmel_i2s_get_gck_param(dev, params_rate(params));
		if (ret)
			return ret;
		break;

	case SND_SOC_DAIFMT_CBM_CFM:
		/* codec is master, so cpu is slave */
		mr |= ATMEL_I2SC_MR_MODE_SLAVE;
		dev->gck_param = NULL;
		break;

	default:
		dev_err(dev->dev, "unsupported master/slave mode\n");
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case 1:
		if (is_playback)
			mr |= ATMEL_I2SC_MR_TXMONO;
		else
			mr |= ATMEL_I2SC_MR_RXMONO;
		break;
	case 2:
		break;
	default:
		dev_err(dev->dev, "unsupported number of audio channels\n");
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		mr |= ATMEL_I2SC_MR_DATALENGTH_8_BITS;
		break;

	case SNDRV_PCM_FORMAT_S16_LE:
		mr |= ATMEL_I2SC_MR_DATALENGTH_16_BITS;
		break;

	case SNDRV_PCM_FORMAT_S18_3LE:
		mr |= ATMEL_I2SC_MR_DATALENGTH_18_BITS | ATMEL_I2SC_MR_IWS;
		break;

	case SNDRV_PCM_FORMAT_S20_3LE:
		mr |= ATMEL_I2SC_MR_DATALENGTH_20_BITS | ATMEL_I2SC_MR_IWS;
		break;

	case SNDRV_PCM_FORMAT_S24_3LE:
		mr |= ATMEL_I2SC_MR_DATALENGTH_24_BITS | ATMEL_I2SC_MR_IWS;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:
		mr |= ATMEL_I2SC_MR_DATALENGTH_24_BITS;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		mr |= ATMEL_I2SC_MR_DATALENGTH_32_BITS;
		break;

	default:
		dev_err(dev->dev, "unsupported size/endianness for audio samples\n");
		return -EINVAL;
	}

	return regmap_write(dev->regmap, ATMEL_I2SC_MR, mr);
}

static int atmel_i2s_switch_mck_generator(struct atmel_i2s_dev *dev,
					  bool enabled)
{
	unsigned int mr, mr_mask;
	unsigned long aclk_rate;
	int ret;

	mr = 0;
	mr_mask = (ATMEL_I2SC_MR_IMCKDIV_MASK |
		   ATMEL_I2SC_MR_IMCKFS_MASK |
		   ATMEL_I2SC_MR_IMCKMODE_MASK);

	if (!enabled) {
		/* Disable the I2S Master Clock generator. */
		ret = regmap_write(dev->regmap, ATMEL_I2SC_CR,
				   ATMEL_I2SC_CR_CKDIS);
		if (ret)
			return ret;

		/* Reset the I2S Master Clock generator settings. */
		ret = regmap_update_bits(dev->regmap, ATMEL_I2SC_MR,
					 mr_mask, mr);
		if (ret)
			return ret;

		/* Disable/unprepare the PMC generated clock. */
		clk_disable_unprepare(dev->gclk);

		/* Disable/unprepare the PLL audio clock. */
		clk_disable_unprepare(dev->aclk);
		return 0;
	}

	if (!dev->gck_param)
		return -EINVAL;

	aclk_rate = dev->gck_param->mck * (dev->gck_param->imckdiv + 1);

	/* Fist change the PLL audio clock frequency ... */
	ret = clk_set_rate(dev->aclk, aclk_rate);
	if (ret)
		return ret;

	/*
	 * ... then set the PMC generated clock rate to the very same frequency
	 * to set the gclk parent to aclk.
	 */
	ret = clk_set_rate(dev->gclk, aclk_rate);
	if (ret)
		return ret;

	/* Prepare and enable the PLL audio clock first ... */
	ret = clk_prepare_enable(dev->aclk);
	if (ret)
		return ret;

	/* ... then prepare and enable the PMC generated clock. */
	ret = clk_prepare_enable(dev->gclk);
	if (ret)
		return ret;

	/* Update the Mode Register to generate the I2S Master Clock. */
	mr |= ATMEL_I2SC_MR_IMCKDIV(dev->gck_param->imckdiv);
	mr |= ATMEL_I2SC_MR_IMCKFS(dev->gck_param->imckfs);
	mr |= ATMEL_I2SC_MR_IMCKMODE_I2SMCK;
	ret = regmap_update_bits(dev->regmap, ATMEL_I2SC_MR, mr_mask, mr);
	if (ret)
		return ret;

	/* Finally enable the I2S Master Clock generator. */
	return regmap_write(dev->regmap, ATMEL_I2SC_CR,
			    ATMEL_I2SC_CR_CKEN);
}

static int atmel_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *dai)
{
	struct atmel_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);
	bool is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);
	bool is_master, mck_enabled;
	unsigned int cr, mr;
	int err;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		cr = is_playback ? ATMEL_I2SC_CR_TXEN : ATMEL_I2SC_CR_RXEN;
		mck_enabled = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		cr = is_playback ? ATMEL_I2SC_CR_TXDIS : ATMEL_I2SC_CR_RXDIS;
		mck_enabled = false;
		break;
	default:
		return -EINVAL;
	}

	/* Read the Mode Register to retrieve the master/slave state. */
	err = regmap_read(dev->regmap, ATMEL_I2SC_MR, &mr);
	if (err)
		return err;
	is_master = (mr & ATMEL_I2SC_MR_MODE_MASK) == ATMEL_I2SC_MR_MODE_MASTER;

	/* If master starts, enable the audio clock. */
	if (is_master && mck_enabled)
		err = atmel_i2s_switch_mck_generator(dev, true);
	if (err)
		return err;

	err = regmap_write(dev->regmap, ATMEL_I2SC_CR, cr);
	if (err)
		return err;

	/* If master stops, disable the audio clock. */
	if (is_master && !mck_enabled)
		err = atmel_i2s_switch_mck_generator(dev, false);

	return err;
}

static const struct snd_soc_dai_ops atmel_i2s_dai_ops = {
	.prepare	= atmel_i2s_prepare,
	.trigger	= atmel_i2s_trigger,
	.hw_params	= atmel_i2s_hw_params,
	.set_fmt	= atmel_i2s_set_dai_fmt,
};

static int atmel_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct atmel_i2s_dev *dev = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &dev->playback, &dev->capture);
	return 0;
}

static struct snd_soc_dai_driver atmel_i2s_dai = {
	.probe	= atmel_i2s_dai_probe,
	.playback = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = ATMEL_I2S_RATES,
		.formats = ATMEL_I2S_FORMATS,
	},
	.capture = {
		.channels_min = 1,
		.channels_max = 2,
		.rates = ATMEL_I2S_RATES,
		.formats = ATMEL_I2S_FORMATS,
	},
	.ops = &atmel_i2s_dai_ops,
	.symmetric_rates = 1,
};

static const struct snd_soc_component_driver atmel_i2s_component = {
	.name	= "atmel-i2s",
};

static int atmel_i2s_sama5d2_mck_init(struct atmel_i2s_dev *dev,
				      struct device_node *np)
{
	struct clk *muxclk;
	int err;

	if (!dev->gclk)
		return 0;

	/* muxclk is optional, so we return error for probe defer only */
	muxclk = devm_clk_get(dev->dev, "muxclk");
	if (IS_ERR(muxclk)) {
		err = PTR_ERR(muxclk);
		if (err == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_warn(dev->dev,
			 "failed to get the I2S clock control: %d\n", err);
		return 0;
	}

	return clk_set_parent(muxclk, dev->gclk);
}

static const struct atmel_i2s_caps atmel_i2s_sama5d2_caps = {
	.mck_init = atmel_i2s_sama5d2_mck_init,
};

static const struct of_device_id atmel_i2s_dt_ids[] = {
	{
		.compatible = "atmel,sama5d2-i2s",
		.data = (void *)&atmel_i2s_sama5d2_caps,
	},

	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, atmel_i2s_dt_ids);

static int atmel_i2s_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct atmel_i2s_dev *dev;
	struct resource *mem;
	struct regmap *regmap;
	void __iomem *base;
	int irq;
	int err = -ENXIO;
	unsigned int pcm_flags = 0;
	unsigned int version;

	/* Get memory for driver data. */
	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* Get hardware capabilities. */
	match = of_match_node(atmel_i2s_dt_ids, np);
	if (match)
		dev->caps = match->data;

	/* Map I/O registers. */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base,
				       &atmel_i2s_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Request IRQ. */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(&pdev->dev, irq, atmel_i2s_interrupt, 0,
			       dev_name(&pdev->dev), dev);
	if (err)
		return err;

	/* Get the peripheral clock. */
	dev->pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(dev->pclk)) {
		err = PTR_ERR(dev->pclk);
		dev_err(&pdev->dev,
			"failed to get the peripheral clock: %d\n", err);
		return err;
	}

	/* Get audio clocks to generate the I2S Master Clock (I2S_MCK) */
	dev->aclk = devm_clk_get(&pdev->dev, "aclk");
	dev->gclk = devm_clk_get(&pdev->dev, "gclk");
	if (IS_ERR(dev->aclk) && IS_ERR(dev->gclk)) {
		if (PTR_ERR(dev->aclk) == -EPROBE_DEFER ||
		    PTR_ERR(dev->gclk) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		/* Master Mode not supported */
		dev->aclk = NULL;
		dev->gclk = NULL;
	} else if (IS_ERR(dev->gclk)) {
		err = PTR_ERR(dev->gclk);
		dev_err(&pdev->dev,
			"failed to get the PMC generated clock: %d\n", err);
		return err;
	} else if (IS_ERR(dev->aclk)) {
		err = PTR_ERR(dev->aclk);
		dev_err(&pdev->dev,
			"failed to get the PLL audio clock: %d\n", err);
		return err;
	}

	dev->dev = &pdev->dev;
	dev->regmap = regmap;
	platform_set_drvdata(pdev, dev);

	/* Do hardware specific settings to initialize I2S_MCK generator */
	if (dev->caps && dev->caps->mck_init) {
		err = dev->caps->mck_init(dev, np);
		if (err)
			return err;
	}

	/* Enable the peripheral clock. */
	err = clk_prepare_enable(dev->pclk);
	if (err)
		return err;

	/* Get IP version. */
	regmap_read(dev->regmap, ATMEL_I2SC_VERSION, &version);
	dev_info(&pdev->dev, "hw version: %#x\n", version);

	/* Enable error interrupts. */
	regmap_write(dev->regmap, ATMEL_I2SC_IER,
		     ATMEL_I2SC_INT_RXOR | ATMEL_I2SC_INT_TXUR);

	err = devm_snd_soc_register_component(&pdev->dev,
					      &atmel_i2s_component,
					      &atmel_i2s_dai, 1);
	if (err) {
		dev_err(&pdev->dev, "failed to register DAI: %d\n", err);
		clk_disable_unprepare(dev->pclk);
		return err;
	}

	/* Prepare DMA config. */
	dev->playback.addr	= (dma_addr_t)mem->start + ATMEL_I2SC_THR;
	dev->playback.maxburst	= 1;
	dev->capture.addr	= (dma_addr_t)mem->start + ATMEL_I2SC_RHR;
	dev->capture.maxburst	= 1;

	if (of_property_match_string(np, "dma-names", "rx-tx") == 0)
		pcm_flags |= SND_DMAENGINE_PCM_FLAG_HALF_DUPLEX;
	err = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, pcm_flags);
	if (err) {
		dev_err(&pdev->dev, "failed to register PCM: %d\n", err);
		clk_disable_unprepare(dev->pclk);
		return err;
	}

	return 0;
}

static int atmel_i2s_remove(struct platform_device *pdev)
{
	struct atmel_i2s_dev *dev = platform_get_drvdata(pdev);

	clk_disable_unprepare(dev->pclk);

	return 0;
}

static struct platform_driver atmel_i2s_driver = {
	.driver		= {
		.name	= "atmel_i2s",
		.of_match_table	= of_match_ptr(atmel_i2s_dt_ids),
	},
	.probe		= atmel_i2s_probe,
	.remove		= atmel_i2s_remove,
};
module_platform_driver(atmel_i2s_driver);

MODULE_DESCRIPTION("Atmel I2S Controller driver");
MODULE_AUTHOR("Cyrille Pitchen <cyrille.pitchen@atmel.com>");
MODULE_LICENSE("GPL v2");
