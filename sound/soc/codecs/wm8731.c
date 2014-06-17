/*
 * wm8731.c  --  WM8731 ALSA SoC Audio driver
 *
 * Copyright 2005 Openedhand Ltd.
 * Copyright 2006-12 Wolfson Microelectronics, plc
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on wm8753.c by Liam Girdwood
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8731.h"

#define WM8731_NUM_SUPPLIES 4
static const char *wm8731_supply_names[WM8731_NUM_SUPPLIES] = {
	"AVDD",
	"HPVDD",
	"DCVDD",
	"DBVDD",
};

/* codec private data */
struct wm8731_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[WM8731_NUM_SUPPLIES];
	const struct snd_pcm_hw_constraint_list *constraints;
	unsigned int sysclk;
	int sysclk_type;
	int playback_fs;
	bool deemph;
};


/*
 * wm8731 register cache
 */
static const struct reg_default wm8731_reg_defaults[] = {
	{ 0, 0x0097 },
	{ 1, 0x0097 },
	{ 2, 0x0079 },
	{ 3, 0x0079 },
	{ 4, 0x000a },
	{ 5, 0x0008 },
	{ 6, 0x009f },
	{ 7, 0x000a },
	{ 8, 0x0000 },
	{ 9, 0x0000 },
};

static bool wm8731_volatile(struct device *dev, unsigned int reg)
{
	return reg == WM8731_RESET;
}

static bool wm8731_writeable(struct device *dev, unsigned int reg)
{
	return reg <= WM8731_RESET;
}

#define wm8731_reset(c)	snd_soc_write(c, WM8731_RESET, 0)

static const char *wm8731_input_select[] = {"Line In", "Mic"};

static SOC_ENUM_SINGLE_DECL(wm8731_insel_enum,
			    WM8731_APANA, 2, wm8731_input_select);

static int wm8731_deemph[] = { 0, 32000, 44100, 48000 };

static int wm8731_set_deemph(struct snd_soc_codec *codec)
{
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);
	int val, i, best;

	/* If we're using deemphasis select the nearest available sample
	 * rate.
	 */
	if (wm8731->deemph) {
		best = 1;
		for (i = 2; i < ARRAY_SIZE(wm8731_deemph); i++) {
			if (abs(wm8731_deemph[i] - wm8731->playback_fs) <
			    abs(wm8731_deemph[best] - wm8731->playback_fs))
				best = i;
		}

		val = best << 1;
	} else {
		best = 0;
		val = 0;
	}

	dev_dbg(codec->dev, "Set deemphasis %d (%dHz)\n",
		best, wm8731_deemph[best]);

	return snd_soc_update_bits(codec, WM8731_APDIGI, 0x6, val);
}

static int wm8731_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);

	ucontrol->value.enumerated.item[0] = wm8731->deemph;

	return 0;
}

static int wm8731_put_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_soc_kcontrol_codec(kcontrol);
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);
	int deemph = ucontrol->value.enumerated.item[0];
	int ret = 0;

	if (deemph > 1)
		return -EINVAL;

	mutex_lock(&codec->mutex);
	if (wm8731->deemph != deemph) {
		wm8731->deemph = deemph;

		wm8731_set_deemph(codec);

		ret = 1;
	}
	mutex_unlock(&codec->mutex);

	return ret;
}

