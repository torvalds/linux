// SPDX-License-Identifier: GPL-2.0-only
/*
 * ak4641.c  --  AK4641 ALSA Soc Audio driver
 *
 * Copyright (C) 2008 Harald Welte <laforge@gnufiish.org>
 * Copyright (C) 2011 Dmitry Artamonow <mad_soft@inbox.ru>
 *
 * Based on ak4535.c by Richard Purdie
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/ak4641.h>

/* AK4641 register space */
#define AK4641_PM1		0x00
#define AK4641_PM2		0x01
#define AK4641_SIG1		0x02
#define AK4641_SIG2		0x03
#define AK4641_MODE1		0x04
#define AK4641_MODE2		0x05
#define AK4641_DAC		0x06
#define AK4641_MIC		0x07
#define AK4641_TIMER		0x08
#define AK4641_ALC1		0x09
#define AK4641_ALC2		0x0a
#define AK4641_PGA		0x0b
#define AK4641_LATT		0x0c
#define AK4641_RATT		0x0d
#define AK4641_VOL		0x0e
#define AK4641_STATUS		0x0f
#define AK4641_EQLO		0x10
#define AK4641_EQMID		0x11
#define AK4641_EQHI		0x12
#define AK4641_BTIF		0x13

/* codec private data */
struct ak4641_priv {
	struct regmap *regmap;
	unsigned int sysclk;
	int deemph;
	int playback_fs;
};

/*
 * ak4641 register cache
 */
static const struct reg_default ak4641_reg_defaults[] = {
	{  0, 0x00 }, {  1, 0x80 }, {  2, 0x00 }, {  3, 0x80 },
	{  4, 0x02 }, {  5, 0x00 }, {  6, 0x11 }, {  7, 0x05 },
	{  8, 0x00 }, {  9, 0x00 }, { 10, 0x36 }, { 11, 0x10 },
	{ 12, 0x00 }, { 13, 0x00 }, { 14, 0x57 }, { 15, 0x00 },
	{ 16, 0x88 }, { 17, 0x88 }, { 18, 0x08 }, { 19, 0x08 }
};

static const int deemph_settings[] = {44100, 0, 48000, 32000};

static int ak4641_set_deemph(struct snd_soc_component *component)
{
	struct ak4641_priv *ak4641 = snd_soc_component_get_drvdata(component);
	int i, best = 0;

	for (i = 0 ; i < ARRAY_SIZE(deemph_settings); i++) {
		/* if deemphasis is on, select the nearest available rate */
		if (ak4641->deemph && deemph_settings[i] != 0 &&
		    abs(deemph_settings[i] - ak4641->playback_fs) <
		    abs(deemph_settings[best] - ak4641->playback_fs))
			best = i;

		if (!ak4641->deemph && deemph_settings[i] == 0)
			best = i;
	}

	dev_dbg(component->dev, "Set deemphasis %d\n", best);

	return snd_soc_component_update_bits(component, AK4641_DAC, 0x3, best);
}

static int ak4641_put_deemph(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4641_priv *ak4641 = snd_soc_component_get_drvdata(component);
	int deemph = ucontrol->value.integer.value[0];

	if (deemph > 1)
		return -EINVAL;

	ak4641->deemph = deemph;

	return ak4641_set_deemph(component);
}

static int ak4641_get_deemph(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct ak4641_priv *ak4641 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = ak4641->deemph;
	return 0;
};

static const char *ak4641_mono_out[] = {"(L + R)/2", "Hi-Z"};
static const char *ak4641_hp_out[] = {"Stereo", "Mono"};
static const char *ak4641_mic_select[] = {"Internal", "External"};
static const char *ak4641_mic_or_dac[] = {"Microphone", "Voice DAC"};


