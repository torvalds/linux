/*
 * CS4271 ASoC codec driver
 *
 * Copyright (c) 2010 Alexander Sverdlin <subaparts@yandex.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This driver support CS4271 codec being master or slave, working
 * in control port mode, connected either via SPI or I2C.
 * The data format accepted is I2S or left-justified.
 * DAPM support not implemented.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/cs4271.h>

#define CS4271_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			    SNDRV_PCM_FMTBIT_S24_LE | \
			    SNDRV_PCM_FMTBIT_S32_LE)
#define CS4271_PCM_RATES SNDRV_PCM_RATE_8000_192000

/*
 * CS4271 registers
 */
#define CS4271_MODE1	0x01	/* Mode Control 1 */
#define CS4271_DACCTL	0x02	/* DAC Control */
#define CS4271_DACVOL	0x03	/* DAC Volume & Mixing Control */
#define CS4271_VOLA	0x04	/* DAC Channel A Volume Control */
#define CS4271_VOLB	0x05	/* DAC Channel B Volume Control */
#define CS4271_ADCCTL	0x06	/* ADC Control */
#define CS4271_MODE2	0x07	/* Mode Control 2 */
#define CS4271_CHIPID	0x08	/* Chip ID */

#define CS4271_FIRSTREG	CS4271_MODE1
#define CS4271_LASTREG	CS4271_MODE2
#define CS4271_NR_REGS	((CS4271_LASTREG & 0xFF) + 1)

/* Bit masks for the CS4271 registers */
#define CS4271_MODE1_MODE_MASK	0xC0
#define CS4271_MODE1_MODE_1X	0x00
#define CS4271_MODE1_MODE_2X	0x80
#define CS4271_MODE1_MODE_4X	0xC0

#define CS4271_MODE1_DIV_MASK	0x30
#define CS4271_MODE1_DIV_1	0x00
#define CS4271_MODE1_DIV_15	0x10
#define CS4271_MODE1_DIV_2	0x20
#define CS4271_MODE1_DIV_3	0x30

#define CS4271_MODE1_MASTER	0x08

#define CS4271_MODE1_DAC_DIF_MASK	0x07
#define CS4271_MODE1_DAC_DIF_LJ		0x00
#define CS4271_MODE1_DAC_DIF_I2S	0x01
#define CS4271_MODE1_DAC_DIF_RJ16	0x02
#define CS4271_MODE1_DAC_DIF_RJ24	0x03
#define CS4271_MODE1_DAC_DIF_RJ20	0x04
#define CS4271_MODE1_DAC_DIF_RJ18	0x05

#define CS4271_DACCTL_AMUTE	0x80
#define CS4271_DACCTL_IF_SLOW	0x40

#define CS4271_DACCTL_DEM_MASK	0x30
#define CS4271_DACCTL_DEM_DIS	0x00
#define CS4271_DACCTL_DEM_441	0x10
#define CS4271_DACCTL_DEM_48	0x20
#define CS4271_DACCTL_DEM_32	0x30

#define CS4271_DACCTL_SVRU	0x08
#define CS4271_DACCTL_SRD	0x04
#define CS4271_DACCTL_INVA	0x02
#define CS4271_DACCTL_INVB	0x01

#define CS4271_DACVOL_BEQUA	0x40
#define CS4271_DACVOL_SOFT	0x20
#define CS4271_DACVOL_ZEROC	0x10

#define CS4271_DACVOL_ATAPI_MASK	0x0F
#define CS4271_DACVOL_ATAPI_M_M		0x00
#define CS4271_DACVOL_ATAPI_M_BR	0x01
#define CS4271_DACVOL_ATAPI_M_BL	0x02
#define CS4271_DACVOL_ATAPI_M_BLR2	0x03
#define CS4271_DACVOL_ATAPI_AR_M	0x04
#define CS4271_DACVOL_ATAPI_AR_BR	0x05
#define CS4271_DACVOL_ATAPI_AR_BL	0x06
#define CS4271_DACVOL_ATAPI_AR_BLR2	0x07
#define CS4271_DACVOL_ATAPI_AL_M	0x08
#define CS4271_DACVOL_ATAPI_AL_BR	0x09
#define CS4271_DACVOL_ATAPI_AL_BL	0x0A
#define CS4271_DACVOL_ATAPI_AL_BLR2	0x0B
#define CS4271_DACVOL_ATAPI_ALR2_M	0x0C
#define CS4271_DACVOL_ATAPI_ALR2_BR	0x0D
#define CS4271_DACVOL_ATAPI_ALR2_BL	0x0E
#define CS4271_DACVOL_ATAPI_ALR2_BLR2	0x0F

