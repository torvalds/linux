 /*
 * Audio Codec driver supporting:
 *  AD1835A, AD1836, AD1837A, AD1838A, AD1839A
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>

#include "ad1836.h"

enum ad1836_type {
	AD1835,
	AD1836,
	AD1838,
};

/* codec private data */
struct ad1836_priv {
	enum ad1836_type type;
	struct regmap *regmap;
};

/*
 * AD1836 volume/mute/de-emphasis etc. controls
 */
static const char *ad1836_deemp[] = {"None", "44.1kHz", "32kHz", "48kHz"};

static const struct soc_enum ad1836_deemp_enum =
	SOC_ENUM_SINGLE(AD1836_DAC_CTRL1, 8, 4, ad1836_deemp);

#define AD1836_DAC_VOLUME(x) \
	SOC_DOUBLE_R("DAC" #x " Playback Volume", AD1836_DAC_L_VOL(x), \
			AD1836_DAC_R_VOL(x), 0, 0x3FF, 0)

#define AD1836_DAC_SWITCH(x) \
	SOC_DOUBLE("DAC" #x " Playback Switch", AD1836_DAC_CTRL2, \
			AD1836_MUTE_LEFT(x), AD1836_MUTE_RIGHT(x), 1, 1)

#define AD1836_ADC_SWITCH(x) \
	SOC_DOUBLE("ADC" #x " Capture Switch", AD1836_ADC_CTRL2, \
		AD1836_MUTE_LEFT(x), AD1836_MUTE_RIGHT(x), 1, 1)

static const struct snd_kcontrol_new ad183x_dac_controls[] = {
	AD1836_DAC_VOLUME(1),
	AD1836_DAC_SWITCH(1),
	AD1836_DAC_VOLUME(2),
	AD1836_DAC_SWITCH(2),
	AD1836_DAC_VOLUME(3),
	AD1836_DAC_SWITCH(3),
	AD1836_DAC_VOLUME(4),
	AD1836_DAC_SWITCH(4),
};

static const struct snd_soc_dapm_widget ad183x_dac_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("DAC1OUT"),
	SND_SOC_DAPM_OUTPUT("DAC2OUT"),
	SND_SOC_DAPM_OUTPUT("DAC3OUT"),
	SND_SOC_DAPM_OUTPUT("DAC4OUT"),
};

static const struct snd_soc_dapm_route ad183x_dac_routes[] = {
	{ "DAC1OUT", NULL, "DAC" },
	{ "DAC2OUT", NULL, "DAC" },
	{ "DAC3OUT", NULL, "DAC" },
	{ "DAC4OUT", NULL, "DAC" },
};

static const struct snd_kcontrol_new ad183x_adc_controls[] = {
	AD1836_ADC_SWITCH(1),
	AD1836_ADC_SWITCH(2),
	AD1836_ADC_SWITCH(3),
};

static const struct snd_soc_dapm_widget ad183x_adc_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("ADC1IN"),
	SND_SOC_DAPM_INPUT("ADC2IN"),
};

static const struct snd_soc_dapm_route ad183x_adc_routes[] = {
	{ "ADC", NULL, "ADC1IN" },
	{ "ADC", NULL, "ADC2IN" },
};

static const struct snd_kcontrol_new ad183x_controls[] = {
	/* ADC high-pass filter */
	SOC_SINGLE("ADC High Pass Filter Switch", AD1836_ADC_CTRL1,
			AD1836_ADC_HIGHPASS_FILTER, 1, 0),

	/* DAC de-emphasis */
	SOC_ENUM("Playback Deemphasis", ad1836_deemp_enum),
};

static const struct snd_soc_dapm_widget ad183x_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", AD1836_DAC_CTRL1,
				AD1836_DAC_POWERDOWN, 1),
	SND_SOC_DAPM_ADC("ADC", "Capture", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("ADC_PWR", AD1836_ADC_CTRL1,
				AD1836_ADC_POWERDOWN, 1, NULL, 0),
};

static const struct snd_soc_dapm_route ad183x_dapm_routes[] = {
	{ "DAC", NULL, "ADC_PWR" },
	{ "ADC", NULL, "ADC_PWR" },
};

static const DECLARE_TLV_DB_SCALE(ad1836_in_tlv, 0, 300, 0);

static const struct snd_kcontrol_new ad1836_controls[] = {
	SOC_DOUBLE_TLV("ADC2 Capture Volume", AD1836_ADC_CTRL1, 3, 0, 4, 0,
	    ad1836_in_tlv),
};

/*
 * DAI ops entries
 */

