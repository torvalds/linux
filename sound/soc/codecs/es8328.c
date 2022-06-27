// SPDX-License-Identifier: GPL-2.0-only
/*
 * es8328.c  --  ES8328 ALSA SoC Audio driver
 *
 * Copyright 2014 Sutajio Ko-Usagi PTE LTD
 *
 * Author: Sean Cross <xobs@kosagi.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "es8328.h"

static const unsigned int rates_12288[] = {
	8000, 12000, 16000, 24000, 32000, 48000, 96000,
};

static const int ratios_12288[] = {
	10, 7, 6, 4, 3, 2, 0,
};

static const struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count	= ARRAY_SIZE(rates_12288),
	.list	= rates_12288,
};

static const unsigned int rates_11289[] = {
	8018, 11025, 22050, 44100, 88200,
};

static const int ratios_11289[] = {
	9, 7, 4, 2, 0,
};

static const struct snd_pcm_hw_constraint_list constraints_11289 = {
	.count	= ARRAY_SIZE(rates_11289),
	.list	= rates_11289,
};

/* regulator supplies for sgtl5000, VDDD is an optional external supply */
enum sgtl5000_regulator_supplies {
	DVDD,
	AVDD,
	PVDD,
	HPVDD,
	ES8328_SUPPLY_NUM
};

/* vddd is optional supply */
static const char * const supply_names[ES8328_SUPPLY_NUM] = {
	"DVDD",
	"AVDD",
	"PVDD",
	"HPVDD",
};

#define ES8328_RATES (SNDRV_PCM_RATE_192000 | \
		SNDRV_PCM_RATE_96000 | \
		SNDRV_PCM_RATE_88200 | \
		SNDRV_PCM_RATE_8000_48000)
#define ES8328_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
		SNDRV_PCM_FMTBIT_S18_3LE | \
		SNDRV_PCM_FMTBIT_S20_3LE | \
		SNDRV_PCM_FMTBIT_S24_LE | \
		SNDRV_PCM_FMTBIT_S32_LE)

struct es8328_priv {
	struct regmap *regmap;
	struct clk *clk;
	int playback_fs;
	bool deemph;
	int mclkdiv2;
	const struct snd_pcm_hw_constraint_list *sysclk_constraints;
	const int *mclk_ratios;
	bool master;
	struct regulator_bulk_data supplies[ES8328_SUPPLY_NUM];
};

/*
 * ES8328 Controls
 */

static const char * const adcpol_txt[] = {"Normal", "L Invert", "R Invert",
					  "L + R Invert"};
static SOC_ENUM_SINGLE_DECL(adcpol,
			    ES8328_ADCCONTROL6, 6, adcpol_txt);

static const DECLARE_TLV_DB_SCALE(play_tlv, -3000, 100, 0);
static const DECLARE_TLV_DB_SCALE(dac_adc_tlv, -9600, 50, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(mic_tlv, 0, 300, 0);

static const struct {
	int rate;
	unsigned int val;
} deemph_settings[] = {
	{ 0,     ES8328_DACCONTROL6_DEEMPH_OFF },
	{ 32000, ES8328_DACCONTROL6_DEEMPH_32k },
	{ 44100, ES8328_DACCONTROL6_DEEMPH_44_1k },
	{ 48000, ES8328_DACCONTROL6_DEEMPH_48k },
};

static int es8328_set_deemph(struct snd_soc_component *component)
{
	struct es8328_priv *es8328 = snd_soc_component_get_drvdata(component);
	int val, i, best;

	/*
	 * If we're using deemphasis select the nearest available sample
	 * rate.
	 */
	if (es8328->deemph) {
		best = 0;
		for (i = 1; i < ARRAY_SIZE(deemph_settings); i++) {
			if (abs(deemph_settings[i].rate - es8328->playback_fs) <
			    abs(deemph_settings[best].rate - es8328->playback_fs))
				best = i;
		}

		val = deemph_settings[best].val;
	} else {
		val = ES8328_DACCONTROL6_DEEMPH_OFF;
	}

	dev_dbg(component->dev, "Set deemphasis %d\n", val);

	return snd_soc_component_update_bits(component, ES8328_DACCONTROL6,
			ES8328_DACCONTROL6_DEEMPH_MASK, val);
}

static int es8328_get_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct es8328_priv *es8328 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = es8328->deemph;
	return 0;
}

