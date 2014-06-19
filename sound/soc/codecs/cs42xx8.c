/*
 * Cirrus Logic CS42448/CS42888 Audio CODEC Digital Audio Interface (DAI) driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Author: Nicolin Chen <Guangyu.Chen@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "cs42xx8.h"

#define CS42XX8_NUM_SUPPLIES 4
static const char *const cs42xx8_supply_names[CS42XX8_NUM_SUPPLIES] = {
	"VA",
	"VD",
	"VLS",
	"VLC",
};

#define CS42XX8_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S20_3LE | \
			 SNDRV_PCM_FMTBIT_S24_LE | \
			 SNDRV_PCM_FMTBIT_S32_LE)

/* codec private data */
struct cs42xx8_priv {
	struct regulator_bulk_data supplies[CS42XX8_NUM_SUPPLIES];
	const struct cs42xx8_driver_data *drvdata;
	struct regmap *regmap;
	struct clk *clk;

	bool slave_mode;
	unsigned long sysclk;
};

/* -127.5dB to 0dB with step of 0.5dB */
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);
/* -64dB to 24dB with step of 0.5dB */
static const DECLARE_TLV_DB_SCALE(adc_tlv, -6400, 50, 0);

static const char *const cs42xx8_adc_single[] = { "Differential", "Single-Ended" };
static const char *const cs42xx8_szc[] = { "Immediate Change", "Zero Cross",
					"Soft Ramp", "Soft Ramp on Zero Cross" };

static const struct soc_enum adc1_single_enum =
	SOC_ENUM_SINGLE(CS42XX8_ADCCTL, 4, 2, cs42xx8_adc_single);
static const struct soc_enum adc2_single_enum =
	SOC_ENUM_SINGLE(CS42XX8_ADCCTL, 3, 2, cs42xx8_adc_single);
static const struct soc_enum adc3_single_enum =
	SOC_ENUM_SINGLE(CS42XX8_ADCCTL, 2, 2, cs42xx8_adc_single);
static const struct soc_enum dac_szc_enum =
	SOC_ENUM_SINGLE(CS42XX8_TXCTL, 5, 4, cs42xx8_szc);
static const struct soc_enum adc_szc_enum =
	SOC_ENUM_SINGLE(CS42XX8_TXCTL, 0, 4, cs42xx8_szc);

static const struct snd_kcontrol_new cs42xx8_snd_controls[] = {
	SOC_DOUBLE_R_TLV("DAC1 Playback Volume", CS42XX8_VOLAOUT1,
			 CS42XX8_VOLAOUT2, 0, 0xff, 1, dac_tlv),
	SOC_DOUBLE_R_TLV("DAC2 Playback Volume", CS42XX8_VOLAOUT3,
			 CS42XX8_VOLAOUT4, 0, 0xff, 1, dac_tlv),
	SOC_DOUBLE_R_TLV("DAC3 Playback Volume", CS42XX8_VOLAOUT5,
			 CS42XX8_VOLAOUT6, 0, 0xff, 1, dac_tlv),
	SOC_DOUBLE_R_TLV("DAC4 Playback Volume", CS42XX8_VOLAOUT7,
			 CS42XX8_VOLAOUT8, 0, 0xff, 1, dac_tlv),
	SOC_DOUBLE_R_S_TLV("ADC1 Capture Volume", CS42XX8_VOLAIN1,
			   CS42XX8_VOLAIN2, 0, -0x80, 0x30, 7, 0, adc_tlv),
	SOC_DOUBLE_R_S_TLV("ADC2 Capture Volume", CS42XX8_VOLAIN3,
			   CS42XX8_VOLAIN4, 0, -0x80, 0x30, 7, 0, adc_tlv),
	SOC_DOUBLE("DAC1 Invert Switch", CS42XX8_DACINV, 0, 1, 1, 0),
	SOC_DOUBLE("DAC2 Invert Switch", CS42XX8_DACINV, 2, 3, 1, 0),
	SOC_DOUBLE("DAC3 Invert Switch", CS42XX8_DACINV, 4, 5, 1, 0),
	SOC_DOUBLE("DAC4 Invert Switch", CS42XX8_DACINV, 6, 7, 1, 0),
	SOC_DOUBLE("ADC1 Invert Switch", CS42XX8_ADCINV, 0, 1, 1, 0),
	SOC_DOUBLE("ADC2 Invert Switch", CS42XX8_ADCINV, 2, 3, 1, 0),
	SOC_SINGLE("ADC High-Pass Filter Switch", CS42XX8_ADCCTL, 7, 1, 1),
	SOC_SINGLE("DAC De-emphasis Switch", CS42XX8_ADCCTL, 5, 1, 0),
	SOC_ENUM("ADC1 Single Ended Mode Switch", adc1_single_enum),
	SOC_ENUM("ADC2 Single Ended Mode Switch", adc2_single_enum),
	SOC_SINGLE("DAC Single Volume Control Switch", CS42XX8_TXCTL, 7, 1, 0),
	SOC_ENUM("DAC Soft Ramp & Zero Cross Control Switch", dac_szc_enum),
	SOC_SINGLE("DAC Auto Mute Switch", CS42XX8_TXCTL, 4, 1, 0),
	SOC_SINGLE("Mute ADC Serial Port Switch", CS42XX8_TXCTL, 3, 1, 0),
	SOC_SINGLE("ADC Single Volume Control Switch", CS42XX8_TXCTL, 2, 1, 0),
	SOC_ENUM("ADC Soft Ramp & Zero Cross Control Switch", adc_szc_enum),
};

