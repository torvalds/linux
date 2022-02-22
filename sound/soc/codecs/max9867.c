// SPDX-License-Identifier: GPL-2.0
//
// MAX9867 ALSA SoC codec driver
//
// Copyright 2013-2015 Maxim Integrated Products
// Copyright 2018 Ladislav Michl <ladis@linux-mips.org>
//

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include "max9867.h"

struct max9867_priv {
	struct regmap *regmap;
	const struct snd_pcm_hw_constraint_list *constraints;
	unsigned int sysclk, pclk;
	bool provider, dsp_a;
	unsigned int adc_dac_active;
};

static const char *const max9867_spmode[] = {
	"Stereo Diff", "Mono Diff",
	"Stereo Cap", "Mono Cap",
	"Stereo Single", "Mono Single",
	"Stereo Single Fast", "Mono Single Fast"
};
static const char *const max9867_filter_text[] = {"IIR", "FIR"};

static const char *const max9867_adc_dac_filter_text[] = {
	"Disabled",
	"Elliptical/16/256",
	"Butterworth/16/500",
	"Elliptical/8/256",
	"Butterworth/8/500",
	"Butterworth/8-24"
};

enum max9867_adc_dac {
	MAX9867_ADC_LEFT,
	MAX9867_ADC_RIGHT,
	MAX9867_DAC_LEFT,
	MAX9867_DAC_RIGHT,
};

static int max9867_adc_dac_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);
	enum max9867_adc_dac adc_dac;

	if (!strcmp(w->name, "ADCL"))
		adc_dac = MAX9867_ADC_LEFT;
	else if (!strcmp(w->name, "ADCR"))
		adc_dac = MAX9867_ADC_RIGHT;
	else if (!strcmp(w->name, "DACL"))
		adc_dac = MAX9867_DAC_LEFT;
	else if (!strcmp(w->name, "DACR"))
		adc_dac = MAX9867_DAC_RIGHT;
	else
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event))
		max9867->adc_dac_active |= BIT(adc_dac);
	else if (SND_SOC_DAPM_EVENT_OFF(event))
		max9867->adc_dac_active &= ~BIT(adc_dac);

	return 0;
}

static int max9867_filter_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);
	unsigned int reg;
	int ret;

	ret = regmap_read(max9867->regmap, MAX9867_CODECFLTR, &reg);
	if (ret)
		return -EINVAL;

	if (reg & MAX9867_CODECFLTR_MODE)
		ucontrol->value.enumerated.item[0] = 1;
	else
		ucontrol->value.enumerated.item[0] = 0;

	return 0;
}

static int max9867_filter_set(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);
	unsigned int reg, mode = ucontrol->value.enumerated.item[0];
	int ret;

	if (mode > 1)
		return -EINVAL;

	/* don't allow change if ADC/DAC active */
	if (max9867->adc_dac_active)
		return -EBUSY;

	/* read current filter mode */
	ret = regmap_read(max9867->regmap, MAX9867_CODECFLTR, &reg);
	if (ret)
		return -EINVAL;

	if (mode)
		mode = MAX9867_CODECFLTR_MODE;

	/* check if change is needed */
	if ((reg & MAX9867_CODECFLTR_MODE) == mode)
		return 0;

	/* shutdown codec before switching filter mode */
	regmap_update_bits(max9867->regmap, MAX9867_PWRMAN,
		MAX9867_PWRMAN_SHDN, 0);

	/* switch filter mode */
	regmap_update_bits(max9867->regmap, MAX9867_CODECFLTR,
		MAX9867_CODECFLTR_MODE, mode);

	/* out of shutdown now */
	regmap_update_bits(max9867->regmap, MAX9867_PWRMAN,
		MAX9867_PWRMAN_SHDN, MAX9867_PWRMAN_SHDN);

	return 0;
}

static SOC_ENUM_SINGLE_EXT_DECL(max9867_filter, max9867_filter_text);
static SOC_ENUM_SINGLE_DECL(max9867_dac_filter, MAX9867_CODECFLTR, 0,
	max9867_adc_dac_filter_text);
static SOC_ENUM_SINGLE_DECL(max9867_adc_filter, MAX9867_CODECFLTR, 4,
	max9867_adc_dac_filter_text);
