/*
 * DA7210 ALSA Soc codec driver
 *
 * Copyright (c) 2009 Dialog Semiconductor
 * Written by David Chen <Dajun.chen@diasemi.com>
 *
 * Copyright (C) 2009 Renesas Solutions Corp.
 * Cleanups by Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * Tested on SuperH Ecovec24 board with S16/S24 LE in 48KHz using I2S
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "da7210.h"

/* DA7210 register space */
#define DA7210_STATUS			0x02
#define DA7210_STARTUP1			0x03
#define DA7210_MIC_L			0x07
#define DA7210_MIC_R			0x08
#define DA7210_INMIX_L			0x0D
#define DA7210_INMIX_R			0x0E
#define DA7210_ADC_HPF			0x0F
#define DA7210_ADC			0x10
#define DA7210_DAC_HPF			0x14
#define DA7210_DAC_L			0x15
#define DA7210_DAC_R			0x16
#define DA7210_DAC_SEL			0x17
#define DA7210_OUTMIX_L			0x1C
#define DA7210_OUTMIX_R			0x1D
#define DA7210_HP_L_VOL			0x21
#define DA7210_HP_R_VOL			0x22
#define DA7210_HP_CFG			0x23
#define DA7210_DAI_SRC_SEL		0x25
#define DA7210_DAI_CFG1			0x26
#define DA7210_DAI_CFG3			0x28
#define DA7210_PLL_DIV1			0x29
#define DA7210_PLL_DIV2			0x2A
#define DA7210_PLL_DIV3			0x2B
#define DA7210_PLL			0x2C
#define DA7210_A_HID_UNLOCK		0x8A
#define DA7210_A_TEST_UNLOCK		0x8B
#define DA7210_A_PLL1			0x90
#define DA7210_A_CP_MODE		0xA7

/* STARTUP1 bit fields */
#define DA7210_SC_MST_EN		(1 << 0)

/* MIC_L bit fields */
#define DA7210_MICBIAS_EN		(1 << 6)
#define DA7210_MIC_L_EN			(1 << 7)

/* MIC_R bit fields */
#define DA7210_MIC_R_EN			(1 << 7)

/* INMIX_L bit fields */
#define DA7210_IN_L_EN			(1 << 7)

/* INMIX_R bit fields */
#define DA7210_IN_R_EN			(1 << 7)

/* ADC bit fields */
#define DA7210_ADC_L_EN			(1 << 3)
#define DA7210_ADC_R_EN			(1 << 7)

/* DAC/ADC HPF fields */
#define DA7210_VOICE_F0_MASK		(0x7 << 4)
#define DA7210_VOICE_F0_25		(1 << 4)
#define DA7210_VOICE_EN			(1 << 7)

/* DAC_SEL bit fields */
#define DA7210_DAC_L_SRC_DAI_L		(4 << 0)
#define DA7210_DAC_L_EN			(1 << 3)
#define DA7210_DAC_R_SRC_DAI_R		(5 << 4)
#define DA7210_DAC_R_EN			(1 << 7)

/* OUTMIX_L bit fields */
#define DA7210_OUT_L_EN			(1 << 7)

/* OUTMIX_R bit fields */
#define DA7210_OUT_R_EN			(1 << 7)

/* HP_CFG bit fields */
#define DA7210_HP_2CAP_MODE		(1 << 1)
#define DA7210_HP_SENSE_EN		(1 << 2)
#define DA7210_HP_L_EN			(1 << 3)
#define DA7210_HP_MODE			(1 << 6)
#define DA7210_HP_R_EN			(1 << 7)

/* DAI_SRC_SEL bit fields */
#define DA7210_DAI_OUT_L_SRC		(6 << 0)
#define DA7210_DAI_OUT_R_SRC		(7 << 4)

