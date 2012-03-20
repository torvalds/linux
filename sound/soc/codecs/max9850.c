/*
 * max9850.c  --  codec driver for max9850
 *
 * Copyright (C) 2011 taskit GmbH
 *
 * Author: Christian Glindkamp <christian.glindkamp@taskit.de>
 *
 * Initial development of this code was funded by
 * MICRONIC Computer Systeme GmbH, http://www.mcsberlin.de/
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "max9850.h"

struct max9850_priv {
	unsigned int sysclk;
};

/* max9850 register cache */
static const u8 max9850_reg[MAX9850_CACHEREGNUM] = {
	0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* these registers are not used at the moment but provided for the sake of
 * completeness */
static int max9850_volatile_register(struct snd_soc_codec *codec,
		unsigned int reg)
{
	switch (reg) {
	case MAX9850_STATUSA:
	case MAX9850_STATUSB:
		return 1;
	default:
		return 0;
	}
}

static const unsigned int max9850_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0x18, 0x1f, TLV_DB_SCALE_ITEM(-7450, 400, 0),
	0x20, 0x33, TLV_DB_SCALE_ITEM(-4150, 200, 0),
	0x34, 0x37, TLV_DB_SCALE_ITEM(-150, 100, 0),
	0x38, 0x3f, TLV_DB_SCALE_ITEM(250, 50, 0),
};

static const struct snd_kcontrol_new max9850_controls[] = {
SOC_SINGLE_TLV("Headphone Volume", MAX9850_VOLUME, 0, 0x3f, 1, max9850_tlv),
SOC_SINGLE("Headphone Switch", MAX9850_VOLUME, 7, 1, 1),
SOC_SINGLE("Mono Switch", MAX9850_GENERAL_PURPOSE, 2, 1, 0),
};

static const struct snd_kcontrol_new max9850_mixer_controls[] = {
	SOC_DAPM_SINGLE("Line In Switch", MAX9850_ENABLE, 1, 1, 0),
};

static const struct snd_soc_dapm_widget max9850_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("Charge Pump 1", MAX9850_ENABLE, 4, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("Charge Pump 2", MAX9850_ENABLE, 5, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MCLK", MAX9850_ENABLE, 6, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("SHDN", MAX9850_ENABLE, 7, 0, NULL, 0),
SND_SOC_DAPM_MIXER_NAMED_CTL("Output Mixer", MAX9850_ENABLE, 2, 0,
		&max9850_mixer_controls[0],
		ARRAY_SIZE(max9850_mixer_controls)),
SND_SOC_DAPM_PGA("Headphone Output", MAX9850_ENABLE, 3, 0, NULL, 0),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", MAX9850_ENABLE, 0, 0),
SND_SOC_DAPM_OUTPUT("OUTL"),
SND_SOC_DAPM_OUTPUT("HPL"),
SND_SOC_DAPM_OUTPUT("OUTR"),
SND_SOC_DAPM_OUTPUT("HPR"),
SND_SOC_DAPM_MIXER("Line Input", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_INPUT("INL"),
SND_SOC_DAPM_INPUT("INR"),
};

static const struct snd_soc_dapm_route max9850_dapm_routes[] = {
	/* output mixer */
	{"Output Mixer", NULL, "DAC"},
	{"Output Mixer", "Line In Switch", "Line Input"},

	/* outputs */
	{"Headphone Output", NULL, "Output Mixer"},
	{"HPL", NULL, "Headphone Output"},
	{"HPR", NULL, "Headphone Output"},
	{"OUTL", NULL, "Output Mixer"},
	{"OUTR", NULL, "Output Mixer"},

	/* inputs */
	{"Line Input", NULL, "INL"},
	{"Line Input", NULL, "INR"},

	/* supplies */
	{"Output Mixer", NULL, "Charge Pump 1"},
	{"Output Mixer", NULL, "Charge Pump 2"},
	{"Output Mixer", NULL, "SHDN"},
	{"DAC", NULL, "MCLK"},
};

static int max9850_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct max9850_priv *max9850 = snd_soc_codec_get_drvdata(codec);
	u64 lrclk_div;
	u8 sf, da;

	if (!max9850->sysclk)
		return -EINVAL;

	/* lrclk_div = 2^22 * rate / iclk with iclk = mclk / sf */
	sf = (snd_soc_read(codec, MAX9850_CLOCK) >> 2) + 1;
	lrclk_div = (1 << 22);
	lrclk_div *= params_rate(params);
	lrclk_div *= sf;
	do_div(lrclk_div, max9850->sysclk);

	snd_soc_write(codec, MAX9850_LRCLK_MSB, (lrclk_div >> 8) & 0x7f);
	snd_soc_write(codec, MAX9850_LRCLK_LSB, lrclk_div & 0xff);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		da = 0;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		da = 0x2;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		da = 0x3;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_update_bits(codec, MAX9850_DIGITAL_AUDIO, 0x3, da);

	return 0;
}

static int max9850_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct max9850_priv *max9850 = snd_soc_codec_get_drvdata(codec);

	/* calculate mclk -> iclk divider */
	if (freq <= 13000000)
		snd_soc_write(codec, MAX9850_CLOCK, 0x0);
	else if (freq <= 26000000)
		snd_soc_write(codec, MAX9850_CLOCK, 0x4);
	else if (freq <= 40000000)
		snd_soc_write(codec, MAX9850_CLOCK, 0x8);
	else
		return -EINVAL;

	max9850->sysclk = freq;
	return 0;
}