static const DECLARE_TLV_DB_SCALE(mono_gain_tlv, -1700, 2300, 0);
static const DECLARE_TLV_DB_SCALE(mic_boost_tlv, 0, 2000, 0);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1050, 150, 0);
static const DECLARE_TLV_DB_SCALE(master_tlv, -12750, 50, 0);
static const DECLARE_TLV_DB_SCALE(mic_stereo_sidetone_tlv, -2700, 300, 0);
static const DECLARE_TLV_DB_SCALE(mic_mono_sidetone_tlv, -400, 400, 0);
static const DECLARE_TLV_DB_SCALE(capture_tlv, -800, 50, 0);
static const DECLARE_TLV_DB_SCALE(alc_tlv, -800, 50, 0);
static const DECLARE_TLV_DB_SCALE(aux_in_tlv, -2100, 300, 0);


static SOC_ENUM_SINGLE_DECL(ak4641_mono_out_enum,
			    AK4641_SIG1, 6, ak4641_mono_out);
static SOC_ENUM_SINGLE_DECL(ak4641_hp_out_enum,
			    AK4641_MODE2, 2, ak4641_hp_out);
static SOC_ENUM_SINGLE_DECL(ak4641_mic_select_enum,
			    AK4641_MIC, 1, ak4641_mic_select);
static SOC_ENUM_SINGLE_DECL(ak4641_mic_or_dac_enum,
			    AK4641_BTIF, 4, ak4641_mic_or_dac);

static const struct snd_kcontrol_new ak4641_snd_controls[] = {
	SOC_ENUM("Mono 1 Output", ak4641_mono_out_enum),
	SOC_SINGLE_TLV("Mono 1 Gain Volume", AK4641_SIG1, 7, 1, 1,
							mono_gain_tlv),
	SOC_ENUM("Headphone Output", ak4641_hp_out_enum),
	SOC_SINGLE_BOOL_EXT("Playback Deemphasis Switch", 0,
					ak4641_get_deemph, ak4641_put_deemph),

	SOC_SINGLE_TLV("Mic Boost Volume", AK4641_MIC, 0, 1, 0, mic_boost_tlv),

	SOC_SINGLE("ALC Operation Time", AK4641_TIMER, 0, 3, 0),
	SOC_SINGLE("ALC Recovery Time", AK4641_TIMER, 2, 3, 0),
	SOC_SINGLE("ALC ZC Time", AK4641_TIMER, 4, 3, 0),

	SOC_SINGLE("ALC 1 Switch", AK4641_ALC1, 5, 1, 0),

	SOC_SINGLE_TLV("ALC Volume", AK4641_ALC2, 0, 71, 0, alc_tlv),
	SOC_SINGLE("Left Out Enable Switch", AK4641_SIG2, 1, 1, 0),
	SOC_SINGLE("Right Out Enable Switch", AK4641_SIG2, 0, 1, 0),

	SOC_SINGLE_TLV("Capture Volume", AK4641_PGA, 0, 71, 0, capture_tlv),

	SOC_DOUBLE_R_TLV("Master Playback Volume", AK4641_LATT,
				AK4641_RATT, 0, 255, 1, master_tlv),

	SOC_SINGLE_TLV("AUX In Volume", AK4641_VOL, 0, 15, 0, aux_in_tlv),

	SOC_SINGLE("Equalizer Switch", AK4641_DAC, 2, 1, 0),
	SOC_SINGLE_TLV("EQ1 100 Hz Volume", AK4641_EQLO, 0, 15, 1, eq_tlv),
	SOC_SINGLE_TLV("EQ2 250 Hz Volume", AK4641_EQLO, 4, 15, 1, eq_tlv),
	SOC_SINGLE_TLV("EQ3 1 kHz Volume", AK4641_EQMID, 0, 15, 1, eq_tlv),
	SOC_SINGLE_TLV("EQ4 3.5 kHz Volume", AK4641_EQMID, 4, 15, 1, eq_tlv),
	SOC_SINGLE_TLV("EQ5 10 kHz Volume", AK4641_EQHI, 0, 15, 1, eq_tlv),
};