static const struct snd_kcontrol_new cs42xx8_adc3_snd_controls[] = {
	SOC_DOUBLE_R_S_TLV("ADC3 Capture Volume", CS42XX8_VOLAIN5,
			   CS42XX8_VOLAIN6, 0, -0x80, 0x30, 7, 0, adc_tlv),
	SOC_DOUBLE("ADC3 Invert Switch", CS42XX8_ADCINV, 4, 5, 1, 0),
	SOC_ENUM("ADC3 Single Ended Mode Switch", adc3_single_enum),
};

static const struct snd_soc_dapm_widget cs42xx8_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC1", "Playback", CS42XX8_PWRCTL, 1, 1),
	SND_SOC_DAPM_DAC("DAC2", "Playback", CS42XX8_PWRCTL, 2, 1),
	SND_SOC_DAPM_DAC("DAC3", "Playback", CS42XX8_PWRCTL, 3, 1),
	SND_SOC_DAPM_DAC("DAC4", "Playback", CS42XX8_PWRCTL, 4, 1),

	SND_SOC_DAPM_OUTPUT("AOUT1L"),
	SND_SOC_DAPM_OUTPUT("AOUT1R"),
	SND_SOC_DAPM_OUTPUT("AOUT2L"),
	SND_SOC_DAPM_OUTPUT("AOUT2R"),
	SND_SOC_DAPM_OUTPUT("AOUT3L"),
	SND_SOC_DAPM_OUTPUT("AOUT3R"),
	SND_SOC_DAPM_OUTPUT("AOUT4L"),
	SND_SOC_DAPM_OUTPUT("AOUT4R"),

	SND_SOC_DAPM_ADC("ADC1", "Capture", CS42XX8_PWRCTL, 5, 1),
	SND_SOC_DAPM_ADC("ADC2", "Capture", CS42XX8_PWRCTL, 6, 1),

	SND_SOC_DAPM_INPUT("AIN1L"),
	SND_SOC_DAPM_INPUT("AIN1R"),
	SND_SOC_DAPM_INPUT("AIN2L"),
	SND_SOC_DAPM_INPUT("AIN2R"),

	SND_SOC_DAPM_SUPPLY("PWR", CS42XX8_PWRCTL, 0, 1, NULL, 0),
};

static const struct snd_soc_dapm_widget cs42xx8_adc3_dapm_widgets[] = {
	SND_SOC_DAPM_ADC("ADC3", "Capture", CS42XX8_PWRCTL, 7, 1),

	SND_SOC_DAPM_INPUT("AIN3L"),
	SND_SOC_DAPM_INPUT("AIN3R"),
};