static int es8328_put_deemph(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct es8328_priv *es8328 = snd_soc_component_get_drvdata(component);
	unsigned int deemph = ucontrol->value.integer.value[0];
	int ret;

	if (deemph > 1)
		return -EINVAL;

	if (es8328->deemph == deemph)
		return 0;

	ret = es8328_set_deemph(component);
	if (ret < 0)
		return ret;

	es8328->deemph = deemph;

	return 1;
}



static const struct snd_kcontrol_new es8328_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Capture Digital Volume",
		ES8328_ADCCONTROL8, ES8328_ADCCONTROL9,
		 0, 0xc0, 1, dac_adc_tlv),
	SOC_SINGLE("Capture ZC Switch", ES8328_ADCCONTROL7, 6, 1, 0),

	SOC_SINGLE_BOOL_EXT("DAC Deemphasis Switch", 0,
		    es8328_get_deemph, es8328_put_deemph),

	SOC_ENUM("Capture Polarity", adcpol),

	SOC_SINGLE_TLV("Left Mixer Left Bypass Volume",
			ES8328_DACCONTROL17, 3, 7, 1, bypass_tlv),
	SOC_SINGLE_TLV("Left Mixer Right Bypass Volume",
			ES8328_DACCONTROL19, 3, 7, 1, bypass_tlv),
	SOC_SINGLE_TLV("Right Mixer Left Bypass Volume",
			ES8328_DACCONTROL18, 3, 7, 1, bypass_tlv),
	SOC_SINGLE_TLV("Right Mixer Right Bypass Volume",
			ES8328_DACCONTROL20, 3, 7, 1, bypass_tlv),

	SOC_DOUBLE_R_TLV("PCM Volume",
			ES8328_LDACVOL, ES8328_RDACVOL,
			0, ES8328_DACVOL_MAX, 1, dac_adc_tlv),

	SOC_DOUBLE_R_TLV("Output 1 Playback Volume",
			ES8328_LOUT1VOL, ES8328_ROUT1VOL,
			0, ES8328_OUT1VOL_MAX, 0, play_tlv),

	SOC_DOUBLE_R_TLV("Output 2 Playback Volume",
			ES8328_LOUT2VOL, ES8328_ROUT2VOL,
			0, ES8328_OUT2VOL_MAX, 0, play_tlv),

	SOC_DOUBLE_TLV("Mic PGA Volume", ES8328_ADCCONTROL1,
			4, 0, 8, 0, mic_tlv),
};

/*
 * DAPM Controls
 */

static const char * const es8328_line_texts[] = {
	"Line 1", "Line 2", "PGA", "Differential"};

static const struct soc_enum es8328_lline_enum =
	SOC_ENUM_SINGLE(ES8328_DACCONTROL16, 3,
			      ARRAY_SIZE(es8328_line_texts),
			      es8328_line_texts);
static const struct snd_kcontrol_new es8328_left_line_controls =
	SOC_DAPM_ENUM("Route", es8328_lline_enum);

static const struct soc_enum es8328_rline_enum =
	SOC_ENUM_SINGLE(ES8328_DACCONTROL16, 0,
			      ARRAY_SIZE(es8328_line_texts),
			      es8328_line_texts);
static const struct snd_kcontrol_new es8328_right_line_controls =
	SOC_DAPM_ENUM("Route", es8328_rline_enum);