/* DAI_CFG1 bit fields */
#define DA7210_DAI_WORD_S16_LE		(0 << 0)
#define DA7210_DAI_WORD_S24_LE		(2 << 0)
#define DA7210_DAI_FLEN_64BIT		(1 << 2)
#define DA7210_DAI_MODE_MASTER		(1 << 7)

/* DAI_CFG3 bit fields */
#define DA7210_DAI_FORMAT_I2SMODE	(0 << 0)
#define DA7210_DAI_OE			(1 << 3)
#define DA7210_DAI_EN			(1 << 7)

/*PLL_DIV3 bit fields */
#define DA7210_MCLK_RANGE_10_20_MHZ	(1 << 4)
#define DA7210_PLL_BYP			(1 << 6)

/* PLL bit fields */
#define DA7210_PLL_FS_MASK		(0xF << 0)
#define DA7210_PLL_FS_8000		(0x1 << 0)
#define DA7210_PLL_FS_11025		(0x2 << 0)
#define DA7210_PLL_FS_12000		(0x3 << 0)
#define DA7210_PLL_FS_16000		(0x5 << 0)
#define DA7210_PLL_FS_22050		(0x6 << 0)
#define DA7210_PLL_FS_24000		(0x7 << 0)
#define DA7210_PLL_FS_32000		(0x9 << 0)
#define DA7210_PLL_FS_44100		(0xA << 0)
#define DA7210_PLL_FS_48000		(0xB << 0)
#define DA7210_PLL_FS_88200		(0xE << 0)
#define DA7210_PLL_FS_96000		(0xF << 0)
#define DA7210_PLL_EN			(0x1 << 7)

#define DA7210_VERSION "0.0.1"

/*
 * Playback Volume
 *
 * max		: 0x3F (+15.0 dB)
 *		   (1.5 dB step)
 * min		: 0x11 (-54.0 dB)
 * mute		: 0x10
 * reserved	: 0x00 - 0x0F
 *
 * ** FIXME **
 *
 * Reserved area are considered as "mute".
 * -> min = -79.5 dB
 */
static const DECLARE_TLV_DB_SCALE(hp_out_tlv, -7950, 150, 1);

static const struct snd_kcontrol_new da7210_snd_controls[] = {

	SOC_DOUBLE_R_TLV("HeadPhone Playback Volume",
			 DA7210_HP_L_VOL, DA7210_HP_R_VOL,
			 0, 0x3F, 0, hp_out_tlv),
};

/* Codec private data */
struct da7210_priv {
	struct snd_soc_codec codec;
};

static struct snd_soc_codec *da7210_codec;

/*
 * Register cache
 */
static const u8 da7210_reg[] = {
	0x00, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R0  - R7  */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08,	/* R8  - RF  */
	0x00, 0x00, 0x00, 0x00, 0x08, 0x10, 0x10, 0x54,	/* R10 - R17 */
	0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R18 - R1F */
	0x00, 0x00, 0x00, 0x02, 0x00, 0x76, 0x00, 0x00,	/* R20 - R27 */
	0x04, 0x00, 0x00, 0x30, 0x2A, 0x00, 0x40, 0x00,	/* R28 - R2F */
	0x40, 0x00, 0x40, 0x00, 0x40, 0x00, 0x40, 0x00,	/* R30 - R37 */
	0x40, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x00,	/* R38 - R3F */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R40 - R4F */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R48 - R4F */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R50 - R57 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R58 - R5F */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R60 - R67 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R68 - R6F */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R70 - R77 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x54, 0x54, 0x00,	/* R78 - R7F */
	0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00,	/* R80 - R87 */
	0x00,						/* R88       */
};

/*
 * Read da7210 register cache
 */
static inline u32 da7210_read_reg_cache(struct snd_soc_codec *codec, u32 reg)
{
	u8 *cache = codec->reg_cache;
	BUG_ON(reg >= ARRAY_SIZE(da7210_reg));
	return cache[reg];
}

/*
 * Write to the da7210 register space
 */
