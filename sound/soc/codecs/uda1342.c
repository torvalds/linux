// SPDX-License-Identifier: GPL-2.0-only
//
// uda1342.c  --  UDA1342 ALSA SoC Codec driver
// Based on the WM87xx drivers by Liam Girdwood and Richard Purdie
//
// Copyright 2007 Dension Audio Systems Ltd.
// Copyright 2024 Loongson Technology Co.,Ltd.
//
// Modifications by Christian Pellegrin <chripell@evolware.org>
// Further cleanup and restructuring by:
//         Binbin Zhou <zhoubinbin@loongson.cn>

#include <linux/module.h>
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "uda1342.h"

#define UDA134X_FORMATS	(SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE)

struct uda1342_priv {
	int sysclk;
	int dai_fmt;

	struct snd_pcm_substream *provider_substream;
	struct snd_pcm_substream *consumer_substream;

	struct regmap *regmap;
	struct i2c_client *i2c;
};

static const struct reg_default uda1342_reg_defaults[] = {
	{ 0x00, 0x1042 },
	{ 0x01, 0x0000 },
	{ 0x10, 0x0088 },
	{ 0x11, 0x0000 },
	{ 0x12, 0x0000 },
	{ 0x20, 0x0080 },
	{ 0x21, 0x0080 },
};

static int uda1342_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct uda1342_priv *uda1342 = snd_soc_component_get_drvdata(component);
	unsigned int mask;
	unsigned int val = 0;

	/* Master mute */
	mask = BIT(5);
	if (mute)
		val = mask;

	return regmap_update_bits(uda1342->regmap, 0x10, mask, val);
}

static int uda1342_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct uda1342_priv *uda1342 = snd_soc_component_get_drvdata(component);
	struct snd_pcm_runtime *provider_runtime;

	if (uda1342->provider_substream) {
		provider_runtime = uda1342->provider_substream->runtime;

		snd_pcm_hw_constraint_single(substream->runtime,
					     SNDRV_PCM_HW_PARAM_RATE, provider_runtime->rate);
		snd_pcm_hw_constraint_single(substream->runtime,
					     SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
					     provider_runtime->sample_bits);

		uda1342->consumer_substream = substream;
	} else {
		uda1342->provider_substream = substream;
	}

	return 0;
}

static void uda1342_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct uda1342_priv *uda1342 = snd_soc_component_get_drvdata(component);

	if (uda1342->provider_substream == substream)
		uda1342->provider_substream = uda1342->consumer_substream;

	uda1342->consumer_substream = NULL;
}

static int uda1342_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct uda1342_priv *uda1342 = snd_soc_component_get_drvdata(component);
	struct device *dev = &uda1342->i2c->dev;
	unsigned int hw_params = 0;

	if (substream == uda1342->consumer_substream)
		return 0;

	/* set SYSCLK / fs ratio */
	switch (uda1342->sysclk / params_rate(params)) {
	case 512:
		break;
	case 384:
		hw_params |= BIT(4);
		break;
	case 256:
		hw_params |= BIT(5);
		break;
	default:
		dev_err(dev, "unsupported frequency\n");
		return -EINVAL;
	}

	/* set DAI format and word length */
	switch (uda1342->dai_fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 16:
			hw_params |= BIT(1);
			break;
		case 18:
			hw_params |= BIT(2);
			break;
		case 20:
			hw_params |= BIT(2) | BIT(1);
			break;
		default:
			dev_err(dev, "unsupported format (right)\n");
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		hw_params |= BIT(3);
		break;
	default:
		dev_err(dev, "unsupported format\n");
		return -EINVAL;
	}

	return regmap_update_bits(uda1342->regmap, 0x0,
				  STATUS0_DAIFMT_MASK | STATUS0_SYSCLK_MASK, hw_params);
}

static int uda1342_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct uda1342_priv *uda1342 = snd_soc_component_get_drvdata(component);
	struct device *dev = &uda1342->i2c->dev;

	/*
	 * Anything between 256fs*8Khz and 512fs*48Khz should be acceptable
	 * because the codec is slave. Of course limitations of the clock
	 * master (the IIS controller) apply.
	 * We'll error out on set_hw_params if it's not OK
	 */
	if ((freq >= (256 * 8000)) && (freq <= (512 * 48000))) {
		uda1342->sysclk = freq;
		return 0;
	}

	dev_err(dev, "unsupported sysclk\n");

	return -EINVAL;
}

static int uda1342_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct uda1342_priv *uda1342 = snd_soc_component_get_drvdata(component);

	/* codec supports only full consumer mode */
	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_BC_FC) {
		dev_err(&uda1342->i2c->dev, "unsupported consumer mode.\n");
		return -EINVAL;
	}

	/* We can't setup DAI format here as it depends on the word bit num */
	/* so let's just store the value for later */
	uda1342->dai_fmt = fmt;

	return 0;
}