/* Left Mixer */
static const struct snd_kcontrol_new es8328_left_mixer_controls[] = {
	SOC_DAPM_SINGLE("Playback Switch", ES8328_DACCONTROL17, 7, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", ES8328_DACCONTROL17, 6, 1, 0),
	SOC_DAPM_SINGLE("Right Playback Switch", ES8328_DACCONTROL18, 7, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", ES8328_DACCONTROL18, 6, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new es8328_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left Playback Switch", ES8328_DACCONTROL19, 7, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", ES8328_DACCONTROL19, 6, 1, 0),
	SOC_DAPM_SINGLE("Playback Switch", ES8328_DACCONTROL20, 7, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", ES8328_DACCONTROL20, 6, 1, 0),
};

static const char * const es8328_pga_sel[] = {
	"Line 1", "Line 2", "Line 3", "Differential"};

/* Left PGA Mux */
static const struct soc_enum es8328_lpga_enum =
	SOC_ENUM_SINGLE(ES8328_ADCCONTROL2, 6,
			      ARRAY_SIZE(es8328_pga_sel),
			      es8328_pga_sel);
static const struct snd_kcontrol_new es8328_left_pga_controls =
	SOC_DAPM_ENUM("Route", es8328_lpga_enum);

/* Right PGA Mux */
static const struct soc_enum es8328_rpga_enum =
	SOC_ENUM_SINGLE(ES8328_ADCCONTROL2, 4,
			      ARRAY_SIZE(es8328_pga_sel),
			      es8328_pga_sel);
static const struct snd_kcontrol_new es8328_right_pga_controls =
	SOC_DAPM_ENUM("Route", es8328_rpga_enum);

/* Differential Mux */
static const char * const es8328_diff_sel[] = {"Line 1", "Line 2"};
static SOC_ENUM_SINGLE_DECL(diffmux,
			    ES8328_ADCCONTROL3, 7, es8328_diff_sel);
static const struct snd_kcontrol_new es8328_diffmux_controls =
	SOC_DAPM_ENUM("Route", diffmux);

/* Mono ADC Mux */
static const char * const es8328_mono_mux[] = {"Stereo", "Mono (Left)",
	"Mono (Right)", "Digital Mono"};
static SOC_ENUM_SINGLE_DECL(monomux,
			    ES8328_ADCCONTROL3, 3, es8328_mono_mux);
static const struct snd_kcontrol_new es8328_monomux_controls =
	SOC_DAPM_ENUM("Route", monomux);

static const struct snd_soc_dapm_widget es8328_dapm_widgets[] = {
	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
		&es8328_diffmux_controls),
	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0,
		&es8328_monomux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0,
		&es8328_monomux_controls),

	SND_SOC_DAPM_MUX("Left PGA Mux", ES8328_ADCPOWER,
			ES8328_ADCPOWER_AINL_OFF, 1,
			&es8328_left_pga_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", ES8328_ADCPOWER,
			ES8328_ADCPOWER_AINR_OFF, 1,
			&es8328_right_pga_controls),

	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0,
		&es8328_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0,
		&es8328_right_line_controls),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", ES8328_ADCPOWER,
			ES8328_ADCPOWER_ADCR_OFF, 1),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", ES8328_ADCPOWER,
			ES8328_ADCPOWER_ADCL_OFF, 1),

	SND_SOC_DAPM_SUPPLY("Mic Bias", ES8328_ADCPOWER,
			ES8328_ADCPOWER_MIC_BIAS_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Bias Gen", ES8328_ADCPOWER,
			ES8328_ADCPOWER_ADC_BIAS_GEN_OFF, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC STM", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_DACSTM_RESET, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC STM", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_ADCSTM_RESET, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC DIG", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_DACDIG_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC DIG", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_ADCDIG_OFF, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC DLL", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_DACDLL_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC DLL", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_ADCDLL_OFF, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("ADC Vref", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_ADCVREF_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Vref", ES8328_CHIPPOWER,
			ES8328_CHIPPOWER_DACVREF_OFF, 1, NULL, 0),

	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", ES8328_DACPOWER,
			ES8328_DACPOWER_RDAC_OFF, 1),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", ES8328_DACPOWER,
			ES8328_DACPOWER_LDAC_OFF, 1),

	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
		&es8328_left_mixer_controls[0],
		ARRAY_SIZE(es8328_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
		&es8328_right_mixer_controls[0],
		ARRAY_SIZE(es8328_right_mixer_controls)),

	SND_SOC_DAPM_PGA("Right Out 2", ES8328_DACPOWER,
			ES8328_DACPOWER_ROUT2_ON, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", ES8328_DACPOWER,
			ES8328_DACPOWER_LOUT2_ON, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", ES8328_DACPOWER,
			ES8328_DACPOWER_ROUT1_ON, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", ES8328_DACPOWER,
			ES8328_DACPOWER_LOUT1_ON, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),

	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
};