static const struct snd_soc_dapm_route cs42xx8_dapm_routes[] = {
	/* Playback */
	{ "AOUT1L", NULL, "DAC1" },
	{ "AOUT1R", NULL, "DAC1" },
	{ "DAC1", NULL, "PWR" },

	{ "AOUT2L", NULL, "DAC2" },
	{ "AOUT2R", NULL, "DAC2" },
	{ "DAC2", NULL, "PWR" },

	{ "AOUT3L", NULL, "DAC3" },
	{ "AOUT3R", NULL, "DAC3" },
	{ "DAC3", NULL, "PWR" },

	{ "AOUT4L", NULL, "DAC4" },
	{ "AOUT4R", NULL, "DAC4" },
	{ "DAC4", NULL, "PWR" },

	/* Capture */
	{ "ADC1", NULL, "AIN1L" },
	{ "ADC1", NULL, "AIN1R" },
	{ "ADC1", NULL, "PWR" },

	{ "ADC2", NULL, "AIN2L" },
	{ "ADC2", NULL, "AIN2R" },
	{ "ADC2", NULL, "PWR" },
};

static const struct snd_soc_dapm_route cs42xx8_adc3_dapm_routes[] = {
	/* Capture */
	{ "ADC3", NULL, "AIN3L" },
	{ "ADC3", NULL, "AIN3R" },
	{ "ADC3", NULL, "PWR" },
};

struct cs42xx8_ratios {
	unsigned int ratio;
	unsigned char speed;
	unsigned char mclk;
};

static const struct cs42xx8_ratios cs42xx8_ratios[] = {
	{ 64, CS42XX8_FM_QUAD, CS42XX8_FUNCMOD_MFREQ_256(4) },
	{ 96, CS42XX8_FM_QUAD, CS42XX8_FUNCMOD_MFREQ_384(4) },
	{ 128, CS42XX8_FM_QUAD, CS42XX8_FUNCMOD_MFREQ_512(4) },
	{ 192, CS42XX8_FM_QUAD, CS42XX8_FUNCMOD_MFREQ_768(4) },
	{ 256, CS42XX8_FM_SINGLE, CS42XX8_FUNCMOD_MFREQ_256(1) },
	{ 384, CS42XX8_FM_SINGLE, CS42XX8_FUNCMOD_MFREQ_384(1) },
	{ 512, CS42XX8_FM_SINGLE, CS42XX8_FUNCMOD_MFREQ_512(1) },
	{ 768, CS42XX8_FM_SINGLE, CS42XX8_FUNCMOD_MFREQ_768(1) },
	{ 1024, CS42XX8_FM_SINGLE, CS42XX8_FUNCMOD_MFREQ_1024(1) }
};

static int cs42xx8_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42xx8_priv *cs42xx8 = snd_soc_codec_get_drvdata(codec);

	cs42xx8->sysclk = freq;

	return 0;
}

static int cs42xx8_set_dai_fmt(struct snd_soc_dai *codec_dai,
			       unsigned int format)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42xx8_priv *cs42xx8 = snd_soc_codec_get_drvdata(codec);
	u32 val;

	/* Set DAI format */
	switch (format & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		val = CS42XX8_INTF_DAC_DIF_LEFTJ | CS42XX8_INTF_ADC_DIF_LEFTJ;
		break;
	case SND_SOC_DAIFMT_I2S:
		val = CS42XX8_INTF_DAC_DIF_I2S | CS42XX8_INTF_ADC_DIF_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val = CS42XX8_INTF_DAC_DIF_RIGHTJ | CS42XX8_INTF_ADC_DIF_RIGHTJ;
		break;
	default:
		dev_err(codec->dev, "unsupported dai format\n");
		return -EINVAL;
	}

	regmap_update_bits(cs42xx8->regmap, CS42XX8_INTF,
			   CS42XX8_INTF_DAC_DIF_MASK |
			   CS42XX8_INTF_ADC_DIF_MASK, val);

	/* Set master/slave audio interface */
	switch (format & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		cs42xx8->slave_mode = true;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		cs42xx8->slave_mode = false;
		break;
	default:
		dev_err(codec->dev, "unsupported master/slave mode\n");
		return -EINVAL;
	}

	return 0;
}