static SOC_ENUM_SINGLE_DECL(max9867_spkmode, MAX9867_MODECONFIG, 0,
	max9867_spmode);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(max9867_master_tlv,
	 0,  2, TLV_DB_SCALE_ITEM(-8600, 200, 1),
	 3, 17, TLV_DB_SCALE_ITEM(-7800, 400, 0),
	18, 25, TLV_DB_SCALE_ITEM(-2000, 200, 0),
	26, 34, TLV_DB_SCALE_ITEM( -500, 100, 0),
	35, 40, TLV_DB_SCALE_ITEM(  350,  50, 0),
);
static DECLARE_TLV_DB_SCALE(max9867_mic_tlv, 0, 100, 0);
static DECLARE_TLV_DB_SCALE(max9867_line_tlv, -600, 200, 0);
static DECLARE_TLV_DB_SCALE(max9867_adc_tlv, -1200, 100, 0);
static DECLARE_TLV_DB_SCALE(max9867_dac_tlv, -1500, 100, 0);
static DECLARE_TLV_DB_SCALE(max9867_dacboost_tlv, 0, 600, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(max9867_micboost_tlv,
	0, 2, TLV_DB_SCALE_ITEM(-2000, 2000, 1),
	3, 3, TLV_DB_SCALE_ITEM(3000, 0, 0),
);

static const struct snd_kcontrol_new max9867_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Master Playback Volume", MAX9867_LEFTVOL,
			MAX9867_RIGHTVOL, 0, 40, 1, max9867_master_tlv),
	SOC_DOUBLE_R_TLV("Line Capture Volume", MAX9867_LEFTLINELVL,
			MAX9867_RIGHTLINELVL, 0, 15, 1, max9867_line_tlv),
	SOC_DOUBLE_R_TLV("Mic Capture Volume", MAX9867_LEFTMICGAIN,
			MAX9867_RIGHTMICGAIN, 0, 20, 1, max9867_mic_tlv),
	SOC_DOUBLE_R_TLV("Mic Boost Capture Volume", MAX9867_LEFTMICGAIN,
			MAX9867_RIGHTMICGAIN, 5, 3, 0, max9867_micboost_tlv),
	SOC_SINGLE("Digital Sidetone Volume", MAX9867_SIDETONE, 0, 31, 1),
	SOC_SINGLE_TLV("Digital Playback Volume", MAX9867_DACLEVEL, 0, 15, 1,
			max9867_dac_tlv),
	SOC_SINGLE_TLV("Digital Boost Playback Volume", MAX9867_DACLEVEL, 4, 3, 0,
			max9867_dacboost_tlv),
	SOC_DOUBLE_TLV("Digital Capture Volume", MAX9867_ADCLEVEL, 4, 0, 15, 1,
			max9867_adc_tlv),
	SOC_ENUM("Speaker Mode", max9867_spkmode),
	SOC_SINGLE("Volume Smoothing Switch", MAX9867_MODECONFIG, 6, 1, 0),
	SOC_SINGLE("Line ZC Switch", MAX9867_MODECONFIG, 5, 1, 0),
	SOC_ENUM_EXT("DSP Filter", max9867_filter, max9867_filter_get, max9867_filter_set),
	SOC_ENUM("ADC Filter", max9867_adc_filter),
	SOC_ENUM("DAC Filter", max9867_dac_filter),
	SOC_SINGLE("Mono Playback Switch", MAX9867_IFC1B, 3, 1, 0),
};

/* Input mixer */
static const struct snd_kcontrol_new max9867_input_mixer_controls[] = {
	SOC_DAPM_DOUBLE("Line Capture Switch", MAX9867_INPUTCONFIG, 7, 5, 1, 0),
	SOC_DAPM_DOUBLE("Mic Capture Switch", MAX9867_INPUTCONFIG, 6, 4, 1, 0),
};

/* Output mixer */
static const struct snd_kcontrol_new max9867_output_mixer_controls[] = {
	SOC_DAPM_DOUBLE_R("Line Bypass Switch",
			  MAX9867_LEFTLINELVL, MAX9867_RIGHTLINELVL, 6, 1, 1),
};

/* Sidetone mixer */
static const struct snd_kcontrol_new max9867_sidetone_mixer_controls[] = {
	SOC_DAPM_DOUBLE("Sidetone Switch", MAX9867_SIDETONE, 6, 7, 1, 0),
};

/* Line out switch */
static const struct snd_kcontrol_new max9867_line_out_control =
	SOC_DAPM_DOUBLE_R("Switch",
			  MAX9867_LEFTVOL, MAX9867_RIGHTVOL, 6, 1, 1);

/* DMIC mux */
static const char *const dmic_mux_text[] = {
	"ADC", "DMIC"
};
static SOC_ENUM_SINGLE_DECL(left_dmic_mux_enum,
			    MAX9867_MICCONFIG, 5, dmic_mux_text);