static int max9850_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u8 da = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		da |= MAX9850_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		da |= MAX9850_DLY;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		da |= MAX9850_RTJ;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		da |= MAX9850_BCINV | MAX9850_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		da |= MAX9850_BCINV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		da |= MAX9850_INV;
		break;
	default:
		return -EINVAL;
	}

	/* set da */
	snd_soc_write(codec, MAX9850_DIGITAL_AUDIO, da);

	return 0;
}

static int max9850_set_bias_level(struct snd_soc_codec *codec,
				  enum snd_soc_bias_level level)
{
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = snd_soc_cache_sync(codec);
			if (ret) {
				dev_err(codec->dev,
					"Failed to sync cache: %d\n", ret);
				return ret;
			}
		}
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define MAX9850_RATES SNDRV_PCM_RATE_8000_48000

#define MAX9850_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops max9850_dai_ops = {
	.hw_params	= max9850_hw_params,
	.set_sysclk	= max9850_set_dai_sysclk,
	.set_fmt	= max9850_set_dai_fmt,
};

static struct snd_soc_dai_driver max9850_dai = {
	.name = "max9850-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = MAX9850_RATES,
		.formats = MAX9850_FORMATS
	},
	.ops = &max9850_dai_ops,
};

#ifdef CONFIG_PM
static int max9850_suspend(struct snd_soc_codec *codec)
{
	max9850_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int max9850_resume(struct snd_soc_codec *codec)
{
	max9850_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define max9850_suspend NULL
#define max9850_resume NULL
#endif

static int max9850_probe(struct snd_soc_codec *codec)
{
	int ret;

	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_I2C);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	/* enable zero-detect */
	snd_soc_update_bits(codec, MAX9850_GENERAL_PURPOSE, 1, 1);
	/* enable slew-rate control */
	snd_soc_update_bits(codec, MAX9850_VOLUME, 0x40, 0x40);
	/* set slew-rate 125ms */
	snd_soc_update_bits(codec, MAX9850_CHARGE_PUMP, 0xff, 0xc0);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_max9850 = {
	.probe =	max9850_probe,
	.suspend =	max9850_suspend,
	.resume =	max9850_resume,
	.set_bias_level = max9850_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(max9850_reg),
	.reg_word_size = sizeof(u8),
	.reg_cache_default = max9850_reg,
	.volatile_register = max9850_volatile_register,

	.controls = max9850_controls,
	.num_controls = ARRAY_SIZE(max9850_controls),
	.dapm_widgets = max9850_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(max9850_dapm_widgets),
	.dapm_routes = max9850_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(max9850_dapm_routes),
};

static int __devinit max9850_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct max9850_priv *max9850;
	int ret;

	max9850 = devm_kzalloc(&i2c->dev, sizeof(struct max9850_priv),
			       GFP_KERNEL);
	if (max9850 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max9850);

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_max9850, &max9850_dai, 1);
	return ret;
}

static __devexit int max9850_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id max9850_i2c_id[] = {
	{ "max9850", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9850_i2c_id);

static struct i2c_driver max9850_i2c_driver = {
	.driver = {
		.name = "max9850",
		.owner = THIS_MODULE,
	},
	.probe = max9850_i2c_probe,
	.remove = __devexit_p(max9850_i2c_remove),
	.id_table = max9850_i2c_id,
};

static int __init max9850_init(void)
{
	return i2c_add_driver(&max9850_i2c_driver);
}
module_init(max9850_init);

static void __exit max9850_exit(void)
{
	i2c_del_driver(&max9850_i2c_driver);
}
module_exit(max9850_exit);

MODULE_AUTHOR("Christian Glindkamp <christian.glindkamp@taskit.de>");
MODULE_DESCRIPTION("ASoC MAX9850 codec driver");
MODULE_LICENSE("GPL");