static int da7210_write(struct snd_soc_codec *codec, u32 reg, u32 value)
{
	u8 *cache = codec->reg_cache;
	u8 data[2];

	BUG_ON(codec->volatile_register);

	data[0] = reg & 0xff;
	data[1] = value & 0xff;

	if (reg >= codec->reg_cache_size)
		return -EIO;

	if (2 != codec->hw_write(codec->control_data, data, 2))
		return -EIO;

	cache[reg] = value;
	return 0;
}

/*
 * Read from the da7210 register space.
 */
static inline u32 da7210_read(struct snd_soc_codec *codec, u32 reg)
{
	if (DA7210_STATUS == reg)
		return i2c_smbus_read_byte_data(codec->control_data, reg);

	return da7210_read_reg_cache(codec, reg);
}

static int da7210_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	int is_play = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	struct snd_soc_codec *codec = dai->codec;

	if (is_play) {
		/* Enable Out */
		snd_soc_update_bits(codec, DA7210_OUTMIX_L, 0x1F, 0x10);
		snd_soc_update_bits(codec, DA7210_OUTMIX_R, 0x1F, 0x10);

	} else {
		/* Volume 7 */
		snd_soc_update_bits(codec, DA7210_MIC_L, 0x7, 0x7);
		snd_soc_update_bits(codec, DA7210_MIC_R, 0x7, 0x7);

		/* Enable Mic */
		snd_soc_update_bits(codec, DA7210_INMIX_L, 0x1F, 0x1);
		snd_soc_update_bits(codec, DA7210_INMIX_R, 0x1F, 0x1);
	}

	return 0;
}

/*
 * Set PCM DAI word length.
 */
static int da7210_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	u32 dai_cfg1;
	u32 hpf_reg, hpf_mask, hpf_value;
	u32 fs, bypass;

	/* set DAI source to Left and Right ADC */
	da7210_write(codec, DA7210_DAI_SRC_SEL,
		     DA7210_DAI_OUT_R_SRC | DA7210_DAI_OUT_L_SRC);

	/* Enable DAI */
	da7210_write(codec, DA7210_DAI_CFG3, DA7210_DAI_OE | DA7210_DAI_EN);

	dai_cfg1 = 0xFC & da7210_read(codec, DA7210_DAI_CFG1);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		dai_cfg1 |= DA7210_DAI_WORD_S16_LE;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		dai_cfg1 |= DA7210_DAI_WORD_S24_LE;
		break;
	default:
		return -EINVAL;
	}

	da7210_write(codec, DA7210_DAI_CFG1, dai_cfg1);

	hpf_reg = (SNDRV_PCM_STREAM_PLAYBACK == substream->stream) ?
		DA7210_DAC_HPF : DA7210_ADC_HPF;

	switch (params_rate(params)) {
	case 8000:
		fs		= DA7210_PLL_FS_8000;
		hpf_mask	= DA7210_VOICE_F0_MASK	| DA7210_VOICE_EN;
		hpf_value	= DA7210_VOICE_F0_25	| DA7210_VOICE_EN;
		bypass		= DA7210_PLL_BYP;
		break;
	case 11025:
		fs		= DA7210_PLL_FS_11025;
		hpf_mask	= DA7210_VOICE_F0_MASK	| DA7210_VOICE_EN;
		hpf_value	= DA7210_VOICE_F0_25	| DA7210_VOICE_EN;
		bypass		= 0;
		break;
	case 12000:
		fs		= DA7210_PLL_FS_12000;
		hpf_mask	= DA7210_VOICE_F0_MASK	| DA7210_VOICE_EN;
		hpf_value	= DA7210_VOICE_F0_25	| DA7210_VOICE_EN;
		bypass		= DA7210_PLL_BYP;
		break;
	case 16000:
		fs		= DA7210_PLL_FS_16000;
		hpf_mask	= DA7210_VOICE_F0_MASK	| DA7210_VOICE_EN;
		hpf_value	= DA7210_VOICE_F0_25	| DA7210_VOICE_EN;
		bypass		= DA7210_PLL_BYP;
		break;
	case 22050:
		fs		= DA7210_PLL_FS_22050;
		hpf_mask	= DA7210_VOICE_EN;
		hpf_value	= 0;
		bypass		= 0;
		break;
	case 32000:
		fs		= DA7210_PLL_FS_32000;
		hpf_mask	= DA7210_VOICE_EN;
		hpf_value	= 0;
		bypass		= DA7210_PLL_BYP;
		break;
	case 44100:
		fs		= DA7210_PLL_FS_44100;
		hpf_mask	= DA7210_VOICE_EN;
		hpf_value	= 0;
		bypass		= 0;
		break;
	case 48000:
		fs		= DA7210_PLL_FS_48000;
		hpf_mask	= DA7210_VOICE_EN;
		hpf_value	= 0;
		bypass		= DA7210_PLL_BYP;
		break;
	case 88200:
		fs		= DA7210_PLL_FS_88200;
		hpf_mask	= DA7210_VOICE_EN;
		hpf_value	= 0;
		bypass		= 0;
		break;
	case 96000:
		fs		= DA7210_PLL_FS_96000;
		hpf_mask	= DA7210_VOICE_EN;
		hpf_value	= 0;
		bypass		= DA7210_PLL_BYP;
		break;
	default:
		return -EINVAL;
	}

	/* Disable active mode */
	snd_soc_update_bits(codec, DA7210_STARTUP1, DA7210_SC_MST_EN, 0);

	snd_soc_update_bits(codec, hpf_reg, hpf_mask, hpf_value);
	snd_soc_update_bits(codec, DA7210_PLL, DA7210_PLL_FS_MASK, fs);
	snd_soc_update_bits(codec, DA7210_PLL_DIV3, DA7210_PLL_BYP, bypass);

	/* Enable active mode */
	snd_soc_update_bits(codec, DA7210_STARTUP1,
			    DA7210_SC_MST_EN, DA7210_SC_MST_EN);

	return 0;
}