#define CS4271_VOLA_MUTE	0x80
#define CS4271_VOLA_VOL_MASK	0x7F
#define CS4271_VOLB_MUTE	0x80
#define CS4271_VOLB_VOL_MASK	0x7F

#define CS4271_ADCCTL_DITHER16	0x20

#define CS4271_ADCCTL_ADC_DIF_MASK	0x10
#define CS4271_ADCCTL_ADC_DIF_LJ	0x00
#define CS4271_ADCCTL_ADC_DIF_I2S	0x10

#define CS4271_ADCCTL_MUTEA	0x08
#define CS4271_ADCCTL_MUTEB	0x04
#define CS4271_ADCCTL_HPFDA	0x02
#define CS4271_ADCCTL_HPFDB	0x01

#define CS4271_MODE2_LOOP	0x10
#define CS4271_MODE2_MUTECAEQUB	0x08
#define CS4271_MODE2_FREEZE	0x04
#define CS4271_MODE2_CPEN	0x02
#define CS4271_MODE2_PDN	0x01

#define CS4271_CHIPID_PART_MASK	0xF0
#define CS4271_CHIPID_REV_MASK	0x0F

/*
 * Default CS4271 power-up configuration
 * Array contains non-existing in hw register at address 0
 * Array do not include Chip ID, as codec driver does not use
 * registers read operations at all
 */
static const struct reg_default cs4271_reg_defaults[] = {
	{ CS4271_MODE1,		0, },
	{ CS4271_DACCTL,	CS4271_DACCTL_AMUTE, },
	{ CS4271_DACVOL,	CS4271_DACVOL_SOFT | CS4271_DACVOL_ATAPI_AL_BR, },
	{ CS4271_VOLA,		0, },
	{ CS4271_VOLB,		0, },
	{ CS4271_ADCCTL,	0, },
	{ CS4271_MODE2,		0, },
};

static bool cs4271_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == CS4271_CHIPID;
}

struct cs4271_private {
	unsigned int			mclk;
	bool				master;
	bool				deemph;
	struct regmap			*regmap;
	/* Current sample rate for de-emphasis control */
	int				rate;
	/* GPIO driving Reset pin, if any */
	int				gpio_nreset;
	/* GPIO that disable serial bus, if any */
	int				gpio_disable;
	/* enable soft reset workaround */
	bool				enable_soft_reset;
};

static const struct snd_soc_dapm_widget cs4271_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("AINA"),
SND_SOC_DAPM_INPUT("AINB"),

SND_SOC_DAPM_OUTPUT("AOUTA+"),
SND_SOC_DAPM_OUTPUT("AOUTA-"),
SND_SOC_DAPM_OUTPUT("AOUTB+"),
SND_SOC_DAPM_OUTPUT("AOUTB-"),
};

static const struct snd_soc_dapm_route cs4271_dapm_routes[] = {
	{ "Capture", NULL, "AINA" },
	{ "Capture", NULL, "AINB" },

	{ "AOUTA+", NULL, "Playback" },
	{ "AOUTA-", NULL, "Playback" },
	{ "AOUTB+", NULL, "Playback" },
	{ "AOUTB-", NULL, "Playback" },
};

/*
 * @freq is the desired MCLK rate
 * MCLK rate should (c) be the sample rate, multiplied by one of the
 * ratios listed in cs4271_mclk_fs_ratios table
 */
static int cs4271_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);

	cs4271->mclk = freq;
	return 0;
}