static const DECLARE_TLV_DB_SCALE(in_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(sidetone_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(mic_tlv, 0, 2000, 0);

static const struct snd_kcontrol_new wm8731_snd_controls[] = {

SOC_DOUBLE_R_TLV("Master Playback Volume", WM8731_LOUT1V, WM8731_ROUT1V,
		 0, 127, 0, out_tlv),
SOC_DOUBLE_R("Master Playback ZC Switch", WM8731_LOUT1V, WM8731_ROUT1V,
	7, 1, 0),

SOC_DOUBLE_R_TLV("Capture Volume", WM8731_LINVOL, WM8731_RINVOL, 0, 31, 0,
		 in_tlv),
SOC_DOUBLE_R("Line Capture Switch", WM8731_LINVOL, WM8731_RINVOL, 7, 1, 1),

SOC_SINGLE_TLV("Mic Boost Volume", WM8731_APANA, 0, 1, 0, mic_tlv),
SOC_SINGLE("Mic Capture Switch", WM8731_APANA, 1, 1, 1),

SOC_SINGLE_TLV("Sidetone Playback Volume", WM8731_APANA, 6, 3, 1,
	       sidetone_tlv),

SOC_SINGLE("ADC High Pass Filter Switch", WM8731_APDIGI, 0, 1, 1),
SOC_SINGLE("Store DC Offset Switch", WM8731_APDIGI, 4, 1, 0),

SOC_SINGLE_BOOL_EXT("Playback Deemphasis Switch", 0,
		    wm8731_get_deemph, wm8731_put_deemph),
};

/* Output Mixer */
static const struct snd_kcontrol_new wm8731_output_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Bypass Switch", WM8731_APANA, 3, 1, 0),
SOC_DAPM_SINGLE("Mic Sidetone Switch", WM8731_APANA, 5, 1, 0),
SOC_DAPM_SINGLE("HiFi Playback Switch", WM8731_APANA, 4, 1, 0),
};

/* Input mux */
static const struct snd_kcontrol_new wm8731_input_mux_controls =
SOC_DAPM_ENUM("Input Select", wm8731_insel_enum);

static const struct snd_soc_dapm_widget wm8731_dapm_widgets[] = {
SND_SOC_DAPM_SUPPLY("ACTIVE",WM8731_ACTIVE, 0, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("OSC", WM8731_PWR, 5, 1, NULL, 0),
SND_SOC_DAPM_MIXER("Output Mixer", WM8731_PWR, 4, 1,
	&wm8731_output_mixer_controls[0],
	ARRAY_SIZE(wm8731_output_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8731_PWR, 3, 1),
SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("LHPOUT"),
SND_SOC_DAPM_OUTPUT("ROUT"),
SND_SOC_DAPM_OUTPUT("RHPOUT"),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8731_PWR, 2, 1),
SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0, &wm8731_input_mux_controls),
SND_SOC_DAPM_PGA("Line Input", WM8731_PWR, 0, 1, NULL, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias", WM8731_PWR, 1, 1),
SND_SOC_DAPM_INPUT("MICIN"),
SND_SOC_DAPM_INPUT("RLINEIN"),
SND_SOC_DAPM_INPUT("LLINEIN"),
};

static int wm8731_check_osc(struct snd_soc_dapm_widget *source,
			    struct snd_soc_dapm_widget *sink)
{
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(source->codec);

	return wm8731->sysclk_type == WM8731_SYSCLK_XTAL;
}

static const struct snd_soc_dapm_route wm8731_intercon[] = {
	{"DAC", NULL, "OSC", wm8731_check_osc},
	{"ADC", NULL, "OSC", wm8731_check_osc},
	{"DAC", NULL, "ACTIVE"},
	{"ADC", NULL, "ACTIVE"},

	/* output mixer */
	{"Output Mixer", "Line Bypass Switch", "Line Input"},
	{"Output Mixer", "HiFi Playback Switch", "DAC"},
	{"Output Mixer", "Mic Sidetone Switch", "Mic Bias"},

	/* outputs */
	{"RHPOUT", NULL, "Output Mixer"},
	{"ROUT", NULL, "Output Mixer"},
	{"LHPOUT", NULL, "Output Mixer"},
	{"LOUT", NULL, "Output Mixer"},

	/* input mux */
	{"Input Mux", "Line In", "Line Input"},
	{"Input Mux", "Mic", "Mic Bias"},
	{"ADC", NULL, "Input Mux"},

	/* inputs */
	{"Line Input", NULL, "LLINEIN"},
	{"Line Input", NULL, "RLINEIN"},
	{"Mic Bias", NULL, "MICIN"},
};

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:4;
	u8 bosr:1;
	u8 usb:1;
};

/* codec mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 48k */
	{12288000, 48000, 256, 0x0, 0x0, 0x0},
	{18432000, 48000, 384, 0x0, 0x1, 0x0},
	{12000000, 48000, 250, 0x0, 0x0, 0x1},

	/* 32k */
	{12288000, 32000, 384, 0x6, 0x0, 0x0},
	{18432000, 32000, 576, 0x6, 0x1, 0x0},
	{12000000, 32000, 375, 0x6, 0x0, 0x1},

	/* 8k */
	{12288000, 8000, 1536, 0x3, 0x0, 0x0},
	{18432000, 8000, 2304, 0x3, 0x1, 0x0},
	{11289600, 8000, 1408, 0xb, 0x0, 0x0},
	{16934400, 8000, 2112, 0xb, 0x1, 0x0},
	{12000000, 8000, 1500, 0x3, 0x0, 0x1},

	/* 96k */
	{12288000, 96000, 128, 0x7, 0x0, 0x0},
	{18432000, 96000, 192, 0x7, 0x1, 0x0},
	{12000000, 96000, 125, 0x7, 0x0, 0x1},

	/* 44.1k */
	{11289600, 44100, 256, 0x8, 0x0, 0x0},
	{16934400, 44100, 384, 0x8, 0x1, 0x0},
	{12000000, 44100, 272, 0x8, 0x1, 0x1},

	/* 88.2k */
	{11289600, 88200, 128, 0xf, 0x0, 0x0},
	{16934400, 88200, 192, 0xf, 0x1, 0x0},
	{12000000, 88200, 136, 0xf, 0x1, 0x1},
};