static const struct snd_kcontrol_new uda1342_snd_controls[] = {
	SOC_SINGLE("Master Playback Volume", 0x11, 0, 0x3F, 1),
	SOC_SINGLE("Analog1 Volume", 0x12, 0, 0x1F, 1),
};

/* Common DAPM widgets */
static const struct snd_soc_dapm_widget uda1342_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("VINL1"),
	SND_SOC_DAPM_INPUT("VINR1"),
	SND_SOC_DAPM_INPUT("VINL2"),
	SND_SOC_DAPM_INPUT("VINR2"),

	SND_SOC_DAPM_DAC("DAC", "Playback", 0, 1, 0),
	SND_SOC_DAPM_ADC("ADC", "Capture", 0, 9, 0),

	SND_SOC_DAPM_OUTPUT("VOUTL"),
	SND_SOC_DAPM_OUTPUT("VOUTR"),
};

static const struct snd_soc_dapm_route uda1342_dapm_routes[] = {
	{ "ADC", NULL, "VINL1" },
	{ "ADC", NULL, "VINR1" },
	{ "ADC", NULL, "VINL2" },
	{ "ADC", NULL, "VINR2" },
	{ "VOUTL", NULL, "DAC" },
	{ "VOUTR", NULL, "DAC" },
};

static const struct snd_soc_dai_ops uda1342_dai_ops = {
	.startup	= uda1342_startup,
	.shutdown	= uda1342_shutdown,
	.hw_params	= uda1342_hw_params,
	.mute_stream	= uda1342_mute,
	.set_sysclk	= uda1342_set_dai_sysclk,
	.set_fmt	= uda1342_set_dai_fmt,
};

static struct snd_soc_dai_driver uda1342_dai = {
	.name = "uda1342-hifi",
	/* playback capabilities */
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = UDA134X_FORMATS,
	},
	/* capture capabilities */
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = UDA134X_FORMATS,
	},
	/* pcm operations */
	.ops = &uda1342_dai_ops,
};

static const struct snd_soc_component_driver soc_component_dev_uda1342 = {
	.controls		= uda1342_snd_controls,
	.num_controls		= ARRAY_SIZE(uda1342_snd_controls),
	.dapm_widgets		= uda1342_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(uda1342_dapm_widgets),
	.dapm_routes		= uda1342_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(uda1342_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config uda1342_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = 0x21,
	.reg_defaults = uda1342_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(uda1342_reg_defaults),
	.cache_type = REGCACHE_MAPLE,
};

static int uda1342_i2c_probe(struct i2c_client *i2c)
{
	struct uda1342_priv *uda1342;

	uda1342 = devm_kzalloc(&i2c->dev, sizeof(*uda1342), GFP_KERNEL);
	if (!uda1342)
		return -ENOMEM;

	uda1342->regmap = devm_regmap_init_i2c(i2c, &uda1342_regmap);
	if (IS_ERR(uda1342->regmap))
		return PTR_ERR(uda1342->regmap);

	i2c_set_clientdata(i2c, uda1342);
	uda1342->i2c = i2c;

	return devm_snd_soc_register_component(&i2c->dev,
					       &soc_component_dev_uda1342,
					       &uda1342_dai, 1);
}

static int uda1342_suspend(struct device *dev)
{
	struct uda1342_priv *uda1342 = dev_get_drvdata(dev);

	regcache_cache_only(uda1342->regmap, true);

	return 0;
}

static int uda1342_resume(struct device *dev)
{
	struct uda1342_priv *uda1342 = dev_get_drvdata(dev);

	regcache_mark_dirty(uda1342->regmap);
	regcache_sync(uda1342->regmap);

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(uda1342_pm_ops,
				 uda1342_suspend, uda1342_resume, NULL);

static const struct i2c_device_id uda1342_i2c_id[] = {
	 { "uda1342" },
	 { }
};
MODULE_DEVICE_TABLE(i2c, uda1342_i2c_id);

static const struct of_device_id uda1342_of_match[] = {
	 { .compatible = "nxp,uda1342" },
	 { }
};
MODULE_DEVICE_TABLE(of, uda1342_of_match);

static struct i2c_driver uda1342_i2c_driver = {
	.driver = {
		.name =  "uda1342",
		.of_match_table = uda1342_of_match,
		.pm = pm_sleep_ptr(&uda1342_pm_ops),
	 },
	.probe = uda1342_i2c_probe,
	.id_table = uda1342_i2c_id,
};
module_i2c_driver(uda1342_i2c_driver);

MODULE_DESCRIPTION("UDA1342 ALSA soc codec driver");
MODULE_AUTHOR("Zoltan Devai, Christian Pellegrin <chripell@evolware.org>");
MODULE_AUTHOR("Binbin Zhou <zhoubinbin@loongson.cn>");
MODULE_LICENSE("GPL");