static int cs42xx8_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs42xx8_priv *cs42xx8 = snd_soc_codec_get_drvdata(codec);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	u32 ratio = cs42xx8->sysclk / params_rate(params);
	u32 i, fm, val, mask;

	for (i = 0; i < ARRAY_SIZE(cs42xx8_ratios); i++) {
		if (cs42xx8_ratios[i].ratio == ratio)
			break;
	}

	if (i == ARRAY_SIZE(cs42xx8_ratios)) {
		dev_err(codec->dev, "unsupported sysclk ratio\n");
		return -EINVAL;
	}

	mask = CS42XX8_FUNCMOD_MFREQ_MASK;
	val = cs42xx8_ratios[i].mclk;

	fm = cs42xx8->slave_mode ? CS42XX8_FM_AUTO : cs42xx8_ratios[i].speed;

	regmap_update_bits(cs42xx8->regmap, CS42XX8_FUNCMOD,
			   CS42XX8_FUNCMOD_xC_FM_MASK(tx) | mask,
			   CS42XX8_FUNCMOD_xC_FM(tx, fm) | val);

	return 0;
}

static int cs42xx8_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs42xx8_priv *cs42xx8 = snd_soc_codec_get_drvdata(codec);

	regmap_update_bits(cs42xx8->regmap, CS42XX8_DACMUTE,
			   CS42XX8_DACMUTE_ALL, mute ? CS42XX8_DACMUTE_ALL : 0);

	return 0;
}

static const struct snd_soc_dai_ops cs42xx8_dai_ops = {
	.set_fmt	= cs42xx8_set_dai_fmt,
	.set_sysclk	= cs42xx8_set_dai_sysclk,
	.hw_params	= cs42xx8_hw_params,
	.digital_mute	= cs42xx8_digital_mute,
};

static struct snd_soc_dai_driver cs42xx8_dai = {
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = CS42XX8_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = CS42XX8_FORMATS,
	},
	.ops = &cs42xx8_dai_ops,
};

static const struct reg_default cs42xx8_reg[] = {
	{ 0x01, 0x01 },   /* Chip I.D. and Revision Register */
	{ 0x02, 0x00 },   /* Power Control */
	{ 0x03, 0xF0 },   /* Functional Mode */
	{ 0x04, 0x46 },   /* Interface Formats */
	{ 0x05, 0x00 },   /* ADC Control & DAC De-Emphasis */
	{ 0x06, 0x10 },   /* Transition Control */
	{ 0x07, 0x00 },   /* DAC Channel Mute */
	{ 0x08, 0x00 },   /* Volume Control AOUT1 */
	{ 0x09, 0x00 },   /* Volume Control AOUT2 */
	{ 0x0a, 0x00 },   /* Volume Control AOUT3 */
	{ 0x0b, 0x00 },   /* Volume Control AOUT4 */
	{ 0x0c, 0x00 },   /* Volume Control AOUT5 */
	{ 0x0d, 0x00 },   /* Volume Control AOUT6 */
	{ 0x0e, 0x00 },   /* Volume Control AOUT7 */
	{ 0x0f, 0x00 },   /* Volume Control AOUT8 */
	{ 0x10, 0x00 },   /* DAC Channel Invert */
	{ 0x11, 0x00 },   /* Volume Control AIN1 */
	{ 0x12, 0x00 },   /* Volume Control AIN2 */
	{ 0x13, 0x00 },   /* Volume Control AIN3 */
	{ 0x14, 0x00 },   /* Volume Control AIN4 */
	{ 0x15, 0x00 },   /* Volume Control AIN5 */
	{ 0x16, 0x00 },   /* Volume Control AIN6 */
	{ 0x17, 0x00 },   /* ADC Channel Invert */
	{ 0x18, 0x00 },   /* Status Control */
	{ 0x1a, 0x00 },   /* Status Mask */
	{ 0x1b, 0x00 },   /* MUTEC Pin Control */
};

