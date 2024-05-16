// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs4349.c  --  CS4349 ALSA Soc Audio driver
 *
 * Copyright 2015 Cirrus Logic, Inc.
 *
 * Authors: Tim Howe <Tim.Howe@cirrus.com>
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include "cs4349.h"


static const struct reg_default cs4349_reg_defaults[] = {
	{ 2, 0x00 },	/* r02	- Mode Control */
	{ 3, 0x09 },	/* r03	- Volume, Mixing and Inversion Control */
	{ 4, 0x81 },	/* r04	- Mute Control */
	{ 5, 0x00 },	/* r05	- Channel A Volume Control */
	{ 6, 0x00 },	/* r06	- Channel B Volume Control */
	{ 7, 0xB1 },	/* r07	- Ramp and Filter Control */
	{ 8, 0x1C },	/* r08	- Misc. Control */
};

/* Private data for the CS4349 */
struct  cs4349_private {
	struct regmap			*regmap;
	struct gpio_desc		*reset_gpio;
	unsigned int			mode;
	int				rate;
};

static bool cs4349_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS4349_CHIPID ... CS4349_MISC:
		return true;
	default:
		return false;
	}
}

static bool cs4349_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS4349_MODE ...  CS4349_MISC:
		return true;
	default:
		return false;
	}
}

static int cs4349_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_component *component = codec_dai->component;
	struct cs4349_private *cs4349 = snd_soc_component_get_drvdata(component);
	unsigned int fmt;

	fmt = format & SND_SOC_DAIFMT_FORMAT_MASK;

	switch (fmt) {
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_LEFT_J:
	case SND_SOC_DAIFMT_RIGHT_J:
		cs4349->mode = format & SND_SOC_DAIFMT_FORMAT_MASK;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int cs4349_pcm_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct cs4349_private *cs4349 = snd_soc_component_get_drvdata(component);
	int fmt, ret;

	cs4349->rate = params_rate(params);

	switch (cs4349->mode) {
	case SND_SOC_DAIFMT_I2S:
		fmt = DIF_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		fmt = DIF_LEFT_JST;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		switch (params_width(params)) {
		case 16:
			fmt = DIF_RGHT_JST16;
			break;
		case 24:
			fmt = DIF_RGHT_JST24;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, CS4349_MODE, DIF_MASK,
				  MODE_FORMAT(fmt));
	if (ret < 0)
		return ret;

	return 0;
}

static int cs4349_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	int reg;

	reg = 0;
	if (mute)
		reg = MUTE_AB_MASK;

	return snd_soc_component_update_bits(component, CS4349_MUTE, MUTE_AB_MASK, reg);
}

static DECLARE_TLV_DB_SCALE(dig_tlv, -12750, 50, 0);

static const char * const chan_mix_texts[] = {
	"Mute", "MuteA", "MuteA SwapB", "MuteA MonoB", "SwapA MuteB",
	"BothR", "Swap", "SwapA MonoB", "MuteB", "Normal", "BothL",
	"MonoB", "MonoA MuteB", "MonoA", "MonoA SwapB", "Mono",
	/*Normal == Channel A = Left, Channel B = Right*/
};

static const char * const fm_texts[] = {
	"Auto", "Single", "Double", "Quad",
};

static const char * const deemph_texts[] = {
	"None", "44.1k", "48k", "32k",
};

static const char * const softr_zeroc_texts[] = {
	"Immediate", "Zero Cross", "Soft Ramp", "SR on ZC",
};

static int deemph_values[] = {
	0, 4, 8, 12,
};

static int softr_zeroc_values[] = {
	0, 64, 128, 192,
};

static const struct soc_enum chan_mix_enum =
	SOC_ENUM_SINGLE(CS4349_VMI, 0,
			ARRAY_SIZE(chan_mix_texts),
			chan_mix_texts);

static const struct soc_enum fm_mode_enum =
	SOC_ENUM_SINGLE(CS4349_MODE, 0,
			ARRAY_SIZE(fm_texts),
			fm_texts);

static SOC_VALUE_ENUM_SINGLE_DECL(deemph_enum, CS4349_MODE, 0, DEM_MASK,
				deemph_texts, deemph_values);

static SOC_VALUE_ENUM_SINGLE_DECL(softr_zeroc_enum, CS4349_RMPFLT, 0,
				SR_ZC_MASK, softr_zeroc_texts,
				softr_zeroc_values);

static const struct snd_kcontrol_new cs4349_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Master Playback Volume",
			 CS4349_VOLA, CS4349_VOLB, 0, 0xFF, 1, dig_tlv),
	SOC_ENUM("Functional Mode", fm_mode_enum),
	SOC_ENUM("De-Emphasis Control", deemph_enum),
	SOC_ENUM("Soft Ramp Zero Cross Control", softr_zeroc_enum),
	SOC_ENUM("Channel Mixer", chan_mix_enum),
	SOC_SINGLE("VolA = VolB Switch", CS4349_VMI, 7, 1, 0),
	SOC_SINGLE("InvertA Switch", CS4349_VMI, 6, 1, 0),
	SOC_SINGLE("InvertB Switch", CS4349_VMI, 5, 1, 0),
	SOC_SINGLE("Auto-Mute Switch", CS4349_MUTE, 7, 1, 0),
	SOC_SINGLE("MUTEC A = B Switch", CS4349_MUTE, 5, 1, 0),
	SOC_SINGLE("Soft Ramp Up Switch", CS4349_RMPFLT, 5, 1, 0),
	SOC_SINGLE("Soft Ramp Down Switch", CS4349_RMPFLT, 4, 1, 0),
	SOC_SINGLE("Slow Roll Off Filter Switch", CS4349_RMPFLT, 2, 1, 0),
	SOC_SINGLE("Freeze Switch", CS4349_MISC, 5, 1, 0),
	SOC_SINGLE("Popguard Switch", CS4349_MISC, 4, 1, 0),
};