/* Mono 1 Mixer */
static const struct snd_kcontrol_new ak4641_mono1_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("Mic Mono Sidetone Volume", AK4641_VOL, 7, 1, 0,
						mic_mono_sidetone_tlv),
	SOC_DAPM_SINGLE("Mic Mono Sidetone Switch", AK4641_SIG1, 4, 1, 0),
	SOC_DAPM_SINGLE("Mono Playback Switch", AK4641_SIG1, 5, 1, 0),
};

/* Stereo Mixer */
static const struct snd_kcontrol_new ak4641_stereo_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("Mic Sidetone Volume", AK4641_VOL, 4, 7, 0,
						mic_stereo_sidetone_tlv),
	SOC_DAPM_SINGLE("Mic Sidetone Switch", AK4641_SIG2, 4, 1, 0),
	SOC_DAPM_SINGLE("Playback Switch", AK4641_SIG2, 7, 1, 0),
	SOC_DAPM_SINGLE("Aux Bypass Switch", AK4641_SIG2, 5, 1, 0),
};

/* Input Mixer */
static const struct snd_kcontrol_new ak4641_input_mixer_controls[] = {
	SOC_DAPM_SINGLE("Mic Capture Switch", AK4641_MIC, 2, 1, 0),
	SOC_DAPM_SINGLE("Aux Capture Switch", AK4641_MIC, 5, 1, 0),
};

/* Mic mux */
static const struct snd_kcontrol_new ak4641_mic_mux_control =
	SOC_DAPM_ENUM("Mic Select", ak4641_mic_select_enum);

/* Input mux */
static const struct snd_kcontrol_new ak4641_input_mux_control =
	SOC_DAPM_ENUM("Input Select", ak4641_mic_or_dac_enum);

/* mono 2 switch */
static const struct snd_kcontrol_new ak4641_mono2_control =
	SOC_DAPM_SINGLE("Switch", AK4641_SIG1, 0, 1, 0);

/* ak4641 dapm widgets */
static const struct snd_soc_dapm_widget ak4641_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Stereo Mixer", SND_SOC_NOPM, 0, 0,
		&ak4641_stereo_mixer_controls[0],
		ARRAY_SIZE(ak4641_stereo_mixer_controls)),
	SND_SOC_DAPM_MIXER("Mono1 Mixer", SND_SOC_NOPM, 0, 0,
		&ak4641_mono1_mixer_controls[0],
		ARRAY_SIZE(ak4641_mono1_mixer_controls)),
	SND_SOC_DAPM_MIXER("Input Mixer", SND_SOC_NOPM, 0, 0,
		&ak4641_input_mixer_controls[0],
		ARRAY_SIZE(ak4641_input_mixer_controls)),
	SND_SOC_DAPM_MUX("Mic Mux", SND_SOC_NOPM, 0, 0,
		&ak4641_mic_mux_control),
	SND_SOC_DAPM_MUX("Input Mux", SND_SOC_NOPM, 0, 0,
		&ak4641_input_mux_control),
	SND_SOC_DAPM_SWITCH("Mono 2 Enable", SND_SOC_NOPM, 0, 0,
		&ak4641_mono2_control),

	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),
	SND_SOC_DAPM_OUTPUT("MOUT1"),
	SND_SOC_DAPM_OUTPUT("MOUT2"),
	SND_SOC_DAPM_OUTPUT("MICOUT"),

	SND_SOC_DAPM_ADC("ADC", "HiFi Capture", AK4641_PM1, 0, 0),
	SND_SOC_DAPM_PGA("Mic", AK4641_PM1, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("AUX In", AK4641_PM1, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mono Out", AK4641_PM1, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Line Out", AK4641_PM1, 4, 0, NULL, 0),

	SND_SOC_DAPM_DAC("DAC", "HiFi Playback", AK4641_PM2, 0, 0),
	SND_SOC_DAPM_PGA("Mono Out 2", AK4641_PM2, 3, 0, NULL, 0),

	SND_SOC_DAPM_ADC("Voice ADC", "Voice Capture", AK4641_BTIF, 0, 0),
	SND_SOC_DAPM_DAC("Voice DAC", "Voice Playback", AK4641_BTIF, 1, 0),

	SND_SOC_DAPM_MICBIAS("Mic Int Bias", AK4641_MIC, 3, 0),
	SND_SOC_DAPM_MICBIAS("Mic Ext Bias", AK4641_MIC, 4, 0),

	SND_SOC_DAPM_INPUT("MICIN"),
	SND_SOC_DAPM_INPUT("MICEXT"),
	SND_SOC_DAPM_INPUT("AUX"),
	SND_SOC_DAPM_INPUT("AIN"),
};