static int cs4271_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0;
	int ret;

	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		cs4271->master = 0;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		cs4271->master = 1;
		val |= CS4271_MODE1_MASTER;
		break;
	default:
		dev_err(codec->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		val |= CS4271_MODE1_DAC_DIF_LJ;
		ret = regmap_update_bits(cs4271->regmap, CS4271_ADCCTL,
			CS4271_ADCCTL_ADC_DIF_MASK, CS4271_ADCCTL_ADC_DIF_LJ);
		if (ret < 0)
			return ret;
		break;
	case SND_SOC_DAIFMT_I2S:
		val |= CS4271_MODE1_DAC_DIF_I2S;
		ret = regmap_update_bits(cs4271->regmap, CS4271_ADCCTL,
			CS4271_ADCCTL_ADC_DIF_MASK, CS4271_ADCCTL_ADC_DIF_I2S);
		if (ret < 0)
			return ret;
		break;
	default:
		dev_err(codec->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(cs4271->regmap, CS4271_MODE1,
		CS4271_MODE1_DAC_DIF_MASK | CS4271_MODE1_MASTER, val);
	if (ret < 0)
		return ret;
	return 0;
}

static int cs4271_deemph[] = {0, 44100, 48000, 32000};

static int cs4271_set_deemph(struct snd_soc_codec *codec)
{
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);
	int i, ret;
	int val = CS4271_DACCTL_DEM_DIS;

	if (cs4271->deemph) {
		/* Find closest de-emphasis freq */
		val = 1;
		for (i = 2; i < ARRAY_SIZE(cs4271_deemph); i++)
			if (abs(cs4271_deemph[i] - cs4271->rate) <
			    abs(cs4271_deemph[val] - cs4271->rate))
				val = i;
		val <<= 4;
	}

	ret = regmap_update_bits(cs4271->regmap, CS4271_DACCTL,
		CS4271_DACCTL_DEM_MASK, val);
	if (ret < 0)
		return ret;
	return 0;
}

static int cs4271_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = cs4271->deemph;
	return 0;
}

static int cs4271_put_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);

	cs4271->deemph = ucontrol->value.enumerated.item[0];
	return cs4271_set_deemph(codec);
}

struct cs4271_clk_cfg {
	bool		master;		/* codec mode */
	u8		speed_mode;	/* codec speed mode: 1x, 2x, 4x */
	unsigned short	ratio;		/* MCLK / sample rate */
	u8		ratio_mask;	/* ratio bit mask for Master mode */
};

static struct cs4271_clk_cfg cs4271_clk_tab[] = {
	{1, CS4271_MODE1_MODE_1X, 256,  CS4271_MODE1_DIV_1},
	{1, CS4271_MODE1_MODE_1X, 384,  CS4271_MODE1_DIV_15},
	{1, CS4271_MODE1_MODE_1X, 512,  CS4271_MODE1_DIV_2},
	{1, CS4271_MODE1_MODE_1X, 768,  CS4271_MODE1_DIV_3},
	{1, CS4271_MODE1_MODE_2X, 128,  CS4271_MODE1_DIV_1},
	{1, CS4271_MODE1_MODE_2X, 192,  CS4271_MODE1_DIV_15},
	{1, CS4271_MODE1_MODE_2X, 256,  CS4271_MODE1_DIV_2},
	{1, CS4271_MODE1_MODE_2X, 384,  CS4271_MODE1_DIV_3},
	{1, CS4271_MODE1_MODE_4X, 64,   CS4271_MODE1_DIV_1},
	{1, CS4271_MODE1_MODE_4X, 96,   CS4271_MODE1_DIV_15},
	{1, CS4271_MODE1_MODE_4X, 128,  CS4271_MODE1_DIV_2},
	{1, CS4271_MODE1_MODE_4X, 192,  CS4271_MODE1_DIV_3},
	{0, CS4271_MODE1_MODE_1X, 256,  CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_1X, 384,  CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_1X, 512,  CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_1X, 768,  CS4271_MODE1_DIV_2},
	{0, CS4271_MODE1_MODE_1X, 1024, CS4271_MODE1_DIV_2},
	{0, CS4271_MODE1_MODE_2X, 128,  CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_2X, 192,  CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_2X, 256,  CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_2X, 384,  CS4271_MODE1_DIV_2},
	{0, CS4271_MODE1_MODE_2X, 512,  CS4271_MODE1_DIV_2},
	{0, CS4271_MODE1_MODE_4X, 64,   CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_4X, 96,   CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_4X, 128,  CS4271_MODE1_DIV_1},
	{0, CS4271_MODE1_MODE_4X, 192,  CS4271_MODE1_DIV_2},
	{0, CS4271_MODE1_MODE_4X, 256,  CS4271_MODE1_DIV_2},
};

#define CS4171_NR_RATIOS ARRAY_SIZE(cs4271_clk_tab)