/*
 * Set DAI mode and Format
 */
static int da7210_set_dai_fmt(struct snd_soc_dai *codec_dai, u32 fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u32 dai_cfg1;
	u32 dai_cfg3;

	dai_cfg1 = 0x7f & da7210_read(codec, DA7210_DAI_CFG1);
	dai_cfg3 = 0xfc & da7210_read(codec, DA7210_DAI_CFG3);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		dai_cfg1 |= DA7210_DAI_MODE_MASTER;
		break;
	default:
		return -EINVAL;
	}

	/* FIXME
	 *
	 * It support I2S only now
	 */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dai_cfg3 |= DA7210_DAI_FORMAT_I2SMODE;
		break;
	default:
		return -EINVAL;
	}

	/* FIXME
	 *
	 * It support 64bit data transmission only now
	 */
	dai_cfg1 |= DA7210_DAI_FLEN_64BIT;

	da7210_write(codec, DA7210_DAI_CFG1, dai_cfg1);
	da7210_write(codec, DA7210_DAI_CFG3, dai_cfg3);

	return 0;
}

#define DA7210_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

/* DAI operations */
static struct snd_soc_dai_ops da7210_dai_ops = {
	.startup	= da7210_startup,
	.hw_params	= da7210_hw_params,
	.set_fmt	= da7210_set_dai_fmt,
};

struct snd_soc_dai da7210_dai = {
	.name = "DA7210 IIS",
	.id = 0,
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA7210_FORMATS,
	},
	/* capture capabilities */
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = DA7210_FORMATS,
	},
	.ops = &da7210_dai_ops,
	.symmetric_rates = 1,
};
EXPORT_SYMBOL_GPL(da7210_dai);