static SOC_ENUM_SINGLE_DECL(right_dmic_mux_enum,
			    MAX9867_MICCONFIG, 4, dmic_mux_text);
static const struct snd_kcontrol_new max9867_left_dmic_mux =
	SOC_DAPM_ENUM("DMICL Mux", left_dmic_mux_enum);
static const struct snd_kcontrol_new max9867_right_dmic_mux =
	SOC_DAPM_ENUM("DMICR Mux", right_dmic_mux_enum);

static const struct snd_soc_dapm_widget max9867_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("MICL"),
	SND_SOC_DAPM_INPUT("MICR"),
	SND_SOC_DAPM_INPUT("DMICL"),
	SND_SOC_DAPM_INPUT("DMICR"),
	SND_SOC_DAPM_INPUT("LINL"),
	SND_SOC_DAPM_INPUT("LINR"),

	SND_SOC_DAPM_PGA("Left Line Input", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Line Input", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER_NAMED_CTL("Input Mixer", SND_SOC_NOPM, 0, 0,
				     max9867_input_mixer_controls,
				     ARRAY_SIZE(max9867_input_mixer_controls)),
	SND_SOC_DAPM_MUX("DMICL Mux", SND_SOC_NOPM, 0, 0,
			 &max9867_left_dmic_mux),
	SND_SOC_DAPM_MUX("DMICR Mux", SND_SOC_NOPM, 0, 0,
			 &max9867_right_dmic_mux),
	SND_SOC_DAPM_ADC_E("ADCL", "HiFi Capture", SND_SOC_NOPM, 0, 0,
			   max9867_adc_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_ADC_E("ADCR", "HiFi Capture", SND_SOC_NOPM, 0, 0,
			   max9867_adc_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_MIXER("Digital", SND_SOC_NOPM, 0, 0,
			   max9867_sidetone_mixer_controls,
			   ARRAY_SIZE(max9867_sidetone_mixer_controls)),
	SND_SOC_DAPM_MIXER_NAMED_CTL("Output Mixer", SND_SOC_NOPM, 0, 0,
				     max9867_output_mixer_controls,
				     ARRAY_SIZE(max9867_output_mixer_controls)),
	SND_SOC_DAPM_DAC_E("DACL", "HiFi Playback", SND_SOC_NOPM, 0, 0,
			   max9867_adc_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_DAC_E("DACR", "HiFi Playback", SND_SOC_NOPM, 0, 0,
			   max9867_adc_dac_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SWITCH("Master Playback", SND_SOC_NOPM, 0, 0,
			    &max9867_line_out_control),
	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),
};

static const struct snd_soc_dapm_route max9867_audio_map[] = {
	{"Left Line Input", NULL, "LINL"},
	{"Right Line Input", NULL, "LINR"},
	{"Input Mixer", "Mic Capture Switch", "MICL"},
	{"Input Mixer", "Mic Capture Switch", "MICR"},
	{"Input Mixer", "Line Capture Switch", "Left Line Input"},
	{"Input Mixer", "Line Capture Switch", "Right Line Input"},
	{"DMICL Mux", "DMIC", "DMICL"},
	{"DMICR Mux", "DMIC", "DMICR"},
	{"DMICL Mux", "ADC", "Input Mixer"},
	{"DMICR Mux", "ADC", "Input Mixer"},
	{"ADCL", NULL, "DMICL Mux"},
	{"ADCR", NULL, "DMICR Mux"},

	{"Digital", "Sidetone Switch", "ADCL"},
	{"Digital", "Sidetone Switch", "ADCR"},
	{"DACL", NULL, "Digital"},
	{"DACR", NULL, "Digital"},

	{"Output Mixer", "Line Bypass Switch", "Left Line Input"},
	{"Output Mixer", "Line Bypass Switch", "Right Line Input"},
	{"Output Mixer", NULL, "DACL"},
	{"Output Mixer", NULL, "DACR"},
	{"Master Playback", "Switch", "Output Mixer"},
	{"LOUT", NULL, "Master Playback"},
	{"ROUT", NULL, "Master Playback"},
};

static const unsigned int max9867_rates_44k1[] = {
	11025, 22050, 44100,
};

static const struct snd_pcm_hw_constraint_list max9867_constraints_44k1 = {
	.list = max9867_rates_44k1,
	.count = ARRAY_SIZE(max9867_rates_44k1),
};

static const unsigned int max9867_rates_48k[] = {
	8000, 16000, 32000, 48000,
};

static const struct snd_pcm_hw_constraint_list max9867_constraints_48k = {
	.list = max9867_rates_48k,
	.count = ARRAY_SIZE(max9867_rates_48k),
};

static int max9867_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
        struct max9867_priv *max9867 =
		snd_soc_component_get_drvdata(dai->component);

	if (max9867->constraints)
		snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, max9867->constraints);

	return 0;
}