static const struct snd_soc_dapm_route es8328_dapm_routes[] = {

	{ "Left Line Mux", "Line 1", "LINPUT1" },
	{ "Left Line Mux", "Line 2", "LINPUT2" },
	{ "Left Line Mux", "PGA", "Left PGA Mux" },
	{ "Left Line Mux", "Differential", "Differential Mux" },

	{ "Right Line Mux", "Line 1", "RINPUT1" },
	{ "Right Line Mux", "Line 2", "RINPUT2" },
	{ "Right Line Mux", "PGA", "Right PGA Mux" },
	{ "Right Line Mux", "Differential", "Differential Mux" },

	{ "Left PGA Mux", "Line 1", "LINPUT1" },
	{ "Left PGA Mux", "Line 2", "LINPUT2" },
	{ "Left PGA Mux", "Differential", "Differential Mux" },

	{ "Right PGA Mux", "Line 1", "RINPUT1" },
	{ "Right PGA Mux", "Line 2", "RINPUT2" },
	{ "Right PGA Mux", "Differential", "Differential Mux" },

	{ "Differential Mux", "Line 1", "LINPUT1" },
	{ "Differential Mux", "Line 1", "RINPUT1" },
	{ "Differential Mux", "Line 2", "LINPUT2" },
	{ "Differential Mux", "Line 2", "RINPUT2" },

	{ "Left ADC Mux", "Stereo", "Left PGA Mux" },
	{ "Left ADC Mux", "Mono (Left)", "Left PGA Mux" },
	{ "Left ADC Mux", "Digital Mono", "Left PGA Mux" },

	{ "Right ADC Mux", "Stereo", "Right PGA Mux" },
	{ "Right ADC Mux", "Mono (Right)", "Right PGA Mux" },
	{ "Right ADC Mux", "Digital Mono", "Right PGA Mux" },

	{ "Left ADC", NULL, "Left ADC Mux" },
	{ "Right ADC", NULL, "Right ADC Mux" },

	{ "ADC DIG", NULL, "ADC STM" },
	{ "ADC DIG", NULL, "ADC Vref" },
	{ "ADC DIG", NULL, "ADC DLL" },

	{ "Left ADC", NULL, "ADC DIG" },
	{ "Right ADC", NULL, "ADC DIG" },

	{ "Mic Bias", NULL, "Mic Bias Gen" },

	{ "Left Line Mux", "Line 1", "LINPUT1" },
	{ "Left Line Mux", "Line 2", "LINPUT2" },
	{ "Left Line Mux", "PGA", "Left PGA Mux" },
	{ "Left Line Mux", "Differential", "Differential Mux" },

	{ "Right Line Mux", "Line 1", "RINPUT1" },
	{ "Right Line Mux", "Line 2", "RINPUT2" },
	{ "Right Line Mux", "PGA", "Right PGA Mux" },
	{ "Right Line Mux", "Differential", "Differential Mux" },

	{ "Left Out 1", NULL, "Left DAC" },
	{ "Right Out 1", NULL, "Right DAC" },
	{ "Left Out 2", NULL, "Left DAC" },
	{ "Right Out 2", NULL, "Right DAC" },

	{ "Left Mixer", "Playback Switch", "Left DAC" },
	{ "Left Mixer", "Left Bypass Switch", "Left Line Mux" },
	{ "Left Mixer", "Right Playback Switch", "Right DAC" },
	{ "Left Mixer", "Right Bypass Switch", "Right Line Mux" },

	{ "Right Mixer", "Left Playback Switch", "Left DAC" },
	{ "Right Mixer", "Left Bypass Switch", "Left Line Mux" },
	{ "Right Mixer", "Playback Switch", "Right DAC" },
	{ "Right Mixer", "Right Bypass Switch", "Right Line Mux" },

	{ "DAC DIG", NULL, "DAC STM" },
	{ "DAC DIG", NULL, "DAC Vref" },
	{ "DAC DIG", NULL, "DAC DLL" },

	{ "Left DAC", NULL, "DAC DIG" },
	{ "Right DAC", NULL, "DAC DIG" },

	{ "Left Out 1", NULL, "Left Mixer" },
	{ "LOUT1", NULL, "Left Out 1" },
	{ "Right Out 1", NULL, "Right Mixer" },
	{ "ROUT1", NULL, "Right Out 1" },

	{ "Left Out 2", NULL, "Left Mixer" },
	{ "LOUT2", NULL, "Left Out 2" },
	{ "Right Out 2", NULL, "Right Mixer" },
	{ "ROUT2", NULL, "Right Out 2" },
};

