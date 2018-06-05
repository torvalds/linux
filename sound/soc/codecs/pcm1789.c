// SPDX-License-Identifier: GPL-2.0
// Audio driver for PCM1789
// Copyright (C) 2018 Bootlin
// Mylène Josserand <mylene.josserand@bootlin.com>

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "pcm1789.h"

#define PCM1789_MUTE_CONTROL	0x10
#define PCM1789_FMT_CONTROL	0x11
#define PCM1789_SOFT_MUTE	0x14
#define PCM1789_DAC_VOL_LEFT	0x18
#define PCM1789_DAC_VOL_RIGHT	0x19

#define PCM1789_FMT_MASK	0x07
#define PCM1789_MUTE_MASK	0x03
#define PCM1789_MUTE_SRET	0x06

struct pcm1789_private {
	struct regmap *regmap;
	unsigned int format;
	unsigned int rate;
	struct gpio_desc *reset;
	struct work_struct work;
	struct device *dev;
};

static const struct reg_default pcm1789_reg_defaults[] = {
	{ PCM1789_FMT_CONTROL, 0x00 },
	{ PCM1789_SOFT_MUTE, 0x00 },
	{ PCM1789_DAC_VOL_LEFT, 0xff },
	{ PCM1789_DAC_VOL_RIGHT, 0xff },
};

static bool pcm1789_accessible_reg(struct device *dev, unsigned int reg)
{
	return reg >= PCM1789_MUTE_CONTROL && reg <= PCM1789_DAC_VOL_RIGHT;
}

static bool pcm1789_writeable_reg(struct device *dev, unsigned int reg)
{
	return pcm1789_accessible_reg(dev, reg);
}

static int pcm1789_set_dai_fmt(struct snd_soc_dai *codec_dai,
			       unsigned int format)
{
	struct snd_soc_component *component = codec_dai->component;
	struct pcm1789_private *priv = snd_soc_component_get_drvdata(component);

	priv->format = format;

	return 0;
}

static int pcm1789_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_component *component = codec_dai->component;
	struct pcm1789_private *priv = snd_soc_component_get_drvdata(component);

	return regmap_update_bits(priv->regmap, PCM1789_SOFT_MUTE,
				  PCM1789_MUTE_MASK,
				  mute ? 0 : PCM1789_MUTE_MASK);
}

static int pcm1789_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *codec_dai)
{
	struct snd_soc_component *component = codec_dai->component;
	struct pcm1789_private *priv = snd_soc_component_get_drvdata(component);
	int val = 0, ret;

	priv->rate = params_rate(params);

	switch (priv->format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 24:
			val = 2;
			break;
		case 16:
			val = 3;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
		switch (params_width(params)) {
		case 16:
		case 24:
		case 32:
			val = 0;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		switch (params_width(params)) {
		case 16:
		case 24:
		case 32:
			val = 1;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		dev_err(component->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, PCM1789_FMT_CONTROL,
				 PCM1789_FMT_MASK, val);
	if (ret < 0)
		return ret;

	return 0;
}

static void pcm1789_work_queue(struct work_struct *work)
{
	struct pcm1789_private *priv = container_of(work,
						    struct pcm1789_private,
						    work);

	/* Perform a software reset to remove codec from desynchronized state */
	if (regmap_update_bits(priv->regmap, PCM1789_MUTE_CONTROL,
			       0x3 << PCM1789_MUTE_SRET, 0) < 0)
		dev_err(priv->dev, "Error while setting SRET");
}

static int pcm1789_trigger(struct snd_pcm_substream *substream, int cmd,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct pcm1789_private *priv = snd_soc_component_get_drvdata(component);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		schedule_work(&priv->work);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct snd_soc_dai_ops pcm1789_dai_ops = {
	.set_fmt	= pcm1789_set_dai_fmt,
	.hw_params	= pcm1789_hw_params,
	.digital_mute	= pcm1789_digital_mute,
	.trigger	= pcm1789_trigger,
};

static const DECLARE_TLV_DB_SCALE(pcm1789_dac_tlv, -12000, 50, 1);

static const struct snd_kcontrol_new pcm1789_controls[] = {
	SOC_DOUBLE_R_RANGE_TLV("DAC Playback Volume", PCM1789_DAC_VOL_LEFT,
			       PCM1789_DAC_VOL_RIGHT, 0, 0xf, 0xff, 0,
			       pcm1789_dac_tlv),
};

static const struct snd_soc_dapm_widget pcm1789_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("IOUTL+"),
	SND_SOC_DAPM_OUTPUT("IOUTL-"),
	SND_SOC_DAPM_OUTPUT("IOUTR+"),
	SND_SOC_DAPM_OUTPUT("IOUTR-"),
};

static const struct snd_soc_dapm_route pcm1789_dapm_routes[] = {
	{ "IOUTL+", NULL, "Playback" },
	{ "IOUTL-", NULL, "Playback" },
	{ "IOUTR+", NULL, "Playback" },
	{ "IOUTR-", NULL, "Playback" },
};

static struct snd_soc_dai_driver pcm1789_dai = {
	.name = "pcm1789-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_CONTINUOUS,
		.rate_min = 10000,
		.rate_max = 200000,
		.formats = PCM1789_FORMATS,
	},
	.ops = &pcm1789_dai_ops,
};

const struct regmap_config pcm1789_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= PCM1789_DAC_VOL_RIGHT,
	.reg_defaults		= pcm1789_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(pcm1789_reg_defaults),
	.writeable_reg		= pcm1789_writeable_reg,
	.readable_reg		= pcm1789_accessible_reg,
};
EXPORT_SYMBOL_GPL(pcm1789_regmap_config);

static const struct snd_soc_component_driver soc_component_dev_pcm1789 = {
	.controls		= pcm1789_controls,
	.num_controls		= ARRAY_SIZE(pcm1789_controls),
	.dapm_widgets		= pcm1789_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(pcm1789_dapm_widgets),
	.dapm_routes		= pcm1789_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(pcm1789_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

int pcm1789_common_init(struct device *dev, struct regmap *regmap)
{
	struct pcm1789_private *pcm1789;

	pcm1789 = devm_kzalloc(dev, sizeof(struct pcm1789_private),
			       GFP_KERNEL);
	if (!pcm1789)
		return -ENOMEM;

	pcm1789->regmap = regmap;
	pcm1789->dev = dev;
	dev_set_drvdata(dev, pcm1789);

	pcm1789->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(pcm1789->reset))
		return PTR_ERR(pcm1789->reset);

	gpiod_set_value_cansleep(pcm1789->reset, 0);
	msleep(300);

	INIT_WORK(&pcm1789->work, pcm1789_work_queue);

	return devm_snd_soc_register_component(dev, &soc_component_dev_pcm1789,
					       &pcm1789_dai, 1);
}
EXPORT_SYMBOL_GPL(pcm1789_common_init);

int pcm1789_common_exit(struct device *dev)
{
	struct pcm1789_private *priv = dev_get_drvdata(dev);

	if (&priv->work)
		flush_work(&priv->work);

	return 0;
}
EXPORT_SYMBOL_GPL(pcm1789_common_exit);

MODULE_DESCRIPTION("ASoC PCM1789 driver");
MODULE_AUTHOR("Mylène Josserand <mylene.josserand@free-electrons.com>");
MODULE_LICENSE("GPL");