static bool cs42xx8_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42XX8_STATUS:
		return true;
	default:
		return false;
	}
}

static bool cs42xx8_writeable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42XX8_CHIPID:
	case CS42XX8_STATUS:
		return false;
	default:
		return true;
	}
}

const struct regmap_config cs42xx8_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS42XX8_LASTREG,
	.reg_defaults = cs42xx8_reg,
	.num_reg_defaults = ARRAY_SIZE(cs42xx8_reg),
	.volatile_reg = cs42xx8_volatile_register,
	.writeable_reg = cs42xx8_writeable_register,
	.cache_type = REGCACHE_RBTREE,
};
EXPORT_SYMBOL_GPL(cs42xx8_regmap_config);

static int cs42xx8_codec_probe(struct snd_soc_codec *codec)
{
	struct cs42xx8_priv *cs42xx8 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	switch (cs42xx8->drvdata->num_adcs) {
	case 3:
		snd_soc_add_codec_controls(codec, cs42xx8_adc3_snd_controls,
					ARRAY_SIZE(cs42xx8_adc3_snd_controls));
		snd_soc_dapm_new_controls(dapm, cs42xx8_adc3_dapm_widgets,
					ARRAY_SIZE(cs42xx8_adc3_dapm_widgets));
		snd_soc_dapm_add_routes(dapm, cs42xx8_adc3_dapm_routes,
					ARRAY_SIZE(cs42xx8_adc3_dapm_routes));
		break;
	default:
		break;
	}

	/* Mute all DAC channels */
	regmap_write(cs42xx8->regmap, CS42XX8_DACMUTE, CS42XX8_DACMUTE_ALL);

	return 0;
}

static const struct snd_soc_codec_driver cs42xx8_driver = {
	.probe = cs42xx8_codec_probe,
	.idle_bias_off = true,

	.controls = cs42xx8_snd_controls,
	.num_controls = ARRAY_SIZE(cs42xx8_snd_controls),
	.dapm_widgets = cs42xx8_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cs42xx8_dapm_widgets),
	.dapm_routes = cs42xx8_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(cs42xx8_dapm_routes),
};

const struct cs42xx8_driver_data cs42448_data = {
	.name = "cs42448",
	.num_adcs = 3,
};
EXPORT_SYMBOL_GPL(cs42448_data);

const struct cs42xx8_driver_data cs42888_data = {
	.name = "cs42888",
	.num_adcs = 2,
};
EXPORT_SYMBOL_GPL(cs42888_data);

