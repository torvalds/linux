/*
 * es8316.c -- es8316 ALSA SoC audio driver
 * Copyright Everest Semiconductor Co.,Ltd
 *
 * Authors: David Yang <yangxiaohua@everest-semi.com>,
 *          Daniel Drake <drake@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include "es8316.h"

/* In slave mode at single speed, the codec is documented as accepting 5
 * MCLK/LRCK ratios, but we also add ratio 400, which is commonly used on
 * Intel Cherry Trail platforms (19.2MHz MCLK, 48kHz LRCK).
 */
#define NR_SUPPORTED_MCLK_LRCK_RATIOS 6
static const unsigned int supported_mclk_lrck_ratios[] = {
	256, 384, 400, 512, 768, 1024
};

struct es8316_priv {
	unsigned int sysclk;
	unsigned int allowed_rates[NR_SUPPORTED_MCLK_LRCK_RATIOS];
	struct snd_pcm_hw_constraint_list sysclk_constraints;
};

/*
 * ES8316 controls
 */
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(dac_vol_tlv, -9600, 50, 1);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(adc_vol_tlv, -9600, 50, 1);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(alc_max_gain_tlv, -650, 150, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(alc_min_gain_tlv, -1200, 150, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(alc_target_tlv, -1650, 150, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(hpmixer_gain_tlv,
	0, 4, TLV_DB_SCALE_ITEM(-1200, 150, 0),
	8, 11, TLV_DB_SCALE_ITEM(-450, 150, 0),
);

static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(adc_pga_gain_tlv,
	0, 0, TLV_DB_SCALE_ITEM(-350, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(0, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(250, 0, 0),
	3, 3, TLV_DB_SCALE_ITEM(450, 0, 0),
	4, 4, TLV_DB_SCALE_ITEM(700, 0, 0),
	5, 5, TLV_DB_SCALE_ITEM(1000, 0, 0),
	6, 6, TLV_DB_SCALE_ITEM(1300, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(1600, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(1800, 0, 0),
	9, 9, TLV_DB_SCALE_ITEM(2100, 0, 0),
	10, 10, TLV_DB_SCALE_ITEM(2400, 0, 0),
);

static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(hpout_vol_tlv,
	0, 0, TLV_DB_SCALE_ITEM(-4800, 0, 0),
	1, 3, TLV_DB_SCALE_ITEM(-2400, 1200, 0),
);

static const char * const ng_type_txt[] =
	{ "Constant PGA Gain", "Mute ADC Output" };
static const struct soc_enum ng_type =
	SOC_ENUM_SINGLE(ES8316_ADC_ALC_NG, 6, 2, ng_type_txt);

static const char * const adcpol_txt[] = { "Normal", "Invert" };
static const struct soc_enum adcpol =
	SOC_ENUM_SINGLE(ES8316_ADC_MUTE, 1, 2, adcpol_txt);
static const char *const dacpol_txt[] =
	{ "Normal", "R Invert", "L Invert", "L + R Invert" };
static const struct soc_enum dacpol =
	SOC_ENUM_SINGLE(ES8316_DAC_SET1, 0, 4, dacpol_txt);

static const struct snd_kcontrol_new es8316_snd_controls[] = {
	SOC_DOUBLE_TLV("Headphone Playback Volume", ES8316_CPHP_ICAL_VOL,
		       4, 0, 3, 1, hpout_vol_tlv),
	SOC_DOUBLE_TLV("Headphone Mixer Volume", ES8316_HPMIX_VOL,
		       0, 4, 11, 0, hpmixer_gain_tlv),

	SOC_ENUM("Playback Polarity", dacpol),
	SOC_DOUBLE_R_TLV("DAC Playback Volume", ES8316_DAC_VOLL,
			 ES8316_DAC_VOLR, 0, 0xc0, 1, dac_vol_tlv),
	SOC_SINGLE("DAC Soft Ramp Switch", ES8316_DAC_SET1, 4, 1, 1),
	SOC_SINGLE("DAC Soft Ramp Rate", ES8316_DAC_SET1, 2, 4, 0),
	SOC_SINGLE("DAC Notch Filter Switch", ES8316_DAC_SET2, 6, 1, 0),
	SOC_SINGLE("DAC Double Fs Switch", ES8316_DAC_SET2, 7, 1, 0),
	SOC_SINGLE("DAC Stereo Enhancement", ES8316_DAC_SET3, 0, 7, 0),

	SOC_ENUM("Capture Polarity", adcpol),
	SOC_SINGLE("Mic Boost Switch", ES8316_ADC_D2SEPGA, 0, 1, 0),
	SOC_SINGLE_TLV("ADC Capture Volume", ES8316_ADC_VOLUME,
		       0, 0xc0, 1, adc_vol_tlv),
	SOC_SINGLE_TLV("ADC PGA Gain Volume", ES8316_ADC_PGAGAIN,
		       4, 10, 0, adc_pga_gain_tlv),
	SOC_SINGLE("ADC Soft Ramp Switch", ES8316_ADC_MUTE, 4, 1, 0),
	SOC_SINGLE("ADC Double Fs Switch", ES8316_ADC_DMIC, 4, 1, 0),

	SOC_SINGLE("ALC Capture Switch", ES8316_ADC_ALC1, 6, 1, 0),
	SOC_SINGLE_TLV("ALC Capture Max Volume", ES8316_ADC_ALC1, 0, 28, 0,
		       alc_max_gain_tlv),
	SOC_SINGLE_TLV("ALC Capture Min Volume", ES8316_ADC_ALC2, 0, 28, 0,
		       alc_min_gain_tlv),
	SOC_SINGLE_TLV("ALC Capture Target Volume", ES8316_ADC_ALC3, 4, 10, 0,
		       alc_target_tlv),
	SOC_SINGLE("ALC Capture Hold Time", ES8316_ADC_ALC3, 0, 10, 0),
	SOC_SINGLE("ALC Capture Decay Time", ES8316_ADC_ALC4, 4, 10, 0),
	SOC_SINGLE("ALC Capture Attack Time", ES8316_ADC_ALC4, 0, 10, 0),
	SOC_SINGLE("ALC Capture Noise Gate Switch", ES8316_ADC_ALC_NG,
		   5, 1, 0),
	SOC_SINGLE("ALC Capture Noise Gate Threshold", ES8316_ADC_ALC_NG,
		   0, 31, 0),
	SOC_ENUM("ALC Capture Noise Gate Type", ng_type),
};

/* Analog Input Mux */
static const char * const es8316_analog_in_txt[] = {
		"lin1-rin1",
		"lin2-rin2",
		"lin1-rin1 with 20db Boost",
		"lin2-rin2 with 20db Boost"
};
static const unsigned int es8316_analog_in_values[] = { 0, 1, 2, 3 };
static const struct soc_enum es8316_analog_input_enum =
	SOC_VALUE_ENUM_SINGLE(ES8316_ADC_PDN_LINSEL, 4, 3,
			      ARRAY_SIZE(es8316_analog_in_txt),
			      es8316_analog_in_txt,
			      es8316_analog_in_values);
static const struct snd_kcontrol_new es8316_analog_in_mux_controls =
	SOC_DAPM_ENUM("Route", es8316_analog_input_enum);

static const char * const es8316_dmic_txt[] = {
		"dmic disable",
		"dmic data at high level",
		"dmic data at low level",
};
static const unsigned int es8316_dmic_values[] = { 0, 1, 2 };
static const struct soc_enum es8316_dmic_src_enum =
	SOC_VALUE_ENUM_SINGLE(ES8316_ADC_DMIC, 0, 3,
			      ARRAY_SIZE(es8316_dmic_txt),
			      es8316_dmic_txt,
			      es8316_dmic_values);
static const struct snd_kcontrol_new es8316_dmic_src_controls =
	SOC_DAPM_ENUM("Route", es8316_dmic_src_enum);

/* hp mixer mux */
static const char * const es8316_hpmux_texts[] = {
	"lin1-rin1",
	"lin2-rin2",
	"lin-rin with Boost",
	"lin-rin with Boost and PGA"
};

static const unsigned int es8316_hpmux_values[] = { 0, 1, 2, 3 };

static SOC_ENUM_SINGLE_DECL(es8316_left_hpmux_enum, ES8316_HPMIX_SEL,
	4, es8316_hpmux_texts);

static const struct snd_kcontrol_new es8316_left_hpmux_controls =
	SOC_DAPM_ENUM("Route", es8316_left_hpmux_enum);

static SOC_ENUM_SINGLE_DECL(es8316_right_hpmux_enum, ES8316_HPMIX_SEL,
	0, es8316_hpmux_texts);

static const struct snd_kcontrol_new es8316_right_hpmux_controls =
	SOC_DAPM_ENUM("Route", es8316_right_hpmux_enum);

/* headphone Output Mixer */
static const struct snd_kcontrol_new es8316_out_left_mix[] = {
	SOC_DAPM_SINGLE("LLIN Switch", ES8316_HPMIX_SWITCH, 6, 1, 0),
	SOC_DAPM_SINGLE("Left DAC Switch", ES8316_HPMIX_SWITCH, 7, 1, 0),
};
static const struct snd_kcontrol_new es8316_out_right_mix[] = {
	SOC_DAPM_SINGLE("RLIN Switch", ES8316_HPMIX_SWITCH, 2, 1, 0),
	SOC_DAPM_SINGLE("Right DAC Switch", ES8316_HPMIX_SWITCH, 3, 1, 0),
};

/* DAC data source mux */
static const char * const es8316_dacsrc_texts[] = {
	"LDATA TO LDAC, RDATA TO RDAC",
	"LDATA TO LDAC, LDATA TO RDAC",
	"RDATA TO LDAC, RDATA TO RDAC",
	"RDATA TO LDAC, LDATA TO RDAC",
};

static const unsigned int es8316_dacsrc_values[] = { 0, 1, 2, 3 };

static SOC_ENUM_SINGLE_DECL(es8316_dacsrc_mux_enum, ES8316_DAC_SET1,
	6, es8316_dacsrc_texts);

static const struct snd_kcontrol_new es8316_dacsrc_mux_controls =
	SOC_DAPM_ENUM("Route", es8316_dacsrc_mux_enum);

static const struct snd_soc_dapm_widget es8316_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("Bias", ES8316_SYS_PDN, 3, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Analog power", ES8316_SYS_PDN, 4, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Bias", ES8316_SYS_PDN, 5, 1, NULL, 0),

	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),

	/* Input Mux */
	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_analog_in_mux_controls),

	SND_SOC_DAPM_SUPPLY("ADC Vref", ES8316_SYS_PDN, 1, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC bias", ES8316_SYS_PDN, 2, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Clock", ES8316_CLKMGR_CLKSW, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Line input PGA", ES8316_ADC_PDN_LINSEL,
			 7, 1, NULL, 0),
	SND_SOC_DAPM_ADC("Mono ADC", NULL, ES8316_ADC_PDN_LINSEL, 6, 1),
	SND_SOC_DAPM_MUX("Digital Mic Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_dmic_src_controls),

	/* Digital Interface */
	SND_SOC_DAPM_AIF_OUT("I2S OUT", "I2S1 Capture",  1,
			     ES8316_SERDATA_ADC, 6, 1),
	SND_SOC_DAPM_AIF_IN("I2S IN", "I2S1 Playback", 0,
			    SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_MUX("DAC Source Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_dacsrc_mux_controls),

	SND_SOC_DAPM_SUPPLY("DAC Vref", ES8316_SYS_PDN, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Clock", ES8316_CLKMGR_CLKSW, 2, 0, NULL, 0),
	SND_SOC_DAPM_DAC("Right DAC", NULL, ES8316_DAC_PDN, 0, 1),
	SND_SOC_DAPM_DAC("Left DAC", NULL, ES8316_DAC_PDN, 4, 1),

	/* Headphone Output Side */
	SND_SOC_DAPM_MUX("Left Headphone Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_left_hpmux_controls),
	SND_SOC_DAPM_MUX("Right Headphone Mux", SND_SOC_NOPM, 0, 0,
			 &es8316_right_hpmux_controls),
	SND_SOC_DAPM_MIXER("Left Headphone Mixer", ES8316_HPMIX_PDN,
			   5, 1, &es8316_out_left_mix[0],
			   ARRAY_SIZE(es8316_out_left_mix)),
	SND_SOC_DAPM_MIXER("Right Headphone Mixer", ES8316_HPMIX_PDN,
			   1, 1, &es8316_out_right_mix[0],
			   ARRAY_SIZE(es8316_out_right_mix)),
	SND_SOC_DAPM_PGA("Left Headphone Mixer Out", ES8316_HPMIX_PDN,
			 4, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right Headphone Mixer Out", ES8316_HPMIX_PDN,
			 0, 1, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("Left Headphone Charge Pump", ES8316_CPHP_OUTEN,
			     6, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Right Headphone Charge Pump", ES8316_CPHP_OUTEN,
			     2, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Headphone Charge Pump", ES8316_CPHP_PDN2,
			    5, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Headphone Charge Pump Clock", ES8316_CLKMGR_CLKSW,
			    4, 0, NULL, 0),

	SND_SOC_DAPM_OUT_DRV("Left Headphone Driver", ES8316_CPHP_OUTEN,
			     5, 0, NULL, 0),
	SND_SOC_DAPM_OUT_DRV("Right Headphone Driver", ES8316_CPHP_OUTEN,
			     1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Headphone Out", ES8316_CPHP_PDN1, 2, 1, NULL, 0),

	/* pdn_Lical and pdn_Rical bits are documented as Reserved, but must
	 * be explicitly unset in order to enable HP output
	 */
	SND_SOC_DAPM_SUPPLY("Left Headphone ical", ES8316_CPHP_ICAL_VOL,
			    7, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Right Headphone ical", ES8316_CPHP_ICAL_VOL,
			    3, 1, NULL, 0),

	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
};

static const struct snd_soc_dapm_route es8316_dapm_routes[] = {
	/* Recording */
	{"MIC1", NULL, "Mic Bias"},
	{"MIC2", NULL, "Mic Bias"},
	{"MIC1", NULL, "Bias"},
	{"MIC2", NULL, "Bias"},
	{"MIC1", NULL, "Analog power"},
	{"MIC2", NULL, "Analog power"},

	{"Differential Mux", "lin1-rin1", "MIC1"},
	{"Differential Mux", "lin2-rin2", "MIC2"},
	{"Line input PGA", NULL, "Differential Mux"},

	{"Mono ADC", NULL, "ADC Clock"},
	{"Mono ADC", NULL, "ADC Vref"},
	{"Mono ADC", NULL, "ADC bias"},
	{"Mono ADC", NULL, "Line input PGA"},

	/* It's not clear why, but to avoid recording only silence,
	 * the DAC clock must be running for the ADC to work.
	 */
	{"Mono ADC", NULL, "DAC Clock"},

	{"Digital Mic Mux", "dmic disable", "Mono ADC"},

	{"I2S OUT", NULL, "Digital Mic Mux"},

	/* Playback */
	{"DAC Source Mux", "LDATA TO LDAC, RDATA TO RDAC", "I2S IN"},

	{"Left DAC", NULL, "DAC Clock"},
	{"Right DAC", NULL, "DAC Clock"},

	{"Left DAC", NULL, "DAC Vref"},
	{"Right DAC", NULL, "DAC Vref"},

	{"Left DAC", NULL, "DAC Source Mux"},
	{"Right DAC", NULL, "DAC Source Mux"},

	{"Left Headphone Mux", "lin-rin with Boost and PGA", "Line input PGA"},
	{"Right Headphone Mux", "lin-rin with Boost and PGA", "Line input PGA"},

	{"Left Headphone Mixer", "LLIN Switch", "Left Headphone Mux"},
	{"Left Headphone Mixer", "Left DAC Switch", "Left DAC"},

	{"Right Headphone Mixer", "RLIN Switch", "Right Headphone Mux"},
	{"Right Headphone Mixer", "Right DAC Switch", "Right DAC"},

	{"Left Headphone Mixer Out", NULL, "Left Headphone Mixer"},
	{"Right Headphone Mixer Out", NULL, "Right Headphone Mixer"},

	{"Left Headphone Charge Pump", NULL, "Left Headphone Mixer Out"},
	{"Right Headphone Charge Pump", NULL, "Right Headphone Mixer Out"},

	{"Left Headphone Charge Pump", NULL, "Headphone Charge Pump"},
	{"Right Headphone Charge Pump", NULL, "Headphone Charge Pump"},

	{"Left Headphone Charge Pump", NULL, "Headphone Charge Pump Clock"},
	{"Right Headphone Charge Pump", NULL, "Headphone Charge Pump Clock"},

	{"Left Headphone Driver", NULL, "Left Headphone Charge Pump"},
	{"Right Headphone Driver", NULL, "Right Headphone Charge Pump"},

	{"HPOL", NULL, "Left Headphone Driver"},
	{"HPOR", NULL, "Right Headphone Driver"},

	{"HPOL", NULL, "Left Headphone ical"},
	{"HPOR", NULL, "Right Headphone ical"},

	{"Headphone Out", NULL, "Bias"},
	{"Headphone Out", NULL, "Analog power"},
	{"HPOL", NULL, "Headphone Out"},
	{"HPOR", NULL, "Headphone Out"},
};

static int es8316_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct es8316_priv *es8316 = snd_soc_component_get_drvdata(component);
	int i;
	int count = 0;

	es8316->sysclk = freq;

	if (freq == 0)
		return 0;

	/* Limit supported sample rates to ones that can be autodetected
	 * by the codec running in slave mode.
	 */
	for (i = 0; i < NR_SUPPORTED_MCLK_LRCK_RATIOS; i++) {
		const unsigned int ratio = supported_mclk_lrck_ratios[i];

		if (freq % ratio == 0)
			es8316->allowed_rates[count++] = freq / ratio;
	}

	es8316->sysclk_constraints.list = es8316->allowed_rates;
	es8316->sysclk_constraints.count = count;

	return 0;
}

static int es8316_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u8 serdata1 = 0;
	u8 serdata2 = 0;
	u8 clksw;
	u8 mask;

	if ((fmt & SND_SOC_DAIFMT_MASTER_MASK) != SND_SOC_DAIFMT_CBS_CFS) {
		dev_err(component->dev, "Codec driver only supports slave mode\n");
		return -EINVAL;
	}

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) != SND_SOC_DAIFMT_I2S) {
		dev_err(component->dev, "Codec driver only supports I2S format\n");
		return -EINVAL;
	}

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		serdata1 |= ES8316_SERDATA1_BCLK_INV;
		serdata2 |= ES8316_SERDATA2_ADCLRP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		serdata1 |= ES8316_SERDATA1_BCLK_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		serdata2 |= ES8316_SERDATA2_ADCLRP;
		break;
	default:
		return -EINVAL;
	}

	mask = ES8316_SERDATA1_MASTER | ES8316_SERDATA1_BCLK_INV;
	snd_soc_component_update_bits(component, ES8316_SERDATA1, mask, serdata1);

	mask = ES8316_SERDATA2_FMT_MASK | ES8316_SERDATA2_ADCLRP;
	snd_soc_component_update_bits(component, ES8316_SERDATA_ADC, mask, serdata2);
	snd_soc_component_update_bits(component, ES8316_SERDATA_DAC, mask, serdata2);

	/* Enable BCLK and MCLK inputs in slave mode */
	clksw = ES8316_CLKMGR_CLKSW_MCLK_ON | ES8316_CLKMGR_CLKSW_BCLK_ON;
	snd_soc_component_update_bits(component, ES8316_CLKMGR_CLKSW, clksw, clksw);

	return 0;
}

static int es8316_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8316_priv *es8316 = snd_soc_component_get_drvdata(component);

	if (es8316->sysclk == 0) {
		dev_err(component->dev, "No sysclk provided\n");
		return -EINVAL;
	}

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC.
	 */
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   &es8316->sysclk_constraints);

	return 0;
}

static int es8316_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8316_priv *es8316 = snd_soc_component_get_drvdata(component);
	u8 wordlen = 0;

	if (!es8316->sysclk) {
		dev_err(component->dev, "No MCLK configured\n");
		return -EINVAL;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		wordlen = ES8316_SERDATA2_LEN_16;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		wordlen = ES8316_SERDATA2_LEN_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		wordlen = ES8316_SERDATA2_LEN_24;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		wordlen = ES8316_SERDATA2_LEN_32;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, ES8316_SERDATA_DAC,
			    ES8316_SERDATA2_LEN_MASK, wordlen);
	snd_soc_component_update_bits(component, ES8316_SERDATA_ADC,
			    ES8316_SERDATA2_LEN_MASK, wordlen);
	return 0;
}

static int es8316_mute(struct snd_soc_dai *dai, int mute)
{
	snd_soc_component_update_bits(dai->component, ES8316_DAC_SET1, 0x20,
			    mute ? 0x20 : 0);
	return 0;
}

#define ES8316_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops es8316_ops = {
	.startup = es8316_pcm_startup,
	.hw_params = es8316_pcm_hw_params,
	.set_fmt = es8316_set_dai_fmt,
	.set_sysclk = es8316_set_dai_sysclk,
	.digital_mute = es8316_mute,
};

static struct snd_soc_dai_driver es8316_dai = {
	.name = "ES8316 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = ES8316_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = ES8316_FORMATS,
	},
	.ops = &es8316_ops,
	.symmetric_rates = 1,
};

static int es8316_probe(struct snd_soc_component *component)
{
	/* Reset codec and enable current state machine */
	snd_soc_component_write(component, ES8316_RESET, 0x3f);
	usleep_range(5000, 5500);
	snd_soc_component_write(component, ES8316_RESET, ES8316_RESET_CSM_ON);
	msleep(30);

	/*
	 * Documentation is unclear, but this value from the vendor driver is
	 * needed otherwise audio output is silent.
	 */
	snd_soc_component_write(component, ES8316_SYS_VMIDSEL, 0xff);

	/*
	 * Documentation for this register is unclear and incomplete,
	 * but here is a vendor-provided value that improves volume
	 * and quality for Intel CHT platforms.
	 */
	snd_soc_component_write(component, ES8316_CLKMGR_ADCOSR, 0x32);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_es8316 = {
	.probe			= es8316_probe,
	.controls		= es8316_snd_controls,
	.num_controls		= ARRAY_SIZE(es8316_snd_controls),
	.dapm_widgets		= es8316_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es8316_dapm_widgets),
	.dapm_routes		= es8316_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(es8316_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config es8316_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x53,
	.cache_type = REGCACHE_RBTREE,
};

static int es8316_i2c_probe(struct i2c_client *i2c_client,
			    const struct i2c_device_id *id)
{
	struct es8316_priv *es8316;
	struct regmap *regmap;

	es8316 = devm_kzalloc(&i2c_client->dev, sizeof(struct es8316_priv),
			      GFP_KERNEL);
	if (es8316 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c_client, es8316);

	regmap = devm_regmap_init_i2c(i2c_client, &es8316_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	return devm_snd_soc_register_component(&i2c_client->dev,
				      &soc_component_dev_es8316,
				      &es8316_dai, 1);
}

static const struct i2c_device_id es8316_i2c_id[] = {
	{"es8316", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, es8316_i2c_id);

static const struct of_device_id es8316_of_match[] = {
	{ .compatible = "everest,es8316", },
	{},
};
MODULE_DEVICE_TABLE(of, es8316_of_match);

static const struct acpi_device_id es8316_acpi_match[] = {
	{"ESSX8316", 0},
	{},
};
MODULE_DEVICE_TABLE(acpi, es8316_acpi_match);

static struct i2c_driver es8316_i2c_driver = {
	.driver = {
		.name			= "es8316",
		.acpi_match_table	= ACPI_PTR(es8316_acpi_match),
		.of_match_table		= of_match_ptr(es8316_of_match),
	},
	.probe		= es8316_i2c_probe,
	.id_table	= es8316_i2c_id,
};
module_i2c_driver(es8316_i2c_driver);

MODULE_DESCRIPTION("Everest Semi ES8316 ALSA SoC Codec Driver");
MODULE_AUTHOR("David Yang <yangxiaohua@everest-semi.com>");
MODULE_LICENSE("GPL v2");
