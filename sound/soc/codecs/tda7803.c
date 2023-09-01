// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/soc.h>

#include "tda7803.h"

#define TDA7803_SAMPLE_RATE \
		(SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | \
		 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000)

struct tda7803_priv {
	struct regmap *regmap;
	u32 input_format;
};

static int tda7803_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tda7803_priv *tda7803 = snd_soc_component_get_drvdata(component);
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	int val = 0;

	snd_soc_component_write(component, TDA7803_REG2, DIGITAL_MUTE_OFF |
				CH2_4_UMUTE | CH1_3_UMUTE |
				MUTE_TIME_SETTING_1_45MS);
	snd_soc_component_write(component, TDA7803_REG7, AMPLIEFIR_SWITCH_ON);

	switch (tda7803->input_format) {
	case 0:
		val = INPUT_FORMAT_TDM_8CH_MODEL1;
		break;
	case 1:
		val = INPUT_FORMAT_TDM_8CH_MODEL2;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_dai_set_fmt(codec_dai, val);

	return 0;
}

static int tda7803_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int val = 0;

	switch (params_rate(params)) {
	case 44100:
		val = SAMPLE_FREQUENCY_RANGE_44100HZ;
		break;
	case 48000:
		val = SAMPLE_FREQUENCY_RANGE_48000HZ;
		break;
	case 96000:
		val = SAMPLE_FREQUENCY_RANGE_96000HZ;
		break;
	case 192000:
		val = SAMPLE_FREQUENCY_RANGE_192000HZ;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, TDA7803_REG3, val);

	return 0;
}

static int tda7803_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;

	snd_soc_component_write(component, TDA7803_REG3, fmt);

	return 0;
}

static const struct snd_soc_dai_ops tda7803_ops = {
	.startup = tda7803_startup,
	.hw_params = tda7803_hw_params,
	.set_fmt = tda7803_set_fmt,
};

static struct snd_soc_dai_driver tda7803_dai = {
	.name = "tda7803-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = TDA7803_SAMPLE_RATE,
		.formats = SNDRV_PCM_FMTBIT_S32_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tda7803_ops,
};

static const struct snd_soc_component_driver soc_codec_dev_tda7803 = {
	.name = "tda7803",
};

static const struct regmap_config tda7803_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = TDA7803_REGMAX,
	.cache_type = REGCACHE_RBTREE,
};

static int tda7803_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct tda7803_priv *tda7803;
	int val;

	tda7803 = devm_kzalloc(&i2c->dev, sizeof(*tda7803), GFP_KERNEL);
	if (!tda7803)
		return -ENOMEM;

	i2c_set_clientdata(i2c, tda7803);

	tda7803->regmap = devm_regmap_init_i2c(i2c, &tda7803_i2c_regmap);
	if (IS_ERR(tda7803->regmap))
		return PTR_ERR(tda7803->regmap);

	if (!device_property_read_u32(&i2c->dev, "st,tda7803-format", &val))
		tda7803->input_format = val;

	return devm_snd_soc_register_component(&i2c->dev,
					       &soc_codec_dev_tda7803,
					       &tda7803_dai, 1);
}

static const struct i2c_device_id tda7803_i2c_id[] = {
	{ "tda7803", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tda7803_i2c_id);

static const struct of_device_id tda7803_of_match[] = {
	{ .compatible = "st,tda7803" },
	{ },
};

static struct i2c_driver tda7803_i2c_driver = {
	.driver = {
		.name   = "tda7803",
		.of_match_table = of_match_ptr(tda7803_of_match),
	},
	.probe          = tda7803_i2c_probe,
	.id_table       = tda7803_i2c_id,
};
module_i2c_driver(tda7803_i2c_driver);

MODULE_AUTHOR("Jun Zeng <jun.zeng@rock-chips.com>");
MODULE_DESCRIPTION("TDA7803 audio processor driver");
MODULE_LICENSE("GPL");