/* rates constraints */
static const unsigned int wm8731_rates_12000000[] = {
	8000, 32000, 44100, 48000, 96000, 88200,
};

static const unsigned int wm8731_rates_12288000_18432000[] = {
	8000, 32000, 48000, 96000,
};

static const unsigned int wm8731_rates_11289600_16934400[] = {
	8000, 44100, 88200,
};

static const struct snd_pcm_hw_constraint_list wm8731_constraints_12000000 = {
	.list = wm8731_rates_12000000,
	.count = ARRAY_SIZE(wm8731_rates_12000000),
};

static const
struct snd_pcm_hw_constraint_list wm8731_constraints_12288000_18432000 = {
	.list = wm8731_rates_12288000_18432000,
	.count = ARRAY_SIZE(wm8731_rates_12288000_18432000),
};

static const
struct snd_pcm_hw_constraint_list wm8731_constraints_11289600_16934400 = {
	.list = wm8731_rates_11289600_16934400,
	.count = ARRAY_SIZE(wm8731_rates_11289600_16934400),
};

static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}
	return 0;
}

static int wm8731_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec, WM8731_IFACE) & 0xfff3;
	int i = get_coeff(wm8731->sysclk, params_rate(params));
	u16 srate = (coeff_div[i].sr << 2) |
		(coeff_div[i].bosr << 1) | coeff_div[i].usb;

	wm8731->playback_fs = params_rate(params);

	snd_soc_write(codec, WM8731_SRATE, srate);

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0008;
		break;
	}

	wm8731_set_deemph(codec);

	snd_soc_write(codec, WM8731_IFACE, iface);
	return 0;
}

static int wm8731_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, WM8731_APDIGI) & 0xfff7;

	if (mute)
		snd_soc_write(codec, WM8731_APDIGI, mute_reg | 0x8);
	else
		snd_soc_write(codec, WM8731_APDIGI, mute_reg);
	return 0;
}