static int es8328_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	return snd_soc_component_update_bits(dai->component, ES8328_DACCONTROL3,
			ES8328_DACCONTROL3_DACMUTE,
			mute ? ES8328_DACCONTROL3_DACMUTE : 0);
}

static int es8328_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8328_priv *es8328 = snd_soc_component_get_drvdata(component);

	if (es8328->master && es8328->sysclk_constraints)
		snd_pcm_hw_constraint_list(substream->runtime, 0,
				SNDRV_PCM_HW_PARAM_RATE,
				es8328->sysclk_constraints);

	return 0;
}

static int es8328_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8328_priv *es8328 = snd_soc_component_get_drvdata(component);
	int i;
	int reg;
	int wl;
	int ratio;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg = ES8328_DACCONTROL2;
	else
		reg = ES8328_ADCCONTROL5;

	if (es8328->master) {
		if (!es8328->sysclk_constraints) {
			dev_err(component->dev, "No MCLK configured\n");
			return -EINVAL;
		}

		for (i = 0; i < es8328->sysclk_constraints->count; i++)
			if (es8328->sysclk_constraints->list[i] ==
			    params_rate(params))
				break;

		if (i == es8328->sysclk_constraints->count) {
			dev_err(component->dev,
				"LRCLK %d unsupported with current clock\n",
				params_rate(params));
			return -EINVAL;
		}
		ratio = es8328->mclk_ratios[i];
	} else {
		ratio = 0;
		es8328->mclkdiv2 = 0;
	}

	snd_soc_component_update_bits(component, ES8328_MASTERMODE,
			ES8328_MASTERMODE_MCLKDIV2,
			es8328->mclkdiv2 ? ES8328_MASTERMODE_MCLKDIV2 : 0);

	switch (params_width(params)) {
	case 16:
		wl = 3;
		break;
	case 18:
		wl = 2;
		break;
	case 20:
		wl = 1;
		break;
	case 24:
		wl = 0;
		break;
	case 32:
		wl = 4;
		break;
	default:
		return -EINVAL;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		snd_soc_component_update_bits(component, ES8328_DACCONTROL1,
				ES8328_DACCONTROL1_DACWL_MASK,
				wl << ES8328_DACCONTROL1_DACWL_SHIFT);

		es8328->playback_fs = params_rate(params);
		es8328_set_deemph(component);
	} else
		snd_soc_component_update_bits(component, ES8328_ADCCONTROL4,
				ES8328_ADCCONTROL4_ADCWL_MASK,
				wl << ES8328_ADCCONTROL4_ADCWL_SHIFT);

	return snd_soc_component_update_bits(component, reg, ES8328_RATEMASK, ratio);
}

static int es8328_set_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct es8328_priv *es8328 = snd_soc_component_get_drvdata(component);
	int mclkdiv2 = 0;

	switch (freq) {
	case 0:
		es8328->sysclk_constraints = NULL;
		es8328->mclk_ratios = NULL;
		break;
	case 22579200:
		mclkdiv2 = 1;
		fallthrough;
	case 11289600:
		es8328->sysclk_constraints = &constraints_11289;
		es8328->mclk_ratios = ratios_11289;
		break;
	case 24576000:
		mclkdiv2 = 1;
		fallthrough;
	case 12288000:
		es8328->sysclk_constraints = &constraints_12288;
		es8328->mclk_ratios = ratios_12288;
		break;
	default:
		return -EINVAL;
	}

	es8328->mclkdiv2 = mclkdiv2;
	return 0;
}