static int max9867_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	int value;
	unsigned long int rate, ratio;
	struct snd_soc_component *component = dai->component;
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);
	unsigned int ni = DIV_ROUND_CLOSEST_ULL(96ULL * 0x10000 * params_rate(params),
						max9867->pclk);

	/* set up the ni value */
	regmap_update_bits(max9867->regmap, MAX9867_AUDIOCLKHIGH,
		MAX9867_NI_HIGH_MASK, (0xFF00 & ni) >> 8);
	regmap_update_bits(max9867->regmap, MAX9867_AUDIOCLKLOW,
		MAX9867_NI_LOW_MASK, 0x00FF & ni);
	if (max9867->provider) {
		if (max9867->dsp_a) {
			value = MAX9867_IFC1B_48X;
		} else {
			rate = params_rate(params) * 2 * params_width(params);
			ratio = max9867->pclk / rate;
			switch (params_width(params)) {
			case 8:
			case 16:
				switch (ratio) {
				case 2:
					value = MAX9867_IFC1B_PCLK_2;
					break;
				case 4:
					value = MAX9867_IFC1B_PCLK_4;
					break;
				case 8:
					value = MAX9867_IFC1B_PCLK_8;
					break;
				case 16:
					value = MAX9867_IFC1B_PCLK_16;
					break;
				default:
					return -EINVAL;
				}
				break;
			case 24:
				value = MAX9867_IFC1B_48X;
				break;
			case 32:
				value = MAX9867_IFC1B_64X;
				break;
			default:
				return -EINVAL;
			}
		}
		regmap_update_bits(max9867->regmap, MAX9867_IFC1B,
			MAX9867_IFC1B_BCLK_MASK, value);
	} else {
		/*
		 * digital pll locks on to any externally supplied LRCLK signal
		 * and also enable rapid lock mode.
		 */
		regmap_update_bits(max9867->regmap, MAX9867_AUDIOCLKLOW,
			MAX9867_RAPID_LOCK, MAX9867_RAPID_LOCK);
		regmap_update_bits(max9867->regmap, MAX9867_AUDIOCLKHIGH,
			MAX9867_PLL, MAX9867_PLL);
	}
	return 0;
}

static int max9867_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);

	return regmap_update_bits(max9867->regmap, MAX9867_DACLEVEL,
				  1 << 6, !!mute << 6);
}

static int max9867_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);
	int value = 0;

	/* Set the prescaler based on the master clock frequency*/
	if (freq >= 10000000 && freq <= 20000000) {
		value |= MAX9867_PSCLK_10_20;
		max9867->pclk = freq;
	} else if (freq >= 20000000 && freq <= 40000000) {
		value |= MAX9867_PSCLK_20_40;
		max9867->pclk = freq / 2;
	} else if (freq >= 40000000 && freq <= 60000000) {
		value |= MAX9867_PSCLK_40_60;
		max9867->pclk = freq / 4;
	} else {
		dev_err(component->dev,
			"Invalid clock frequency %uHz (required 10-60MHz)\n",
			freq);
		return -EINVAL;
	}
	if (freq % 48000 == 0)
		max9867->constraints = &max9867_constraints_48k;
	else if (freq % 44100 == 0)
		max9867->constraints = &max9867_constraints_44k1;
	else
		dev_warn(component->dev,
			 "Unable to set exact rate with %uHz clock frequency\n",
			 freq);
	max9867->sysclk = freq;
	value = value << MAX9867_PSCLK_SHIFT;
	/* exact integer mode is not supported */
	value &= ~MAX9867_FREQ_MASK;
	regmap_update_bits(max9867->regmap, MAX9867_SYSCLK,
			MAX9867_PSCLK_MASK, value);
	return 0;
}

static int max9867_dai_set_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);
	u8 iface1A, iface1B;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBP_CFP:
		max9867->provider = true;
		iface1A = MAX9867_MASTER;
		iface1B = MAX9867_IFC1B_48X;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		max9867->provider = false;
		iface1A = iface1B = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		max9867->dsp_a = false;
		iface1A |= MAX9867_I2S_DLY;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		max9867->dsp_a = true;
		iface1A |= MAX9867_TDM_MODE | MAX9867_SDOUT_HIZ;
		break;
	default:
		return -EINVAL;
	}

	/* Clock inversion bits, BCI and WCI */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface1A |= MAX9867_WCI_MODE | MAX9867_BCI_MODE;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface1A |= MAX9867_BCI_MODE;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface1A |= MAX9867_WCI_MODE;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(max9867->regmap, MAX9867_IFC1A, iface1A);
	regmap_update_bits(max9867->regmap, MAX9867_IFC1B,
			   MAX9867_IFC1B_BCLK_MASK, iface1B);

	return 0;
}

