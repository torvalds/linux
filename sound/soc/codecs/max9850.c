// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max9850.c  --  codec driver for max9850
 *
 * Copyright (C) 2011 taskit GmbH
 *
 * Author: Christian Glindkamp <christian.glindkamp@taskit.de>
 *
 * Initial development of this code was funded by
 * MICRONIC Computer Systeme GmbH, http://www.mcsberlin.de/
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "max9850.h"

struct max9850_priv {
	struct regmap *regmap;
	unsigned int sysclk;
};

/* these registers are not used at the moment but provided for the sake of
 * completeness */
static bool max9850_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX9850_STATUSA:
	case MAX9850_STATUSB:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max9850_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = MAX9850_DIGITAL_AUDIO,
	.volatile_reg = max9850_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};

static const DECLARE_TLV_DB_RANGE(max9850_tlv,
	0x18, 0x1f, TLV_DB_SCALE_ITEM(-7450, 400, 0),
	0x20, 0x33, TLV_DB_SCALE_ITEM(-4150, 200, 0),
	0x34, 0x37, TLV_DB_SCALE_ITEM(-150, 100, 0),
	0x38, 0x3f, TLV_DB_SCALE_ITEM(250, 50, 0)
);

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
	struct snd_soc_component *component = dai->component;
	struct max9850_priv *max9850 = snd_soc_component_get_drvdata(component);
	u64 lrclk_div;
	u8 sf, da;

	if (!max9850->sysclk)
		return -EINVAL;

	/* lrclk_div = 2^22 * rate / iclk with iclk = mclk / sf */
	sf = (snd_soc_component_read32(component, MAX9850_CLOCK) >> 2) + 1;
	lrclk_div = (1 << 22);
	lrclk_div *= params_rate(params);
	lrclk_div *= sf;
	do_div(lrclk_div, max9850->sysclk);

	snd_soc_component_write(component, MAX9850_LRCLK_MSB, (lrclk_div >> 8) & 0x7f);
	snd_soc_component_write(component, MAX9850_LRCLK_LSB, lrclk_div & 0xff);

	switch (params_width(params)) {
	case 16:
		da = 0;
		break;
	case 20:
		da = 0x2;
		break;
	case 24:
		da = 0x3;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, MAX9850_DIGITAL_AUDIO, 0x3, da);

	return 0;
}

static int max9850_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max9850_priv *max9850 = snd_soc_component_get_drvdata(component);

	/* calculate mclk -> iclk divider */
	if (freq <= 13000000)
		snd_soc_component_write(component, MAX9850_CLOCK, 0x0);
	else if (freq <= 26000000)
		snd_soc_component_write(component, MAX9850_CLOCK, 0x4);
	else if (freq <= 40000000)
		snd_soc_component_write(component, MAX9850_CLOCK, 0x8);
	else
		return -EINVAL;

	max9850->sysclk = freq;
	return 0;
}

static int max9850_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
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
	snd_soc_component_write(component, MAX9850_DIGITAL_AUDIO, da);

	return 0;
}

static int max9850_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	struct max9850_priv *max9850 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regcache_sync(max9850->regmap);
			if (ret) {
				dev_err(component->dev,
					"Failed to sync cache: %d\n", ret);
				return ret;
			}
		}
		break;
	case SND_SOC_BIAS_OFF:
		break;
	}
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

static int max9850_probe(struct snd_soc_component *component)
{
	/* enable zero-detect */
	snd_soc_component_update_bits(component, MAX9850_GENERAL_PURPOSE, 1, 1);
	/* enable slew-rate control */
	snd_soc_component_update_bits(component, MAX9850_VOLUME, 0x40, 0x40);
	/* set slew-rate 125ms */
	snd_soc_component_update_bits(component, MAX9850_CHARGE_PUMP, 0xff, 0xc0);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_max9850 = {
	.probe			= max9850_probe,
	.set_bias_level		= max9850_set_bias_level,
	.controls		= max9850_controls,
	.num_controls		= ARRAY_SIZE(max9850_controls),
	.dapm_widgets		= max9850_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max9850_dapm_widgets),
	.dapm_routes		= max9850_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(max9850_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int max9850_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct max9850_priv *max9850;
	int ret;

	max9850 = devm_kzalloc(&i2c->dev, sizeof(struct max9850_priv),
			       GFP_KERNEL);
	if (max9850 == NULL)
		return -ENOMEM;

	max9850->regmap = devm_regmap_init_i2c(i2c, &max9850_regmap);
	if (IS_ERR(max9850->regmap))
		return PTR_ERR(max9850->regmap);

	i2c_set_clientdata(i2c, max9850);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_max9850, &max9850_dai, 1);
	return ret;
}

static const struct i2c_device_id max9850_i2c_id[] = {
	{ "max9850", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9850_i2c_id);

static struct i2c_driver max9850_i2c_driver = {
	.driver = {
		.name = "max9850",
	},
	.probe = max9850_i2c_probe,
	.id_table = max9850_i2c_id,
};

module_i2c_driver(max9850_i2c_driver);

MODULE_AUTHOR("Christian Glindkamp <christian.glindkamp@taskit.de>");
MODULE_DESCRIPTION("ASoC MAX9850 codec driver");
MODULE_LICENSE("GPL");