static int cs4271_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);
	int i, ret;
	unsigned int ratio, val;

	if (cs4271->enable_soft_reset) {
		/*
		 * Put the codec in soft reset and back again in case it's not
		 * currently streaming data. This way of bringing the codec in
		 * sync to the current clocks is not explicitly documented in
		 * the data sheet, but it seems to work fine, and in contrast
		 * to a read hardware reset, we don't have to sync back all
		 * registers every time.
		 */

		if ((substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
		     !dai->capture_active) ||
		    (substream->stream == SNDRV_PCM_STREAM_CAPTURE &&
		     !dai->playback_active)) {
			ret = regmap_update_bits(cs4271->regmap, CS4271_MODE2,
						 CS4271_MODE2_PDN,
						 CS4271_MODE2_PDN);
			if (ret < 0)
				return ret;

			ret = regmap_update_bits(cs4271->regmap, CS4271_MODE2,
						 CS4271_MODE2_PDN, 0);
			if (ret < 0)
				return ret;
		}
	}

	cs4271->rate = params_rate(params);

	/* Configure DAC */
	if (cs4271->rate < 50000)
		val = CS4271_MODE1_MODE_1X;
	else if (cs4271->rate < 100000)
		val = CS4271_MODE1_MODE_2X;
	else
		val = CS4271_MODE1_MODE_4X;

	ratio = cs4271->mclk / cs4271->rate;
	for (i = 0; i < CS4171_NR_RATIOS; i++)
		if ((cs4271_clk_tab[i].master == cs4271->master) &&
		    (cs4271_clk_tab[i].speed_mode == val) &&
		    (cs4271_clk_tab[i].ratio == ratio))
			break;

	if (i == CS4171_NR_RATIOS) {
		dev_err(codec->dev, "Invalid sample rate\n");
		return -EINVAL;
	}

	val |= cs4271_clk_tab[i].ratio_mask;

	ret = regmap_update_bits(cs4271->regmap, CS4271_MODE1,
		CS4271_MODE1_MODE_MASK | CS4271_MODE1_DIV_MASK, val);
	if (ret < 0)
		return ret;

	return cs4271_set_deemph(codec);
}

static int cs4271_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);
	int ret;
	int val_a = 0;
	int val_b = 0;

	if (stream != SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	if (mute) {
		val_a = CS4271_VOLA_MUTE;
		val_b = CS4271_VOLB_MUTE;
	}

	ret = regmap_update_bits(cs4271->regmap, CS4271_VOLA,
				 CS4271_VOLA_MUTE, val_a);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(cs4271->regmap, CS4271_VOLB,
				 CS4271_VOLB_MUTE, val_b);
	if (ret < 0)
		return ret;

	return 0;
}

/* CS4271 controls */
static DECLARE_TLV_DB_SCALE(cs4271_dac_tlv, -12700, 100, 0);

static const struct snd_kcontrol_new cs4271_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Master Playback Volume", CS4271_VOLA, CS4271_VOLB,
		0, 0x7F, 1, cs4271_dac_tlv),
	SOC_SINGLE("Digital Loopback Switch", CS4271_MODE2, 4, 1, 0),
	SOC_SINGLE("Soft Ramp Switch", CS4271_DACVOL, 5, 1, 0),
	SOC_SINGLE("Zero Cross Switch", CS4271_DACVOL, 4, 1, 0),
	SOC_SINGLE_BOOL_EXT("De-emphasis Switch", 0,
		cs4271_get_deemph, cs4271_put_deemph),
	SOC_SINGLE("Auto-Mute Switch", CS4271_DACCTL, 7, 1, 0),
	SOC_SINGLE("Slow Roll Off Filter Switch", CS4271_DACCTL, 6, 1, 0),
	SOC_SINGLE("Soft Volume Ramp-Up Switch", CS4271_DACCTL, 3, 1, 0),
	SOC_SINGLE("Soft Ramp-Down Switch", CS4271_DACCTL, 2, 1, 0),
	SOC_SINGLE("Left Channel Inversion Switch", CS4271_DACCTL, 1, 1, 0),
	SOC_SINGLE("Right Channel Inversion Switch", CS4271_DACCTL, 0, 1, 0),
	SOC_DOUBLE("Master Capture Switch", CS4271_ADCCTL, 3, 2, 1, 1),
	SOC_SINGLE("Dither 16-Bit Data Switch", CS4271_ADCCTL, 5, 1, 0),
	SOC_DOUBLE("High Pass Filter Switch", CS4271_ADCCTL, 1, 0, 1, 1),
	SOC_DOUBLE_R("Master Playback Switch", CS4271_VOLA, CS4271_VOLB,
		7, 1, 1),
};

static const struct snd_soc_dai_ops cs4271_dai_ops = {
	.hw_params	= cs4271_hw_params,
	.set_sysclk	= cs4271_set_dai_sysclk,
	.set_fmt	= cs4271_set_dai_fmt,
	.mute_stream	= cs4271_mute_stream,
};

