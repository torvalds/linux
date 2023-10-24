// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8523.c  --  WM8523 ALSA SoC Audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8523.h"

#define WM8523_NUM_SUPPLIES 2
static const char *wm8523_supply_names[WM8523_NUM_SUPPLIES] = {
	"AVDD",
	"LINEVDD",
};

#define WM8523_NUM_RATES 7

/* codec private data */
struct wm8523_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[WM8523_NUM_SUPPLIES];
	unsigned int sysclk;
	unsigned int rate_constraint_list[WM8523_NUM_RATES];
	struct snd_pcm_hw_constraint_list rate_constraint;
};

static const struct reg_default wm8523_reg_defaults[] = {
	{ 2, 0x0000 },     /* R2 - PSCTRL1 */
	{ 3, 0x1812 },     /* R3 - AIF_CTRL1 */
	{ 4, 0x0000 },     /* R4 - AIF_CTRL2 */
	{ 5, 0x0001 },     /* R5 - DAC_CTRL3 */
	{ 6, 0x0190 },     /* R6 - DAC_GAINL */
	{ 7, 0x0190 },     /* R7 - DAC_GAINR */
	{ 8, 0x0000 },     /* R8 - ZERO_DETECT */
};

static bool wm8523_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8523_DEVICE_ID:
	case WM8523_REVISION:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(dac_tlv, -10000, 25, 0);

static const char *wm8523_zd_count_text[] = {
	"1024",
	"2048",
};

static SOC_ENUM_SINGLE_DECL(wm8523_zc_count, WM8523_ZERO_DETECT, 0,
			    wm8523_zd_count_text);

static const struct snd_kcontrol_new wm8523_controls[] = {
SOC_DOUBLE_R_TLV("Playback Volume", WM8523_DAC_GAINL, WM8523_DAC_GAINR,
		 0, 448, 0, dac_tlv),
SOC_SINGLE("ZC Switch", WM8523_DAC_CTRL3, 4, 1, 0),
SOC_SINGLE("Playback Deemphasis Switch", WM8523_AIF_CTRL1, 8, 1, 0),
SOC_DOUBLE("Playback Switch", WM8523_DAC_CTRL3, 2, 3, 1, 1),
SOC_SINGLE("Volume Ramp Up Switch", WM8523_DAC_CTRL3, 1, 1, 0),
SOC_SINGLE("Volume Ramp Down Switch", WM8523_DAC_CTRL3, 0, 1, 0),
SOC_ENUM("Zero Detect Count", wm8523_zc_count),
};

static const struct snd_soc_dapm_widget wm8523_dapm_widgets[] = {
SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_OUTPUT("LINEVOUTL"),
SND_SOC_DAPM_OUTPUT("LINEVOUTR"),
};

static const struct snd_soc_dapm_route wm8523_dapm_routes[] = {
	{ "LINEVOUTL", NULL, "DAC" },
	{ "LINEVOUTR", NULL, "DAC" },
};

static const struct {
	int value;
	int ratio;
} lrclk_ratios[WM8523_NUM_RATES] = {
	{ 1, 128 },
	{ 2, 192 },
	{ 3, 256 },
	{ 4, 384 },
	{ 5, 512 },
	{ 6, 768 },
	{ 7, 1152 },
};

static const struct {
	int value;
	int ratio;
} bclk_ratios[] = {
	{ 2, 32 },
	{ 3, 64 },
	{ 4, 128 },
};

static int wm8523_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8523_priv *wm8523 = snd_soc_component_get_drvdata(component);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!wm8523->sysclk) {
		dev_err(component->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &wm8523->rate_constraint);

	return 0;
}