static int wm8731_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case WM8731_SYSCLK_XTAL:
	case WM8731_SYSCLK_MCLK:
		wm8731->sysclk_type = clk_id;
		break;
	default:
		return -EINVAL;
	}

	switch (freq) {
	case 0:
		wm8731->constraints = NULL;
		break;
	case 12000000:
		wm8731->constraints = &wm8731_constraints_12000000;
		break;
	case 12288000:
	case 18432000:
		wm8731->constraints = &wm8731_constraints_12288000_18432000;
		break;
	case 16934400:
	case 11289600:
		wm8731->constraints = &wm8731_constraints_11289600_16934400;
		break;
	default:
		return -EINVAL;
	}

	wm8731->sysclk = freq;

	snd_soc_dapm_sync(&codec->dapm);

	return 0;
}


static int wm8731_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface |= 0x0040;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0001;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0013;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0003;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0090;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0080;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0010;
		break;
	default:
		return -EINVAL;
	}

	/* set iface */
	snd_soc_write(codec, WM8731_IFACE, iface);
	return 0;
}

static int wm8731_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);
	int ret;
	u16 reg;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8731->supplies),
						    wm8731->supplies);
			if (ret != 0)
				return ret;

			regcache_sync(wm8731->regmap);
		}

		/* Clear PWROFF, gate CLKOUT, everything else as-is */
		reg = snd_soc_read(codec, WM8731_PWR) & 0xff7f;
		snd_soc_write(codec, WM8731_PWR, reg | 0x0040);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, WM8731_PWR, 0xffff);
		regulator_bulk_disable(ARRAY_SIZE(wm8731->supplies),
				       wm8731->supplies);
		regcache_mark_dirty(wm8731->regmap);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

static int wm8731_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(dai->codec);

	if (wm8731->constraints)
		snd_pcm_hw_constraint_list(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_RATE,
					   wm8731->constraints);

	return 0;
}

#define WM8731_RATES SNDRV_PCM_RATE_8000_96000

#define WM8731_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8731_dai_ops = {
	.startup	= wm8731_startup,
	.hw_params	= wm8731_hw_params,
	.digital_mute	= wm8731_mute,
	.set_sysclk	= wm8731_set_dai_sysclk,
	.set_fmt	= wm8731_set_dai_fmt,
};

static struct snd_soc_dai_driver wm8731_dai = {
	.name = "wm8731-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8731_RATES,
		.formats = WM8731_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8731_RATES,
		.formats = WM8731_FORMATS,},
	.ops = &wm8731_dai_ops,
	.symmetric_rates = 1,
};

#ifdef CONFIG_PM
static int wm8731_suspend(struct snd_soc_codec *codec)
{
	wm8731_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm8731_resume(struct snd_soc_codec *codec)
{
	wm8731_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define wm8731_suspend NULL
#define wm8731_resume NULL
#endif

static int wm8731_probe(struct snd_soc_codec *codec)
{
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);
	int ret = 0, i;

	for (i = 0; i < ARRAY_SIZE(wm8731->supplies); i++)
		wm8731->supplies[i].supply = wm8731_supply_names[i];

	ret = devm_regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8731->supplies),
				 wm8731->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8731->supplies),
				    wm8731->supplies);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ret = wm8731_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset: %d\n", ret);
		goto err_regulator_enable;
	}

	wm8731_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* Latch the update bits */
	snd_soc_update_bits(codec, WM8731_LOUT1V, 0x100, 0);
	snd_soc_update_bits(codec, WM8731_ROUT1V, 0x100, 0);
	snd_soc_update_bits(codec, WM8731_LINVOL, 0x100, 0);
	snd_soc_update_bits(codec, WM8731_RINVOL, 0x100, 0);

	/* Disable bypass path by default */
	snd_soc_update_bits(codec, WM8731_APANA, 0x8, 0);

	/* Regulators will have been enabled by bias management */
	regulator_bulk_disable(ARRAY_SIZE(wm8731->supplies), wm8731->supplies);

	return 0;

err_regulator_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8731->supplies), wm8731->supplies);

	return ret;
}