static struct snd_soc_dai_driver cs4271_dai = {
	.name = "cs4271-hifi",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= CS4271_PCM_RATES,
		.formats	= CS4271_PCM_FORMATS,
	},
	.capture = {
		.stream_name	= "Capture",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= CS4271_PCM_RATES,
		.formats	= CS4271_PCM_FORMATS,
	},
	.ops = &cs4271_dai_ops,
	.symmetric_rates = 1,
};

#ifdef CONFIG_PM
static int cs4271_soc_suspend(struct snd_soc_codec *codec)
{
	int ret;
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);

	/* Set power-down bit */
	ret = regmap_update_bits(cs4271->regmap, CS4271_MODE2,
				 CS4271_MODE2_PDN, CS4271_MODE2_PDN);
	if (ret < 0)
		return ret;

	return 0;
}

static int cs4271_soc_resume(struct snd_soc_codec *codec)
{
	int ret;
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);

	/* Restore codec state */
	ret = regcache_sync(cs4271->regmap);
	if (ret < 0)
		return ret;

	/* then disable the power-down bit */
	ret = regmap_update_bits(cs4271->regmap, CS4271_MODE2,
				 CS4271_MODE2_PDN, 0);
	if (ret < 0)
		return ret;

	return 0;
}
#else
#define cs4271_soc_suspend	NULL
#define cs4271_soc_resume	NULL
#endif /* CONFIG_PM */

#ifdef CONFIG_OF
static const struct of_device_id cs4271_dt_ids[] = {
	{ .compatible = "cirrus,cs4271", },
	{ }
};
MODULE_DEVICE_TABLE(of, cs4271_dt_ids);
#endif

static int cs4271_probe(struct snd_soc_codec *codec)
{
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);
	struct cs4271_platform_data *cs4271plat = codec->dev->platform_data;
	int ret;
	bool amutec_eq_bmutec = false;

#ifdef CONFIG_OF
	if (of_match_device(cs4271_dt_ids, codec->dev)) {
		if (of_get_property(codec->dev->of_node,
				     "cirrus,amutec-eq-bmutec", NULL))
			amutec_eq_bmutec = true;

		if (of_get_property(codec->dev->of_node,
				     "cirrus,enable-soft-reset", NULL))
			cs4271->enable_soft_reset = true;
	}
#endif

	if (cs4271plat) {
		amutec_eq_bmutec = cs4271plat->amutec_eq_bmutec;
		cs4271->enable_soft_reset = cs4271plat->enable_soft_reset;
	}

	if (gpio_is_valid(cs4271->gpio_nreset)) {
		/* Reset codec */
		gpio_direction_output(cs4271->gpio_nreset, 0);
		udelay(1);
		gpio_set_value(cs4271->gpio_nreset, 1);
		/* Give the codec time to wake up */
		udelay(1);
	}

	ret = regmap_update_bits(cs4271->regmap, CS4271_MODE2,
				 CS4271_MODE2_PDN | CS4271_MODE2_CPEN,
				 CS4271_MODE2_PDN | CS4271_MODE2_CPEN);
	if (ret < 0)
		return ret;
	ret = regmap_update_bits(cs4271->regmap, CS4271_MODE2,
				 CS4271_MODE2_PDN, 0);
	if (ret < 0)
		return ret;
	/* Power-up sequence requires 85 uS */
	udelay(85);

	if (amutec_eq_bmutec)
		regmap_update_bits(cs4271->regmap, CS4271_MODE2,
				   CS4271_MODE2_MUTECAEQUB,
				   CS4271_MODE2_MUTECAEQUB);

	return 0;
}

static int cs4271_remove(struct snd_soc_codec *codec)
{
	struct cs4271_private *cs4271 = snd_soc_codec_get_drvdata(codec);

	if (gpio_is_valid(cs4271->gpio_nreset))
		/* Set codec to the reset state */
		gpio_set_value(cs4271->gpio_nreset, 0);

	return 0;
};

static struct snd_soc_codec_driver soc_codec_dev_cs4271 = {
	.probe			= cs4271_probe,
	.remove			= cs4271_remove,
	.suspend		= cs4271_soc_suspend,
	.resume			= cs4271_soc_resume,

	.controls		= cs4271_snd_controls,
	.num_controls		= ARRAY_SIZE(cs4271_snd_controls),
	.dapm_widgets		= cs4271_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs4271_dapm_widgets),
	.dapm_routes		= cs4271_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs4271_dapm_routes),
};