static int ad1836_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	/* at present, we support adc aux mode to interface with
	 * blackfin sport tdm mode
	 */
	case SND_SOC_DAIFMT_DSP_A:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	/* ALCLK,ABCLK are both output, AD1836 can only be master */
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ad1836_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct ad1836_priv *ad1836 = snd_soc_codec_get_drvdata(dai->codec);
	int word_len = 0;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		word_len = AD1836_WORD_LEN_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		word_len = AD1836_WORD_LEN_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		word_len = AD1836_WORD_LEN_24;
		break;
	}

	regmap_update_bits(ad1836->regmap, AD1836_DAC_CTRL1,
		AD1836_DAC_WORD_LEN_MASK,
		word_len << AD1836_DAC_WORD_LEN_OFFSET);

	regmap_update_bits(ad1836->regmap, AD1836_ADC_CTRL2,
		AD1836_ADC_WORD_LEN_MASK,
		word_len << AD1836_ADC_WORD_OFFSET);

	return 0;
}

static const struct snd_soc_dai_ops ad1836_dai_ops = {
	.hw_params = ad1836_hw_params,
	.set_fmt = ad1836_set_dai_fmt,
};

#define AD183X_DAI(_name, num_dacs, num_adcs) \
{ \
	.name = _name "-hifi", \
	.playback = { \
		.stream_name = "Playback", \
		.channels_min = 2, \
		.channels_max = (num_dacs) * 2, \
		.rates = SNDRV_PCM_RATE_48000,  \
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE, \
	}, \
	.capture = { \
		.stream_name = "Capture", \
		.channels_min = 2, \
		.channels_max = (num_adcs) * 2, \
		.rates = SNDRV_PCM_RATE_48000, \
		.formats = SNDRV_PCM_FMTBIT_S32_LE | SNDRV_PCM_FMTBIT_S16_LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_S24_LE, \
	}, \
	.ops = &ad1836_dai_ops, \
}

static struct snd_soc_dai_driver ad183x_dais[] = {
	[AD1835] = AD183X_DAI("ad1835", 4, 1),
	[AD1836] = AD183X_DAI("ad1836", 3, 2),
	[AD1838] = AD183X_DAI("ad1838", 3, 1),
};

#ifdef CONFIG_PM
static int ad1836_suspend(struct snd_soc_codec *codec)
{
	struct ad1836_priv *ad1836 = snd_soc_codec_get_drvdata(codec);
	/* reset clock control mode */
	return regmap_update_bits(ad1836->regmap, AD1836_ADC_CTRL2,
		AD1836_ADC_SERFMT_MASK, 0);
}

static int ad1836_resume(struct snd_soc_codec *codec)
{
	struct ad1836_priv *ad1836 = snd_soc_codec_get_drvdata(codec);
	/* restore clock control mode */
	return regmap_update_bits(ad1836->regmap, AD1836_ADC_CTRL2,
		AD1836_ADC_SERFMT_MASK, AD1836_ADC_AUX);
}
#else
#define ad1836_suspend NULL
#define ad1836_resume  NULL
#endif

static int ad1836_probe(struct snd_soc_codec *codec)
{
	struct ad1836_priv *ad1836 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int num_dacs, num_adcs;
	int ret = 0;
	int i;

	num_dacs = ad183x_dais[ad1836->type].playback.channels_max / 2;
	num_adcs = ad183x_dais[ad1836->type].capture.channels_max / 2;

	/* default setting for ad1836 */
	/* de-emphasis: 48kHz, power-on dac */
	regmap_write(ad1836->regmap, AD1836_DAC_CTRL1, 0x300);
	/* unmute dac channels */
	regmap_write(ad1836->regmap, AD1836_DAC_CTRL2, 0x0);
	/* high-pass filter enable, power-on adc */
	regmap_write(ad1836->regmap, AD1836_ADC_CTRL1, 0x100);
	/* unmute adc channles, adc aux mode */
	regmap_write(ad1836->regmap, AD1836_ADC_CTRL2, 0x180);
	/* volume */
	for (i = 1; i <= num_dacs; ++i) {
		regmap_write(ad1836->regmap, AD1836_DAC_L_VOL(i), 0x3FF);
		regmap_write(ad1836->regmap, AD1836_DAC_R_VOL(i), 0x3FF);
	}

	if (ad1836->type == AD1836) {
		/* left/right diff:PGA/MUX */
		regmap_write(ad1836->regmap, AD1836_ADC_CTRL3, 0x3A);
		ret = snd_soc_add_codec_controls(codec, ad1836_controls,
				ARRAY_SIZE(ad1836_controls));
		if (ret)
			return ret;
	} else {
		regmap_write(ad1836->regmap, AD1836_ADC_CTRL3, 0x00);
	}

	ret = snd_soc_add_codec_controls(codec, ad183x_dac_controls, num_dacs * 2);
	if (ret)
		return ret;

	ret = snd_soc_add_codec_controls(codec, ad183x_adc_controls, num_adcs);
	if (ret)
		return ret;

	ret = snd_soc_dapm_new_controls(dapm, ad183x_dac_dapm_widgets, num_dacs);
	if (ret)
		return ret;

	ret = snd_soc_dapm_new_controls(dapm, ad183x_adc_dapm_widgets, num_adcs);
	if (ret)
		return ret;

	ret = snd_soc_dapm_add_routes(dapm, ad183x_dac_routes, num_dacs);
	if (ret)
		return ret;

	ret = snd_soc_dapm_add_routes(dapm, ad183x_adc_routes, num_adcs);
	if (ret)
		return ret;

	return ret;
}