static int es8328_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct es8328_priv *es8328 = snd_soc_component_get_drvdata(component);
	u8 dac_mode = 0;
	u8 adc_mode = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		/* Master serial port mode, with BCLK generated automatically */
		snd_soc_component_update_bits(component, ES8328_MASTERMODE,
				    ES8328_MASTERMODE_MSC,
				    ES8328_MASTERMODE_MSC);
		es8328->master = true;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		/* Slave serial port mode */
		snd_soc_component_update_bits(component, ES8328_MASTERMODE,
				    ES8328_MASTERMODE_MSC, 0);
		es8328->master = false;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		dac_mode |= ES8328_DACCONTROL1_DACFORMAT_I2S;
		adc_mode |= ES8328_ADCCONTROL4_ADCFORMAT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		dac_mode |= ES8328_DACCONTROL1_DACFORMAT_RJUST;
		adc_mode |= ES8328_ADCCONTROL4_ADCFORMAT_RJUST;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		dac_mode |= ES8328_DACCONTROL1_DACFORMAT_LJUST;
		adc_mode |= ES8328_ADCCONTROL4_ADCFORMAT_LJUST;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	if ((fmt & SND_SOC_DAIFMT_INV_MASK) != SND_SOC_DAIFMT_NB_NF)
		return -EINVAL;

	snd_soc_component_update_bits(component, ES8328_DACCONTROL1,
			ES8328_DACCONTROL1_DACFORMAT_MASK, dac_mode);
	snd_soc_component_update_bits(component, ES8328_ADCCONTROL4,
			ES8328_ADCCONTROL4_ADCFORMAT_MASK, adc_mode);

	return 0;
}

static int es8328_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VREF, VMID=2x50k, digital enabled */
		snd_soc_component_write(component, ES8328_CHIPPOWER, 0);
		snd_soc_component_update_bits(component, ES8328_CONTROL1,
				ES8328_CONTROL1_VMIDSEL_MASK |
				ES8328_CONTROL1_ENREF,
				ES8328_CONTROL1_VMIDSEL_50k |
				ES8328_CONTROL1_ENREF);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			snd_soc_component_update_bits(component, ES8328_CONTROL1,
					ES8328_CONTROL1_VMIDSEL_MASK |
					ES8328_CONTROL1_ENREF,
					ES8328_CONTROL1_VMIDSEL_5k |
					ES8328_CONTROL1_ENREF);

			/* Charge caps */
			msleep(100);
		}

		snd_soc_component_write(component, ES8328_CONTROL2,
				ES8328_CONTROL2_OVERCURRENT_ON |
				ES8328_CONTROL2_THERMAL_SHUTDOWN_ON);

		/* VREF, VMID=2*500k, digital stopped */
		snd_soc_component_update_bits(component, ES8328_CONTROL1,
				ES8328_CONTROL1_VMIDSEL_MASK |
				ES8328_CONTROL1_ENREF,
				ES8328_CONTROL1_VMIDSEL_500k |
				ES8328_CONTROL1_ENREF);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, ES8328_CONTROL1,
				ES8328_CONTROL1_VMIDSEL_MASK |
				ES8328_CONTROL1_ENREF,
				0);
		break;
	}
	return 0;
}

static const struct snd_soc_dai_ops es8328_dai_ops = {
	.startup	= es8328_startup,
	.hw_params	= es8328_hw_params,
	.mute_stream	= es8328_mute,
	.set_sysclk	= es8328_set_sysclk,
	.set_fmt	= es8328_set_dai_fmt,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver es8328_dai = {
	.name = "es8328-hifi-analog",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = ES8328_RATES,
		.formats = ES8328_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = ES8328_RATES,
		.formats = ES8328_FORMATS,
	},
	.ops = &es8328_dai_ops,
	.symmetric_rate = 1,
};