static const struct snd_soc_dapm_route ak4641_audio_map[] = {
	/* Stereo Mixer */
	{"Stereo Mixer", "Playback Switch", "DAC"},
	{"Stereo Mixer", "Mic Sidetone Switch", "Input Mux"},
	{"Stereo Mixer", "Aux Bypass Switch", "AUX In"},

	/* Mono 1 Mixer */
	{"Mono1 Mixer", "Mic Mono Sidetone Switch", "Input Mux"},
	{"Mono1 Mixer", "Mono Playback Switch", "DAC"},

	/* Mic */
	{"Mic", NULL, "AIN"},
	{"Mic Mux", "Internal", "Mic Int Bias"},
	{"Mic Mux", "External", "Mic Ext Bias"},
	{"Mic Int Bias", NULL, "MICIN"},
	{"Mic Ext Bias", NULL, "MICEXT"},
	{"MICOUT", NULL, "Mic Mux"},

	/* Input Mux */
	{"Input Mux", "Microphone", "Mic"},
	{"Input Mux", "Voice DAC", "Voice DAC"},

	/* Line Out */
	{"LOUT", NULL, "Line Out"},
	{"ROUT", NULL, "Line Out"},
	{"Line Out", NULL, "Stereo Mixer"},

	/* Mono 1 Out */
	{"MOUT1", NULL, "Mono Out"},
	{"Mono Out", NULL, "Mono1 Mixer"},

	/* Mono 2 Out */
	{"MOUT2", NULL, "Mono 2 Enable"},
	{"Mono 2 Enable", "Switch", "Mono Out 2"},
	{"Mono Out 2", NULL, "Stereo Mixer"},

	{"Voice ADC", NULL, "Mono 2 Enable"},

	/* Aux In */
	{"AUX In", NULL, "AUX"},

	/* ADC */
	{"ADC", NULL, "Input Mixer"},
	{"Input Mixer", "Mic Capture Switch", "Mic"},
	{"Input Mixer", "Aux Capture Switch", "AUX In"},
};

static int ak4641_set_dai_sysclk(struct snd_soc_dai *codec_dai,
	int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct ak4641_priv *ak4641 = snd_soc_component_get_drvdata(component);

	ak4641->sysclk = freq;
	return 0;
}

static int ak4641_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak4641_priv *ak4641 = snd_soc_component_get_drvdata(component);
	int rate = params_rate(params), fs = 256;
	u8 mode2;

	if (rate)
		fs = ak4641->sysclk / rate;
	else
		return -EINVAL;

	/* set fs */
	switch (fs) {
	case 1024:
		mode2 = (0x2 << 5);
		break;
	case 512:
		mode2 = (0x1 << 5);
		break;
	case 256:
		mode2 = (0x0 << 5);
		break;
	default:
		dev_err(component->dev, "Error: unsupported fs=%d\n", fs);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AK4641_MODE2, (0x3 << 5), mode2);

	/* Update de-emphasis filter for the new rate */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		ak4641->playback_fs = rate;
		ak4641_set_deemph(component);
	}

	return 0;
}