/* power down chip */
static int ad1836_remove(struct snd_soc_codec *codec)
{
	struct ad1836_priv *ad1836 = snd_soc_codec_get_drvdata(codec);
	/* reset clock control mode */
	return regmap_update_bits(ad1836->regmap, AD1836_ADC_CTRL2,
		AD1836_ADC_SERFMT_MASK, 0);
}

static struct snd_soc_codec_driver soc_codec_dev_ad1836 = {
	.probe = ad1836_probe,
	.remove = ad1836_remove,
	.suspend = ad1836_suspend,
	.resume = ad1836_resume,

	.controls = ad183x_controls,
	.num_controls = ARRAY_SIZE(ad183x_controls),
	.dapm_widgets = ad183x_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ad183x_dapm_widgets),
	.dapm_routes = ad183x_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(ad183x_dapm_routes),
};

static const struct reg_default ad1836_reg_defaults[] = {
	{ AD1836_DAC_CTRL1, 0x0000 },
	{ AD1836_DAC_CTRL2, 0x0000 },
	{ AD1836_DAC_L_VOL(0), 0x0000 },
	{ AD1836_DAC_R_VOL(0), 0x0000 },
	{ AD1836_DAC_L_VOL(1), 0x0000 },
	{ AD1836_DAC_R_VOL(1), 0x0000 },
	{ AD1836_DAC_L_VOL(2), 0x0000 },
	{ AD1836_DAC_R_VOL(2), 0x0000 },
	{ AD1836_DAC_L_VOL(3), 0x0000 },
	{ AD1836_DAC_R_VOL(3), 0x0000 },
	{ AD1836_ADC_CTRL1, 0x0000 },
	{ AD1836_ADC_CTRL2, 0x0000 },
	{ AD1836_ADC_CTRL3, 0x0000 },
};

static const struct regmap_config ad1836_regmap_config = {
	.val_bits = 12,
	.reg_bits = 4,
	.read_flag_mask = 0x08,

	.max_register = AD1836_ADC_CTRL3,
	.reg_defaults = ad1836_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ad1836_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static int ad1836_spi_probe(struct spi_device *spi)
{
	struct ad1836_priv *ad1836;
	int ret;

	ad1836 = devm_kzalloc(&spi->dev, sizeof(struct ad1836_priv),
			      GFP_KERNEL);
	if (ad1836 == NULL)
		return -ENOMEM;

	ad1836->regmap = devm_regmap_init_spi(spi, &ad1836_regmap_config);
	if (IS_ERR(ad1836->regmap))
		return PTR_ERR(ad1836->regmap);

	ad1836->type = spi_get_device_id(spi)->driver_data;

	spi_set_drvdata(spi, ad1836);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_ad1836, &ad183x_dais[ad1836->type], 1);
	return ret;
}

static int ad1836_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static const struct spi_device_id ad1836_ids[] = {
	{ "ad1835", AD1835 },
	{ "ad1836", AD1836 },
	{ "ad1837", AD1835 },
	{ "ad1838", AD1838 },
	{ "ad1839", AD1838 },
	{ },
};
MODULE_DEVICE_TABLE(spi, ad1836_ids);

static struct spi_driver ad1836_spi_driver = {
	.driver = {
		.name	= "ad1836",
		.owner	= THIS_MODULE,
	},
	.probe		= ad1836_spi_probe,
	.remove		= ad1836_spi_remove,
	.id_table	= ad1836_ids,
};

module_spi_driver(ad1836_spi_driver);

MODULE_DESCRIPTION("ASoC ad1836 driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