static int cs4271_common_probe(struct device *dev,
			       struct cs4271_private **c)
{
	struct cs4271_platform_data *cs4271plat = dev->platform_data;
	struct cs4271_private *cs4271;

	cs4271 = devm_kzalloc(dev, sizeof(*cs4271), GFP_KERNEL);
	if (!cs4271)
		return -ENOMEM;

	if (of_match_device(cs4271_dt_ids, dev))
		cs4271->gpio_nreset =
			of_get_named_gpio(dev->of_node, "reset-gpio", 0);

	if (cs4271plat)
		cs4271->gpio_nreset = cs4271plat->gpio_nreset;

	if (gpio_is_valid(cs4271->gpio_nreset)) {
		int ret;

		ret = devm_gpio_request(dev, cs4271->gpio_nreset,
					"CS4271 Reset");
		if (ret < 0)
			return ret;
	}

	*c = cs4271;
	return 0;
}

#if defined(CONFIG_SPI_MASTER)

static const struct regmap_config cs4271_spi_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = CS4271_LASTREG,
	.read_flag_mask = 0x21,
	.write_flag_mask = 0x20,

	.reg_defaults = cs4271_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs4271_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = cs4271_volatile_reg,
};

static int cs4271_spi_probe(struct spi_device *spi)
{
	struct cs4271_private *cs4271;
	int ret;

	ret = cs4271_common_probe(&spi->dev, &cs4271);
	if (ret < 0)
		return ret;

	spi_set_drvdata(spi, cs4271);
	cs4271->regmap = devm_regmap_init_spi(spi, &cs4271_spi_regmap);
	if (IS_ERR(cs4271->regmap))
		return PTR_ERR(cs4271->regmap);

	return snd_soc_register_codec(&spi->dev, &soc_codec_dev_cs4271,
		&cs4271_dai, 1);
}

static int cs4271_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static struct spi_driver cs4271_spi_driver = {
	.driver = {
		.name	= "cs4271",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(cs4271_dt_ids),
	},
	.probe		= cs4271_spi_probe,
	.remove		= cs4271_spi_remove,
};
#endif /* defined(CONFIG_SPI_MASTER) */

#if IS_ENABLED(CONFIG_I2C)
static const struct i2c_device_id cs4271_i2c_id[] = {
	{"cs4271", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, cs4271_i2c_id);

static const struct regmap_config cs4271_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = CS4271_LASTREG,

	.reg_defaults = cs4271_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs4271_reg_defaults),
	.cache_type = REGCACHE_RBTREE,

	.volatile_reg = cs4271_volatile_reg,
};

static int cs4271_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct cs4271_private *cs4271;
	int ret;

	ret = cs4271_common_probe(&client->dev, &cs4271);
	if (ret < 0)
		return ret;

	i2c_set_clientdata(client, cs4271);
	cs4271->regmap = devm_regmap_init_i2c(client, &cs4271_i2c_regmap);
	if (IS_ERR(cs4271->regmap))
		return PTR_ERR(cs4271->regmap);

	return snd_soc_register_codec(&client->dev, &soc_codec_dev_cs4271,
		&cs4271_dai, 1);
}

static int cs4271_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static struct i2c_driver cs4271_i2c_driver = {
	.driver = {
		.name	= "cs4271",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(cs4271_dt_ids),
	},
	.id_table	= cs4271_i2c_id,
	.probe		= cs4271_i2c_probe,
	.remove		= cs4271_i2c_remove,
};
#endif /* IS_ENABLED(CONFIG_I2C) */

/*
 * We only register our serial bus driver here without
 * assignment to particular chip. So if any of the below
 * fails, there is some problem with I2C or SPI subsystem.
 * In most cases this module will be compiled with support
 * of only one serial bus.
 */
static int __init cs4271_modinit(void)
{
	int ret;

#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&cs4271_i2c_driver);
	if (ret) {
		pr_err("Failed to register CS4271 I2C driver: %d\n", ret);
		return ret;
	}
#endif

#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&cs4271_spi_driver);
	if (ret) {
		pr_err("Failed to register CS4271 SPI driver: %d\n", ret);
		return ret;
	}
#endif

	return 0;
}
module_init(cs4271_modinit);

static void __exit cs4271_modexit(void)
{
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&cs4271_spi_driver);
#endif

#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&cs4271_i2c_driver);
#endif
}
module_exit(cs4271_modexit);

MODULE_AUTHOR("Alexander Sverdlin <subaparts@yandex.ru>");
MODULE_DESCRIPTION("Cirrus Logic CS4271 ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