static int ak4641_pcm_set_dai_fmt(struct snd_soc_dai *codec_dai,
				  unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u8 btif;
	int ret;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		btif = (0x3 << 5);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		btif = (0x2 << 5);
		break;
	case SND_SOC_DAIFMT_DSP_A:	/* MSB after FRM */
		btif = (0x0 << 5);
		break;
	case SND_SOC_DAIFMT_DSP_B:	/* MSB during FRM */
		btif = (0x1 << 5);
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_component_update_bits(component, AK4641_BTIF, (0x3 << 5), btif);
	if (ret < 0)
		return ret;

	return 0;
}

static int ak4641_i2s_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u8 mode1 = 0;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		mode1 = 0x02;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		mode1 = 0x01;
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_component_write(component, AK4641_MODE1, mode1);
}

static int ak4641_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;

	return snd_soc_component_update_bits(component, AK4641_DAC, 0x20, mute ? 0x20 : 0);
}

static int ak4641_set_bias_level(struct snd_soc_component *component,
	enum snd_soc_bias_level level)
{
	struct ak4641_priv *ak4641 = snd_soc_component_get_drvdata(component);
	struct ak4641_platform_data *pdata = component->dev->platform_data;
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* unmute */
		snd_soc_component_update_bits(component, AK4641_DAC, 0x20, 0);
		break;
	case SND_SOC_BIAS_PREPARE:
		/* mute */
		snd_soc_component_update_bits(component, AK4641_DAC, 0x20, 0x20);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			if (pdata && gpio_is_valid(pdata->gpio_power))
				gpio_set_value(pdata->gpio_power, 1);
			mdelay(1);
			if (pdata && gpio_is_valid(pdata->gpio_npdn))
				gpio_set_value(pdata->gpio_npdn, 1);
			mdelay(1);

			ret = regcache_sync(ak4641->regmap);
			if (ret) {
				dev_err(component->dev,
					"Failed to sync cache: %d\n", ret);
				return ret;
			}
		}
		snd_soc_component_update_bits(component, AK4641_PM1, 0x80, 0x80);
		snd_soc_component_update_bits(component, AK4641_PM2, 0x80, 0);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, AK4641_PM1, 0x80, 0);
		if (pdata && gpio_is_valid(pdata->gpio_npdn))
			gpio_set_value(pdata->gpio_npdn, 0);
		if (pdata && gpio_is_valid(pdata->gpio_power))
			gpio_set_value(pdata->gpio_power, 0);
		regcache_mark_dirty(ak4641->regmap);
		break;
	}
	return 0;
}

#define AK4641_RATES	(SNDRV_PCM_RATE_8000_48000)
#define AK4641_RATES_BT (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
			 SNDRV_PCM_RATE_16000)
#define AK4641_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE)

static const struct snd_soc_dai_ops ak4641_i2s_dai_ops = {
	.hw_params    = ak4641_i2s_hw_params,
	.set_fmt      = ak4641_i2s_set_dai_fmt,
	.digital_mute = ak4641_mute,
	.set_sysclk   = ak4641_set_dai_sysclk,
};

static const struct snd_soc_dai_ops ak4641_pcm_dai_ops = {
	.hw_params    = NULL, /* rates are controlled by BT chip */
	.set_fmt      = ak4641_pcm_set_dai_fmt,
	.digital_mute = ak4641_mute,
	.set_sysclk   = ak4641_set_dai_sysclk,
};

static struct snd_soc_dai_driver ak4641_dai[] = {
{
	.name = "ak4641-hifi",
	.id = 1,
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AK4641_RATES,
		.formats = AK4641_FORMATS,
	},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = AK4641_RATES,
		.formats = AK4641_FORMATS,
	},
	.ops = &ak4641_i2s_dai_ops,
	.symmetric_rates = 1,
},
{
	.name = "ak4641-voice",
	.id = 1,
	.playback = {
		.stream_name = "Voice Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = AK4641_RATES_BT,
		.formats = AK4641_FORMATS,
	},
	.capture = {
		.stream_name = "Voice Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = AK4641_RATES_BT,
		.formats = AK4641_FORMATS,
	},
	.ops = &ak4641_pcm_dai_ops,
	.symmetric_rates = 1,
},
};