/*
 * Initialize the DA7210 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int da7210_init(struct da7210_priv *da7210)
{
	struct snd_soc_codec *codec = &da7210->codec;
	int ret = 0;

	if (da7210_codec) {
		dev_err(codec->dev, "Another da7210 is registered\n");
		return -EINVAL;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	snd_soc_codec_set_drvdata(codec, da7210);
	codec->name		= "DA7210";
	codec->owner		= THIS_MODULE;
	codec->read		= da7210_read;
	codec->write		= da7210_write;
	codec->dai		= &da7210_dai;
	codec->num_dai		= 1;
	codec->hw_write		= (hw_write_t)i2c_master_send;
	codec->reg_cache_size	= ARRAY_SIZE(da7210_reg);
	codec->reg_cache	= kmemdup(da7210_reg,
					  sizeof(da7210_reg), GFP_KERNEL);

	if (!codec->reg_cache)
		return -ENOMEM;

	da7210_dai.dev = codec->dev;
	da7210_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret) {
		dev_err(codec->dev, "Failed to register CODEC: %d\n", ret);
		goto init_err;
	}

	ret = snd_soc_register_dai(&da7210_dai);
	if (ret) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		goto codec_err;
	}

	/* FIXME
	 *
	 * This driver use fixed value here
	 * And below settings expects MCLK = 12.288MHz
	 *
	 * When you select different MCLK, please check...
	 *      DA7210_PLL_DIV1 val
	 *      DA7210_PLL_DIV2 val
	 *      DA7210_PLL_DIV3 val
	 *      DA7210_PLL_DIV3 :: DA7210_MCLK_RANGExxx
	 */

	/*
	 * make sure that DA7210 use bypass mode before start up
	 */
	da7210_write(codec, DA7210_STARTUP1, 0);
	da7210_write(codec, DA7210_PLL_DIV3,
		     DA7210_MCLK_RANGE_10_20_MHZ | DA7210_PLL_BYP);

	/*
	 * ADC settings
	 */

	/* Enable Left & Right MIC PGA and Mic Bias */
	da7210_write(codec, DA7210_MIC_L, DA7210_MIC_L_EN | DA7210_MICBIAS_EN);
	da7210_write(codec, DA7210_MIC_R, DA7210_MIC_R_EN);

	/* Enable Left and Right input PGA */
	da7210_write(codec, DA7210_INMIX_L, DA7210_IN_L_EN);
	da7210_write(codec, DA7210_INMIX_R, DA7210_IN_R_EN);

	/* Enable Left and Right ADC */
	da7210_write(codec, DA7210_ADC, DA7210_ADC_L_EN | DA7210_ADC_R_EN);

	/*
	 * DAC settings
	 */

	/* Enable Left and Right DAC */
	da7210_write(codec, DA7210_DAC_SEL,
		     DA7210_DAC_L_SRC_DAI_L | DA7210_DAC_L_EN |
		     DA7210_DAC_R_SRC_DAI_R | DA7210_DAC_R_EN);

	/* Enable Left and Right out PGA */
	da7210_write(codec, DA7210_OUTMIX_L, DA7210_OUT_L_EN);
	da7210_write(codec, DA7210_OUTMIX_R, DA7210_OUT_R_EN);

	/* Enable Left and Right HeadPhone PGA */
	da7210_write(codec, DA7210_HP_CFG,
		     DA7210_HP_2CAP_MODE | DA7210_HP_SENSE_EN |
		     DA7210_HP_L_EN | DA7210_HP_MODE | DA7210_HP_R_EN);

	/* Diable PLL and bypass it */
	da7210_write(codec, DA7210_PLL, DA7210_PLL_FS_48000);

	/*
	 * If 48kHz sound came, it use bypass mode,
	 * and when it is 44.1kHz, it use PLL.
	 *
	 * This time, this driver sets PLL always ON
	 * and controls bypass/PLL mode by switching
	 * DA7210_PLL_DIV3 :: DA7210_PLL_BYP bit.
	 *   see da7210_hw_params
	 */
	da7210_write(codec, DA7210_PLL_DIV1, 0xE5); /* MCLK = 12.288MHz */
	da7210_write(codec, DA7210_PLL_DIV2, 0x99);
	da7210_write(codec, DA7210_PLL_DIV3, 0x0A |
		     DA7210_MCLK_RANGE_10_20_MHZ | DA7210_PLL_BYP);
	snd_soc_update_bits(codec, DA7210_PLL, DA7210_PLL_EN, DA7210_PLL_EN);

	/* As suggested by Dialog */
	da7210_write(codec, DA7210_A_HID_UNLOCK,	0x8B); /* unlock */
	da7210_write(codec, DA7210_A_TEST_UNLOCK,	0xB4);
	da7210_write(codec, DA7210_A_PLL1,		0x01);
	da7210_write(codec, DA7210_A_CP_MODE,		0x7C);
	da7210_write(codec, DA7210_A_HID_UNLOCK,	0x00); /* re-lock */
	da7210_write(codec, DA7210_A_TEST_UNLOCK,	0x00);

	/* Activate all enabled subsystem */
	da7210_write(codec, DA7210_STARTUP1, DA7210_SC_MST_EN);

	return ret;