static int wm8523_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8523_priv *wm8523 = snd_soc_component_get_drvdata(component);
	int i;
	u16 aifctrl1 = snd_soc_component_read(component, WM8523_AIF_CTRL1);
	u16 aifctrl2 = snd_soc_component_read(component, WM8523_AIF_CTRL2);

	/* Find a supported LRCLK ratio */
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		if (wm8523->sysclk / params_rate(params) ==
		    lrclk_ratios[i].ratio)
			break;
	}

	/* Should never happen, should be handled by constraints */
	if (i == ARRAY_SIZE(lrclk_ratios)) {
		dev_err(component->dev, "MCLK/fs ratio %d unsupported\n",
			wm8523->sysclk / params_rate(params));
		return -EINVAL;
	}

	aifctrl2 &= ~WM8523_SR_MASK;
	aifctrl2 |= lrclk_ratios[i].value;

	if (aifctrl1 & WM8523_AIF_MSTR) {
		/* Find a fs->bclk ratio */
		for (i = 0; i < ARRAY_SIZE(bclk_ratios); i++)
			if (params_width(params) * 2 <= bclk_ratios[i].ratio)
				break;

		if (i == ARRAY_SIZE(bclk_ratios)) {
			dev_err(component->dev,
				"No matching BCLK/fs ratio for word length %d\n",
				params_width(params));
			return -EINVAL;
		}

		aifctrl2 &= ~WM8523_BCLKDIV_MASK;
		aifctrl2 |= bclk_ratios[i].value << WM8523_BCLKDIV_SHIFT;
	}

	aifctrl1 &= ~WM8523_WL_MASK;
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		aifctrl1 |= 0x8;
		break;
	case 24:
		aifctrl1 |= 0x10;
		break;
	case 32:
		aifctrl1 |= 0x18;
		break;
	}

	snd_soc_component_write(component, WM8523_AIF_CTRL1, aifctrl1);
	snd_soc_component_write(component, WM8523_AIF_CTRL2, aifctrl2);

	return 0;
}

static int wm8523_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct wm8523_priv *wm8523 = snd_soc_component_get_drvdata(component);
	unsigned int val;
	int i;

	wm8523->sysclk = freq;

	wm8523->rate_constraint.count = 0;
	for (i = 0; i < ARRAY_SIZE(lrclk_ratios); i++) {
		val = freq / lrclk_ratios[i].ratio;
		/* Check that it's a standard rate since core can't
		 * cope with others and having the odd rates confuses
		 * constraint matching.
		 */
		switch (val) {
		case 8000:
		case 11025:
		case 16000:
		case 22050:
		case 32000:
		case 44100:
		case 48000:
		case 64000:
		case 88200:
		case 96000:
		case 176400:
		case 192000:
			dev_dbg(component->dev, "Supported sample rate: %dHz\n",
				val);
			wm8523->rate_constraint_list[i] = val;
			wm8523->rate_constraint.count++;
			break;
		default:
			dev_dbg(component->dev, "Skipping sample rate: %dHz\n",
				val);
		}
	}

	/* Need at least one supported rate... */
	if (wm8523->rate_constraint.count == 0)
		return -EINVAL;

	return 0;
}


static int wm8523_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 aifctrl1 = snd_soc_component_read(component, WM8523_AIF_CTRL1);

	aifctrl1 &= ~(WM8523_BCLK_INV_MASK | WM8523_LRCLK_INV_MASK |
		      WM8523_FMT_MASK | WM8523_AIF_MSTR_MASK);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		aifctrl1 |= WM8523_AIF_MSTR;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		aifctrl1 |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aifctrl1 |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		aifctrl1 |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		aifctrl1 |= 0x0023;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		aifctrl1 |= WM8523_BCLK_INV | WM8523_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		aifctrl1 |= WM8523_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		aifctrl1 |= WM8523_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, WM8523_AIF_CTRL1, aifctrl1);

	return 0;
}

static int wm8523_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8523_priv *wm8523 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* Full power on */
		snd_soc_component_update_bits(component, WM8523_PSCTRL1,
				    WM8523_SYS_ENA_MASK, 3);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8523->supplies),
						    wm8523->supplies);
			if (ret != 0) {
				dev_err(component->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			/* Sync back default/cached values */
			regcache_sync(wm8523->regmap);

			/* Initial power up */
			snd_soc_component_update_bits(component, WM8523_PSCTRL1,
					    WM8523_SYS_ENA_MASK, 1);

			msleep(100);
		}

		/* Power up to mute */
		snd_soc_component_update_bits(component, WM8523_PSCTRL1,
				    WM8523_SYS_ENA_MASK, 2);

		break;

	case SND_SOC_BIAS_OFF:
		/* The chip runs through the power down sequence for us. */
		snd_soc_component_update_bits(component, WM8523_PSCTRL1,
				    WM8523_SYS_ENA_MASK, 0);
		msleep(100);

		regulator_bulk_disable(ARRAY_SIZE(wm8523->supplies),
				       wm8523->supplies);
		break;
	}
	return 0;
}