static const struct of_device_id cs42xx8_of_match[] = {
	{ .compatible = "cirrus,cs42448", .data = &cs42448_data, },
	{ .compatible = "cirrus,cs42888", .data = &cs42888_data, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cs42xx8_of_match);
EXPORT_SYMBOL_GPL(cs42xx8_of_match);

int cs42xx8_probe(struct device *dev, struct regmap *regmap)
{
	const struct of_device_id *of_id = of_match_device(cs42xx8_of_match, dev);
	struct cs42xx8_priv *cs42xx8;
	int ret, val, i;

	cs42xx8 = devm_kzalloc(dev, sizeof(*cs42xx8), GFP_KERNEL);
	if (cs42xx8 == NULL)
		return -ENOMEM;

	dev_set_drvdata(dev, cs42xx8);

	if (of_id)
		cs42xx8->drvdata = of_id->data;

	if (!cs42xx8->drvdata) {
		dev_err(dev, "failed to find driver data\n");
		return -EINVAL;
	}

	cs42xx8->clk = devm_clk_get(dev, "mclk");
	if (IS_ERR(cs42xx8->clk)) {
		dev_err(dev, "failed to get the clock: %ld\n",
				PTR_ERR(cs42xx8->clk));
		return -EINVAL;
	}

	cs42xx8->sysclk = clk_get_rate(cs42xx8->clk);

	for (i = 0; i < ARRAY_SIZE(cs42xx8->supplies); i++)
		cs42xx8->supplies[i].supply = cs42xx8_supply_names[i];

	ret = devm_regulator_bulk_get(dev,
			ARRAY_SIZE(cs42xx8->supplies), cs42xx8->supplies);
	if (ret) {
		dev_err(dev, "failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(cs42xx8->supplies),
				    cs42xx8->supplies);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		return ret;
	}

	/* Make sure hardware reset done */
	msleep(5);

	cs42xx8->regmap = regmap;
	if (IS_ERR(cs42xx8->regmap)) {
		ret = PTR_ERR(cs42xx8->regmap);
		dev_err(dev, "failed to allocate regmap: %d\n", ret);
		goto err_enable;
	}

	/*
	 * We haven't marked the chip revision as volatile due to
	 * sharing a register with the right input volume; explicitly
	 * bypass the cache to read it.
	 */
	regcache_cache_bypass(cs42xx8->regmap, true);

	/* Validate the chip ID */
	ret = regmap_read(cs42xx8->regmap, CS42XX8_CHIPID, &val);
	if (ret < 0) {
		dev_err(dev, "failed to get device ID, ret = %d", ret);
		goto err_enable;
	}

	/* The top four bits of the chip ID should be 0000 */
	if (((val & CS42XX8_CHIPID_CHIP_ID_MASK) >> 4) != 0x00) {
		dev_err(dev, "unmatched chip ID: %d\n",
			(val & CS42XX8_CHIPID_CHIP_ID_MASK) >> 4);
		ret = -EINVAL;
		goto err_enable;
	}

	dev_info(dev, "found device, revision %X\n",
			val & CS42XX8_CHIPID_REV_ID_MASK);

	regcache_cache_bypass(cs42xx8->regmap, false);

	cs42xx8_dai.name = cs42xx8->drvdata->name;

	/* Each adc supports stereo input */
	cs42xx8_dai.capture.channels_max = cs42xx8->drvdata->num_adcs * 2;

	ret = snd_soc_register_codec(dev, &cs42xx8_driver, &cs42xx8_dai, 1);
	if (ret) {
		dev_err(dev, "failed to register codec:%d\n", ret);
		goto err_enable;
	}

	regcache_cache_only(cs42xx8->regmap, true);

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(cs42xx8->supplies),
			       cs42xx8->supplies);

	return ret;
}
EXPORT_SYMBOL_GPL(cs42xx8_probe);

#ifdef CONFIG_PM_RUNTIME
static int cs42xx8_runtime_resume(struct device *dev)
{
	struct cs42xx8_priv *cs42xx8 = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(cs42xx8->clk);
	if (ret) {
		dev_err(dev, "failed to enable mclk: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(cs42xx8->supplies),
				    cs42xx8->supplies);
	if (ret) {
		dev_err(dev, "failed to enable supplies: %d\n", ret);
		goto err_clk;
	}

	/* Make sure hardware reset done */
	msleep(5);

	regcache_cache_only(cs42xx8->regmap, false);

	ret = regcache_sync(cs42xx8->regmap);
	if (ret) {
		dev_err(dev, "failed to sync regmap: %d\n", ret);
		goto err_bulk;
	}

	return 0;

err_bulk:
	regulator_bulk_disable(ARRAY_SIZE(cs42xx8->supplies),
			       cs42xx8->supplies);
err_clk:
	clk_disable_unprepare(cs42xx8->clk);

	return ret;
}

static int cs42xx8_runtime_suspend(struct device *dev)
{
	struct cs42xx8_priv *cs42xx8 = dev_get_drvdata(dev);

	regcache_cache_only(cs42xx8->regmap, true);

	regulator_bulk_disable(ARRAY_SIZE(cs42xx8->supplies),
			       cs42xx8->supplies);

	clk_disable_unprepare(cs42xx8->clk);

	return 0;
}
#endif

const struct dev_pm_ops cs42xx8_pm = {
	SET_RUNTIME_PM_OPS(cs42xx8_runtime_suspend, cs42xx8_runtime_resume, NULL)
};
EXPORT_SYMBOL_GPL(cs42xx8_pm);

MODULE_DESCRIPTION("Cirrus Logic CS42448/CS42888 ALSA SoC Codec Driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