static const struct snd_soc_component_driver soc_component_dev_ak4641 = {
	.controls		= ak4641_snd_controls,
	.num_controls		= ARRAY_SIZE(ak4641_snd_controls),
	.dapm_widgets		= ak4641_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak4641_dapm_widgets),
	.dapm_routes		= ak4641_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(ak4641_audio_map),
	.set_bias_level		= ak4641_set_bias_level,
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config ak4641_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = AK4641_BTIF,
	.reg_defaults = ak4641_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ak4641_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static int ak4641_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct ak4641_platform_data *pdata = i2c->dev.platform_data;
	struct ak4641_priv *ak4641;
	int ret;

	ak4641 = devm_kzalloc(&i2c->dev, sizeof(struct ak4641_priv),
			      GFP_KERNEL);
	if (!ak4641)
		return -ENOMEM;

	ak4641->regmap = devm_regmap_init_i2c(i2c, &ak4641_regmap);
	if (IS_ERR(ak4641->regmap))
		return PTR_ERR(ak4641->regmap);

	if (pdata) {
		if (gpio_is_valid(pdata->gpio_power)) {
			ret = gpio_request_one(pdata->gpio_power,
					GPIOF_OUT_INIT_LOW, "ak4641 power");
			if (ret)
				goto err_out;
		}
		if (gpio_is_valid(pdata->gpio_npdn)) {
			ret = gpio_request_one(pdata->gpio_npdn,
					GPIOF_OUT_INIT_LOW, "ak4641 npdn");
			if (ret)
				goto err_gpio;

			udelay(1); /* > 150 ns */
			gpio_set_value(pdata->gpio_npdn, 1);
		}
	}

	i2c_set_clientdata(i2c, ak4641);

	ret = devm_snd_soc_register_component(&i2c->dev,
				&soc_component_dev_ak4641,
				ak4641_dai, ARRAY_SIZE(ak4641_dai));
	if (ret != 0)
		goto err_gpio2;

	return 0;

err_gpio2:
	if (pdata) {
		if (gpio_is_valid(pdata->gpio_power))
			gpio_set_value(pdata->gpio_power, 0);
		if (gpio_is_valid(pdata->gpio_npdn))
			gpio_free(pdata->gpio_npdn);
	}
err_gpio:
	if (pdata && gpio_is_valid(pdata->gpio_power))
		gpio_free(pdata->gpio_power);
err_out:
	return ret;
}

static int ak4641_i2c_remove(struct i2c_client *i2c)
{
	struct ak4641_platform_data *pdata = i2c->dev.platform_data;

	if (pdata) {
		if (gpio_is_valid(pdata->gpio_power)) {
			gpio_set_value(pdata->gpio_power, 0);
			gpio_free(pdata->gpio_power);
		}
		if (gpio_is_valid(pdata->gpio_npdn))
			gpio_free(pdata->gpio_npdn);
	}

	return 0;
}

static const struct i2c_device_id ak4641_i2c_id[] = {
	{ "ak4641", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ak4641_i2c_id);

static struct i2c_driver ak4641_i2c_driver = {
	.driver = {
		.name = "ak4641",
	},
	.probe =    ak4641_i2c_probe,
	.remove =   ak4641_i2c_remove,
	.id_table = ak4641_i2c_id,
};

module_i2c_driver(ak4641_i2c_driver);

MODULE_DESCRIPTION("SoC AK4641 driver");
MODULE_AUTHOR("Harald Welte <laforge@gnufiish.org>");
MODULE_LICENSE("GPL");