#define WM8523_RATES SNDRV_PCM_RATE_8000_192000

#define WM8523_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops wm8523_dai_ops = {
	.startup	= wm8523_startup,
	.hw_params	= wm8523_hw_params,
	.set_sysclk	= wm8523_set_dai_sysclk,
	.set_fmt	= wm8523_set_dai_fmt,
};

static struct snd_soc_dai_driver wm8523_dai = {
	.name = "wm8523-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,  /* Mono modes not yet supported */
		.channels_max = 2,
		.rates = WM8523_RATES,
		.formats = WM8523_FORMATS,
	},
	.ops = &wm8523_dai_ops,
};

static int wm8523_probe(struct snd_soc_component *component)
{
	struct wm8523_priv *wm8523 = snd_soc_component_get_drvdata(component);

	wm8523->rate_constraint.list = &wm8523->rate_constraint_list[0];
	wm8523->rate_constraint.count =
		ARRAY_SIZE(wm8523->rate_constraint_list);

	/* Change some default settings - latch VU and enable ZC */
	snd_soc_component_update_bits(component, WM8523_DAC_GAINR,
			    WM8523_DACR_VU, WM8523_DACR_VU);
	snd_soc_component_update_bits(component, WM8523_DAC_CTRL3, WM8523_ZC, WM8523_ZC);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_wm8523 = {
	.probe			= wm8523_probe,
	.set_bias_level		= wm8523_set_bias_level,
	.controls		= wm8523_controls,
	.num_controls		= ARRAY_SIZE(wm8523_controls),
	.dapm_widgets		= wm8523_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8523_dapm_widgets),
	.dapm_routes		= wm8523_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8523_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct of_device_id wm8523_of_match[] = {
	{ .compatible = "wlf,wm8523" },
	{ },
};
MODULE_DEVICE_TABLE(of, wm8523_of_match);

static const struct regmap_config wm8523_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = WM8523_ZERO_DETECT,

	.reg_defaults = wm8523_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8523_reg_defaults),
	.cache_type = REGCACHE_MAPLE,

	.volatile_reg = wm8523_volatile_register,
};

static int wm8523_i2c_probe(struct i2c_client *i2c)
{
	struct wm8523_priv *wm8523;
	unsigned int val;
	int ret, i;

	wm8523 = devm_kzalloc(&i2c->dev, sizeof(struct wm8523_priv),
			      GFP_KERNEL);
	if (wm8523 == NULL)
		return -ENOMEM;

	wm8523->regmap = devm_regmap_init_i2c(i2c, &wm8523_regmap);
	if (IS_ERR(wm8523->regmap)) {
		ret = PTR_ERR(wm8523->regmap);
		dev_err(&i2c->dev, "Failed to create regmap: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8523->supplies); i++)
		wm8523->supplies[i].supply = wm8523_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(wm8523->supplies),
				      wm8523->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8523->supplies),
				    wm8523->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = regmap_read(wm8523->regmap, WM8523_DEVICE_ID, &val);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID register\n");
		goto err_enable;
	}
	if (val != 0x8523) {
		dev_err(&i2c->dev, "Device is not a WM8523, ID is %x\n", ret);
		ret = -EINVAL;
		goto err_enable;
	}

	ret = regmap_read(wm8523->regmap, WM8523_REVISION, &val);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read revision register\n");
		goto err_enable;
	}
	dev_info(&i2c->dev, "revision %c\n",
		 (val & WM8523_CHIP_REV_MASK) + 'A');

	ret = regmap_write(wm8523->regmap, WM8523_DEVICE_ID, 0x8523);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to reset device: %d\n", ret);
		goto err_enable;
	}

	regulator_bulk_disable(ARRAY_SIZE(wm8523->supplies), wm8523->supplies);

	i2c_set_clientdata(i2c, wm8523);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8523, &wm8523_dai, 1);

	return ret;

err_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8523->supplies), wm8523->supplies);
	return ret;
}

static const struct i2c_device_id wm8523_i2c_id[] = {
	{ "wm8523", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8523_i2c_id);

static struct i2c_driver wm8523_i2c_driver = {
	.driver = {
		.name = "wm8523",
		.of_match_table = wm8523_of_match,
	},
	.probe = wm8523_i2c_probe,
	.id_table = wm8523_i2c_id,
};

module_i2c_driver(wm8523_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8523 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