static int es8328_suspend(struct snd_soc_component *component)
{
	struct es8328_priv *es8328;
	int ret;

	es8328 = snd_soc_component_get_drvdata(component);

	clk_disable_unprepare(es8328->clk);

	ret = regulator_bulk_disable(ARRAY_SIZE(es8328->supplies),
			es8328->supplies);
	if (ret) {
		dev_err(component->dev, "unable to disable regulators\n");
		return ret;
	}
	return 0;
}

static int es8328_resume(struct snd_soc_component *component)
{
	struct regmap *regmap = dev_get_regmap(component->dev, NULL);
	struct es8328_priv *es8328;
	int ret;

	es8328 = snd_soc_component_get_drvdata(component);

	ret = clk_prepare_enable(es8328->clk);
	if (ret) {
		dev_err(component->dev, "unable to enable clock\n");
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(es8328->supplies),
					es8328->supplies);
	if (ret) {
		dev_err(component->dev, "unable to enable regulators\n");
		return ret;
	}

	regcache_mark_dirty(regmap);
	ret = regcache_sync(regmap);
	if (ret) {
		dev_err(component->dev, "unable to sync regcache\n");
		return ret;
	}

	return 0;
}

static int es8328_component_probe(struct snd_soc_component *component)
{
	struct es8328_priv *es8328;
	int ret;

	es8328 = snd_soc_component_get_drvdata(component);

	ret = regulator_bulk_enable(ARRAY_SIZE(es8328->supplies),
					es8328->supplies);
	if (ret) {
		dev_err(component->dev, "unable to enable regulators\n");
		return ret;
	}

	/* Setup clocks */
	es8328->clk = devm_clk_get(component->dev, NULL);
	if (IS_ERR(es8328->clk)) {
		dev_err(component->dev, "codec clock missing or invalid\n");
		ret = PTR_ERR(es8328->clk);
		goto clk_fail;
	}

	ret = clk_prepare_enable(es8328->clk);
	if (ret) {
		dev_err(component->dev, "unable to prepare codec clk\n");
		goto clk_fail;
	}

	return 0;

clk_fail:
	regulator_bulk_disable(ARRAY_SIZE(es8328->supplies),
			       es8328->supplies);
	return ret;
}

static void es8328_remove(struct snd_soc_component *component)
{
	struct es8328_priv *es8328;

	es8328 = snd_soc_component_get_drvdata(component);

	clk_disable_unprepare(es8328->clk);

	regulator_bulk_disable(ARRAY_SIZE(es8328->supplies),
			       es8328->supplies);
}

const struct regmap_config es8328_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= ES8328_REG_MAX,
	.cache_type	= REGCACHE_RBTREE,
	.use_single_read = true,
	.use_single_write = true,
};
EXPORT_SYMBOL_GPL(es8328_regmap_config);

static const struct snd_soc_component_driver es8328_component_driver = {
	.probe			= es8328_component_probe,
	.remove			= es8328_remove,
	.suspend		= es8328_suspend,
	.resume			= es8328_resume,
	.set_bias_level		= es8328_set_bias_level,
	.controls		= es8328_snd_controls,
	.num_controls		= ARRAY_SIZE(es8328_snd_controls),
	.dapm_widgets		= es8328_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es8328_dapm_widgets),
	.dapm_routes		= es8328_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(es8328_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

int es8328_probe(struct device *dev, struct regmap *regmap)
{
	struct es8328_priv *es8328;
	int ret;
	int i;

	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	es8328 = devm_kzalloc(dev, sizeof(*es8328), GFP_KERNEL);
	if (es8328 == NULL)
		return -ENOMEM;

	es8328->regmap = regmap;

	for (i = 0; i < ARRAY_SIZE(es8328->supplies); i++)
		es8328->supplies[i].supply = supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(es8328->supplies),
				es8328->supplies);
	if (ret) {
		dev_err(dev, "unable to get regulators\n");
		return ret;
	}

	dev_set_drvdata(dev, es8328);

	return devm_snd_soc_register_component(dev,
			&es8328_component_driver, &es8328_dai, 1);
}
EXPORT_SYMBOL_GPL(es8328_probe);

MODULE_DESCRIPTION("ASoC ES8328 driver");
MODULE_AUTHOR("Sean Cross <xobs@kosagi.com>");
MODULE_LICENSE("GPL");