/* power down chip */
static int wm8731_remove(struct snd_soc_codec *codec)
{
	struct wm8731_priv *wm8731 = snd_soc_codec_get_drvdata(codec);

	wm8731_set_bias_level(codec, SND_SOC_BIAS_OFF);

	regulator_bulk_disable(ARRAY_SIZE(wm8731->supplies), wm8731->supplies);

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8731 = {
	.probe =	wm8731_probe,
	.remove =	wm8731_remove,
	.suspend =	wm8731_suspend,
	.resume =	wm8731_resume,
	.set_bias_level = wm8731_set_bias_level,
	.dapm_widgets = wm8731_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8731_dapm_widgets),
	.dapm_routes = wm8731_intercon,
	.num_dapm_routes = ARRAY_SIZE(wm8731_intercon),
	.controls =	wm8731_snd_controls,
	.num_controls = ARRAY_SIZE(wm8731_snd_controls),
};

static const struct of_device_id wm8731_of_match[] = {
	{ .compatible = "wlf,wm8731", },
	{ }
};

MODULE_DEVICE_TABLE(of, wm8731_of_match);

static const struct regmap_config wm8731_regmap = {
	.reg_bits = 7,
	.val_bits = 9,

	.max_register = WM8731_RESET,
	.volatile_reg = wm8731_volatile,
	.writeable_reg = wm8731_writeable,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8731_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8731_reg_defaults),
};

#if defined(CONFIG_SPI_MASTER)
static int wm8731_spi_probe(struct spi_device *spi)
{
	struct wm8731_priv *wm8731;
	int ret;

	wm8731 = devm_kzalloc(&spi->dev, sizeof(struct wm8731_priv),
			      GFP_KERNEL);
	if (wm8731 == NULL)
		return -ENOMEM;

	wm8731->regmap = devm_regmap_init_spi(spi, &wm8731_regmap);
	if (IS_ERR(wm8731->regmap)) {
		ret = PTR_ERR(wm8731->regmap);
		dev_err(&spi->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	spi_set_drvdata(spi, wm8731);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_wm8731, &wm8731_dai, 1);
	if (ret != 0) {
		dev_err(&spi->dev, "Failed to register CODEC: %d\n", ret);
		return ret;
	}

	return 0;
}

static int wm8731_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static struct spi_driver wm8731_spi_driver = {
	.driver = {
		.name	= "wm8731",
		.owner	= THIS_MODULE,
		.of_match_table = wm8731_of_match,
	},
	.probe		= wm8731_spi_probe,
	.remove		= wm8731_spi_remove,
};
#endif /* CONFIG_SPI_MASTER */

#if IS_ENABLED(CONFIG_I2C)
static int wm8731_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8731_priv *wm8731;
	int ret;

	wm8731 = devm_kzalloc(&i2c->dev, sizeof(struct wm8731_priv),
			      GFP_KERNEL);
	if (wm8731 == NULL)
		return -ENOMEM;

	wm8731->regmap = devm_regmap_init_i2c(i2c, &wm8731_regmap);
	if (IS_ERR(wm8731->regmap)) {
		ret = PTR_ERR(wm8731->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	i2c_set_clientdata(i2c, wm8731);

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8731, &wm8731_dai, 1);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to register CODEC: %d\n", ret);
		return ret;
	}

	return 0;
}

static int wm8731_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id wm8731_i2c_id[] = {
	{ "wm8731", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8731_i2c_id);

static struct i2c_driver wm8731_i2c_driver = {
	.driver = {
		.name = "wm8731",
		.owner = THIS_MODULE,
		.of_match_table = wm8731_of_match,
	},
	.probe =    wm8731_i2c_probe,
	.remove =   wm8731_i2c_remove,
	.id_table = wm8731_i2c_id,
};
#endif

static int __init wm8731_modinit(void)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8731_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8731 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8731_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8731 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8731_modinit);

static void __exit wm8731_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&wm8731_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8731_spi_driver);
#endif
}
module_exit(wm8731_exit);

MODULE_DESCRIPTION("ASoC WM8731 driver");
MODULE_AUTHOR("Richard Purdie");
MODULE_LICENSE("GPL");