static const struct snd_soc_dapm_widget cs4349_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("HiFi DAC", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_OUTPUT("OutputA"),
	SND_SOC_DAPM_OUTPUT("OutputB"),
};

static const struct snd_soc_dapm_route cs4349_routes[] = {
	{"DAC Playback", NULL, "OutputA"},
	{"DAC Playback", NULL, "OutputB"},

	{"OutputA", NULL, "HiFi DAC"},
	{"OutputB", NULL, "HiFi DAC"},
};

#define CS4349_PCM_FORMATS (SNDRV_PCM_FMTBIT_S8  | SNDRV_PCM_FMTBIT_S16_LE  | \
			SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_3LE | SNDRV_PCM_FMTBIT_S24_LE  | \
			SNDRV_PCM_FMTBIT_S32_LE)

#define CS4349_PCM_RATES SNDRV_PCM_RATE_8000_192000

static const struct snd_soc_dai_ops cs4349_dai_ops = {
	.hw_params	= cs4349_pcm_hw_params,
	.set_fmt	= cs4349_set_dai_fmt,
	.mute_stream	= cs4349_mute,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver cs4349_dai = {
	.name = "cs4349_hifi",
	.playback = {
		.stream_name	= "DAC Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= CS4349_PCM_RATES,
		.formats	= CS4349_PCM_FORMATS,
	},
	.ops = &cs4349_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver soc_component_dev_cs4349 = {
	.controls		= cs4349_snd_controls,
	.num_controls		= ARRAY_SIZE(cs4349_snd_controls),
	.dapm_widgets		= cs4349_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(cs4349_dapm_widgets),
	.dapm_routes		= cs4349_routes,
	.num_dapm_routes	= ARRAY_SIZE(cs4349_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config cs4349_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,

	.max_register		= CS4349_MISC,
	.reg_defaults		= cs4349_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(cs4349_reg_defaults),
	.readable_reg		= cs4349_readable_register,
	.writeable_reg		= cs4349_writeable_register,
	.cache_type		= REGCACHE_MAPLE,
};

static int cs4349_i2c_probe(struct i2c_client *client)
{
	struct cs4349_private *cs4349;
	int ret;

	cs4349 = devm_kzalloc(&client->dev, sizeof(*cs4349), GFP_KERNEL);
	if (!cs4349)
		return -ENOMEM;

	cs4349->regmap = devm_regmap_init_i2c(client, &cs4349_regmap);
	if (IS_ERR(cs4349->regmap)) {
		ret = PTR_ERR(cs4349->regmap);
		dev_err(&client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	/* Reset the Device */
	cs4349->reset_gpio = devm_gpiod_get_optional(&client->dev,
		"reset", GPIOD_OUT_LOW);
	if (IS_ERR(cs4349->reset_gpio))
		return PTR_ERR(cs4349->reset_gpio);

	gpiod_set_value_cansleep(cs4349->reset_gpio, 1);

	i2c_set_clientdata(client, cs4349);

	return devm_snd_soc_register_component(&client->dev,
		&soc_component_dev_cs4349,
		&cs4349_dai, 1);
}

static void cs4349_i2c_remove(struct i2c_client *client)
{
	struct cs4349_private *cs4349 = i2c_get_clientdata(client);

	/* Hold down reset */
	gpiod_set_value_cansleep(cs4349->reset_gpio, 0);
}

#ifdef CONFIG_PM
static int cs4349_runtime_suspend(struct device *dev)
{
	struct cs4349_private *cs4349 = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(cs4349->regmap, CS4349_MISC, PWR_DWN, PWR_DWN);
	if (ret < 0)
		return ret;

	regcache_cache_only(cs4349->regmap, true);

	/* Hold down reset */
	gpiod_set_value_cansleep(cs4349->reset_gpio, 0);

	return 0;
}

static int cs4349_runtime_resume(struct device *dev)
{
	struct cs4349_private *cs4349 = dev_get_drvdata(dev);
	int ret;

	ret = regmap_update_bits(cs4349->regmap, CS4349_MISC, PWR_DWN, 0);
	if (ret < 0)
		return ret;

	gpiod_set_value_cansleep(cs4349->reset_gpio, 1);

	regcache_cache_only(cs4349->regmap, false);
	regcache_sync(cs4349->regmap);

	return 0;
}
#endif

static const struct dev_pm_ops cs4349_runtime_pm = {
	SET_RUNTIME_PM_OPS(cs4349_runtime_suspend, cs4349_runtime_resume,
			   NULL)
};

static const struct of_device_id cs4349_of_match[] = {
	{ .compatible = "cirrus,cs4349", },
	{},
};

MODULE_DEVICE_TABLE(of, cs4349_of_match);

static const struct i2c_device_id cs4349_i2c_id[] = {
	{"cs4349", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, cs4349_i2c_id);

static struct i2c_driver cs4349_i2c_driver = {
	.driver = {
		.name		= "cs4349",
		.of_match_table	= cs4349_of_match,
		.pm = &cs4349_runtime_pm,
	},
	.id_table	= cs4349_i2c_id,
	.probe		= cs4349_i2c_probe,
	.remove		= cs4349_i2c_remove,
};

module_i2c_driver(cs4349_i2c_driver);

MODULE_AUTHOR("Tim Howe <tim.howe@cirrus.com>");
MODULE_DESCRIPTION("Cirrus Logic CS4349 ALSA SoC Codec Driver");
MODULE_LICENSE("GPL");