codec_err:
	snd_soc_unregister_codec(codec);
init_err:
	kfree(codec->reg_cache);
	codec->reg_cache = NULL;

	return ret;

}

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static int __devinit da7210_i2c_probe(struct i2c_client *i2c,
			   	      const struct i2c_device_id *id)
{
	struct da7210_priv *da7210;
	struct snd_soc_codec *codec;
	int ret;

	da7210 = kzalloc(sizeof(struct da7210_priv), GFP_KERNEL);
	if (!da7210)
		return -ENOMEM;

	codec = &da7210->codec;
	codec->dev = &i2c->dev;

	i2c_set_clientdata(i2c, da7210);
	codec->control_data = i2c;

	ret = da7210_init(da7210);
	if (ret < 0) {
		pr_err("Failed to initialise da7210 audio codec\n");
		kfree(da7210);
	}

	return ret;
}

static int __devexit da7210_i2c_remove(struct i2c_client *client)
{
	struct da7210_priv *da7210 = i2c_get_clientdata(client);

	snd_soc_unregister_dai(&da7210_dai);
	kfree(da7210->codec.reg_cache);
	kfree(da7210);
	da7210_codec = NULL;

	return 0;
}

static const struct i2c_device_id da7210_i2c_id[] = {
	{ "da7210", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, da7210_i2c_id);

/* I2C codec control layer */
static struct i2c_driver da7210_i2c_driver = {
	.driver = {
		.name = "DA7210 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe = da7210_i2c_probe,
	.remove =  __devexit_p(da7210_i2c_remove),
	.id_table = da7210_i2c_id,
};
#endif

static int da7210_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret;

	if (!da7210_codec) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = da7210_codec;
	codec = da7210_codec;

	/* Register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0)
		goto pcm_err;

	snd_soc_add_controls(da7210_codec, da7210_snd_controls,
			     ARRAY_SIZE(da7210_snd_controls));

	dev_info(&pdev->dev, "DA7210 Audio Codec %s\n", DA7210_VERSION);

pcm_err:
	return ret;
}

static int da7210_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_da7210 = {
	.probe =	da7210_probe,
	.remove =	da7210_remove,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_da7210);

static int __init da7210_modinit(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&da7210_i2c_driver);
#endif
	return ret;
}
module_init(da7210_modinit);

static void __exit da7210_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&da7210_i2c_driver);
#endif
}
module_exit(da7210_exit);

MODULE_DESCRIPTION("ASoC DA7210 driver");
MODULE_AUTHOR("David Chen, Kuninori Morimoto");
MODULE_LICENSE("GPL");