static const struct snd_soc_dai_ops max9867_dai_ops = {
	.set_sysclk	= max9867_set_dai_sysclk,
	.set_fmt	= max9867_dai_set_fmt,
	.mute_stream	= max9867_mute,
	.startup	= max9867_startup,
	.hw_params	= max9867_dai_hw_params,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver max9867_dai[] = {
	{
	.name = "max9867-aif1",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &max9867_dai_ops,
	.symmetric_rate = 1,
	}
};

#ifdef CONFIG_PM
static int max9867_suspend(struct snd_soc_component *component)
{
	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);

	return 0;
}

static int max9867_resume(struct snd_soc_component *component)
{
	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);

	return 0;
}
#else
#define max9867_suspend	NULL
#define max9867_resume	NULL
#endif

static int max9867_set_bias_level(struct snd_soc_component *component,
				  enum snd_soc_bias_level level)
{
	int err;
	struct max9867_priv *max9867 = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			err = regcache_sync(max9867->regmap);
			if (err)
				return err;

			err = regmap_write(max9867->regmap,
					   MAX9867_PWRMAN, 0xff);
			if (err)
				return err;
		}
		break;
	case SND_SOC_BIAS_OFF:
		err = regmap_write(max9867->regmap, MAX9867_PWRMAN, 0);
		if (err)
			return err;

		regcache_mark_dirty(max9867->regmap);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_component_driver max9867_component = {
	.controls		= max9867_snd_controls,
	.num_controls		= ARRAY_SIZE(max9867_snd_controls),
	.dapm_routes		= max9867_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max9867_audio_map),
	.dapm_widgets		= max9867_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max9867_dapm_widgets),
	.suspend		= max9867_suspend,
	.resume			= max9867_resume,
	.set_bias_level		= max9867_set_bias_level,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static bool max9867_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX9867_STATUS:
	case MAX9867_JACKSTATUS:
	case MAX9867_AUXHIGH:
	case MAX9867_AUXLOW:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max9867_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= MAX9867_REVISION,
	.volatile_reg	= max9867_volatile_register,
	.cache_type	= REGCACHE_RBTREE,
};

static int max9867_i2c_probe(struct i2c_client *i2c,
		const struct i2c_device_id *id)
{
	struct max9867_priv *max9867;
	int ret, reg;

	max9867 = devm_kzalloc(&i2c->dev, sizeof(*max9867), GFP_KERNEL);
	if (!max9867)
		return -ENOMEM;

	i2c_set_clientdata(i2c, max9867);
	max9867->regmap = devm_regmap_init_i2c(i2c, &max9867_regmap);
	if (IS_ERR(max9867->regmap)) {
		ret = PTR_ERR(max9867->regmap);
		dev_err(&i2c->dev, "Failed to allocate regmap: %d\n", ret);
		return ret;
	}
	ret = regmap_read(max9867->regmap, MAX9867_REVISION, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read: %d\n", ret);
		return ret;
	}
	dev_info(&i2c->dev, "device revision: %x\n", reg);
	ret = devm_snd_soc_register_component(&i2c->dev, &max9867_component,
			max9867_dai, ARRAY_SIZE(max9867_dai));
	if (ret < 0)
		dev_err(&i2c->dev, "Failed to register component: %d\n", ret);
	return ret;
}

static const struct i2c_device_id max9867_i2c_id[] = {
	{ "max9867", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9867_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id max9867_of_match[] = {
	{ .compatible = "maxim,max9867", },
	{ }
};
MODULE_DEVICE_TABLE(of, max9867_of_match);
#endif

static struct i2c_driver max9867_i2c_driver = {
	.driver = {
		.name = "max9867",
		.of_match_table = of_match_ptr(max9867_of_match),
	},
	.probe  = max9867_i2c_probe,
	.id_table = max9867_i2c_id,
};

module_i2c_driver(max9867_i2c_driver);

MODULE_AUTHOR("Ladislav Michl <ladis@linux-mips.org>");
MODULE_DESCRIPTION("ASoC MAX9867 driver");
MODULE_LICENSE("GPL");
