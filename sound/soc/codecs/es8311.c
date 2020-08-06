// SPDX-License-Identifier: GPL-2.0
/*
 * es8311.c  --  ES8311/ES8312 ALSA SoC Audio Codec
 *
 * Copyright (C) 2018 Everest Semiconductor Co., Ltd
 *
 * Authors:  David Yang(yangxiaohua@everest-semi.com)
 *
 *
 * Based on es8374.c by David Yang(yangxiaohua@everest-semi.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/stddef.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "es8311.h"

/* codec private data */
struct	es8311_priv {
	struct snd_soc_component *component;
	struct clk *mclk_in;
	struct gpio_desc *spk_ctl_gpio;
	struct regmap *regmap;
	/* Optional properties: */
	int adc_pga_gain;
	int adc_volume;
	int dac_volume;
	int aec_mode;
	int delay_pa_drv_ms;
};

static const DECLARE_TLV_DB_SCALE(vdac_tlv,
				  -9550, 50, true);
static const DECLARE_TLV_DB_SCALE(vadc_tlv,
				  -9550, 50, true);
static const DECLARE_TLV_DB_SCALE(mic_pga_tlv,
				  0, 300, true);
static const DECLARE_TLV_DB_SCALE(adc_scale_tlv,
				  0, 600, false);
static const DECLARE_TLV_DB_SCALE(alc_winsize_tlv,
				  0, 25, false);
static const DECLARE_TLV_DB_SCALE(alc_maxlevel_tlv,
				  -3600, 200, false);
static const DECLARE_TLV_DB_SCALE(alc_minlevel_tlv,
				  -3600, 200, false);
static const DECLARE_TLV_DB_SCALE(alc_noisegate_tlv,
				  -9600, 600, false);
static const DECLARE_TLV_DB_SCALE(alc_noisegate_winsize_tlv,
				  4200, 4200, false);
static const DECLARE_TLV_DB_SCALE(alc_automute_gain_tlv,
				  4200, 4200, false);
static const DECLARE_TLV_DB_SCALE(adc_ramprate_tlv,
				  0, 25, false);

static const char *const dmic_type_txt[] = {
	"dmic at high level",
	"dmic at low level"
};
static const struct soc_enum dmic_type =
	SOC_ENUM_SINGLE(ES8311_ADC_REG15, 0, 1, dmic_type_txt);

static const char *const automute_type_txt[] = {
	"automute disabled",
	"automute enable"
};
static const struct soc_enum alc_automute_type =
	SOC_ENUM_SINGLE(ES8311_ADC_REG18, 6, 1, automute_type_txt);

static const char *const dacdsm_mute_type_txt[] = {
	"mute to 8",
	"mute to 7/9"
};
static const struct soc_enum dacdsm_mute_type =
	SOC_ENUM_SINGLE(ES8311_DAC_REG31, 7, 1, dacdsm_mute_type_txt);

static const char *const aec_type_txt[] = {
	"adc left, adc right",
	"adc left, null right",
	"null left, adc right",
	"null left, null right",
	"dac left, adc right",
	"adc left, dac right",
	"dac left, dac right",
	"N/A"
};
static const struct soc_enum aec_type =
	SOC_ENUM_SINGLE(ES8311_GPIO_REG44, 4, 7, aec_type_txt);

static const char *const adc2dac_sel_txt[] = {
	"disable",
	"adc data to dac",
};
static const struct soc_enum adc2dac_sel =
	SOC_ENUM_SINGLE(ES8311_GPIO_REG44, 7, 1, adc2dac_sel_txt);

static const char *const mclk_sel_txt[] = {
	"from mclk pin",
	"from bclk",
};
static const struct soc_enum mclk_src =
	SOC_ENUM_SINGLE(ES8311_CLK_MANAGER_REG01, 7, 1, mclk_sel_txt);

/*
 * es8311 Controls
 */
static const struct snd_kcontrol_new es8311_snd_controls[] = {
	SOC_SINGLE_TLV("MIC PGA GAIN", ES8311_SYSTEM_REG14,
		       0, 10, 0, mic_pga_tlv),
	SOC_SINGLE_TLV("ADC SCALE", ES8311_ADC_REG16,
		       0, 7, 0, adc_scale_tlv),
	SOC_ENUM("DMIC TYPE", dmic_type),
	SOC_SINGLE_TLV("ADC RAMP RATE", ES8311_ADC_REG15,
		       4, 15, 0, adc_ramprate_tlv),
	SOC_SINGLE("ADC SDP MUTE", ES8311_SDPOUT_REG0A, 6, 1, 0),
	SOC_SINGLE("ADC INVERTED", ES8311_ADC_REG16, 4, 1, 0),
	SOC_SINGLE("ADC SYNC", ES8311_ADC_REG16, 5, 1, 1),
	SOC_SINGLE("ADC RAM CLR", ES8311_ADC_REG16, 3, 1, 0),
	SOC_SINGLE_TLV("ADC VOLUME", ES8311_ADC_REG17,
		       0, 255, 0, vadc_tlv),
	SOC_SINGLE("ALC ENABLE", ES8311_ADC_REG18, 7, 1, 0),
	SOC_ENUM("ALC AUTOMUTE TYPE", alc_automute_type),
	SOC_SINGLE_TLV("ALC WIN SIZE", ES8311_ADC_REG18,
		       0, 15, 0, alc_winsize_tlv),
	SOC_SINGLE_TLV("ALC MAX LEVEL", ES8311_ADC_REG19,
		       4, 15, 0, alc_maxlevel_tlv),
	SOC_SINGLE_TLV("ALC MIN LEVEL", ES8311_ADC_REG19,
		       0, 15, 0, alc_minlevel_tlv),
	SOC_SINGLE_TLV("ALC AUTOMUTE WINSIZE", ES8311_ADC_REG1A,
		       4, 15, 0, alc_noisegate_winsize_tlv),
	SOC_SINGLE_TLV("ALC AUTOMUTE GATE THRESHOLD", ES8311_ADC_REG1A,
		       0, 15, 0, alc_noisegate_tlv),
	SOC_SINGLE_TLV("ALC AUTOMUTE VOLUME", ES8311_ADC_REG1B,
		       5, 7, 0, alc_automute_gain_tlv),
	SOC_SINGLE("ADC FS MODE", ES8311_CLK_MANAGER_REG03, 6, 1, 0),
	SOC_SINGLE("ADC OSR", ES8311_CLK_MANAGER_REG03, 0, 63, 0),
	SOC_SINGLE("DAC SDP MUTE", ES8311_SDPIN_REG09, 6, 1, 0),
	SOC_SINGLE("DAC DEM MUTE", ES8311_DAC_REG31, 5, 1, 0),
	SOC_SINGLE("DAC INVERT", ES8311_DAC_REG31, 4, 1, 0),
	SOC_SINGLE("DAC RAM CLR", ES8311_DAC_REG31, 3, 1, 0),
	SOC_ENUM("DAC DSM MUTE", dacdsm_mute_type),
	SOC_SINGLE("DAC OFFSET", ES8311_DAC_REG33, 0, 255, 0),
	SOC_SINGLE_TLV("DAC VOLUME", ES8311_DAC_REG32,
		       0, 255, 0, vdac_tlv),
	SOC_SINGLE("DRC ENABLE", ES8311_DAC_REG34, 7, 1, 0),
	SOC_SINGLE_TLV("DRC WIN SIZE",	ES8311_DAC_REG34,
		       0, 15, 0, alc_winsize_tlv),
	SOC_SINGLE_TLV("DRC MAX LEVEL",	ES8311_DAC_REG35,
		       4, 15, 0, alc_maxlevel_tlv),
	SOC_SINGLE_TLV("DRC MIN LEVEL",	ES8311_DAC_REG35,
		       0, 15, 0, alc_minlevel_tlv),
	SOC_SINGLE_TLV("DAC RAMP RATE",	ES8311_DAC_REG37,
		       4, 15, 0, adc_ramprate_tlv),
	SOC_SINGLE("DAC OSR", ES8311_CLK_MANAGER_REG04, 0, 127, 0),
	SOC_ENUM("AEC MODE", aec_type),
	SOC_ENUM("ADC DATA TO DAC TEST MODE", adc2dac_sel),
	SOC_SINGLE("MCLK INVERT", ES8311_CLK_MANAGER_REG01, 6, 1, 0),
	SOC_SINGLE("BCLK INVERT", ES8311_CLK_MANAGER_REG06, 5, 1, 0),
	SOC_ENUM("MCLK SOURCE", mclk_src),
};

/*
 * DAPM Controls
 */
static const char *const es8311_dmic_mux_txt[] = {
	"DMIC DISABLE",
	"DMIC ENABLE",
};
static const unsigned int es8311_dmic_mux_values[] = {
	0, 1
};
static const struct soc_enum es8311_dmic_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8311_SYSTEM_REG14, 6, 1,
			      ARRAY_SIZE(es8311_dmic_mux_txt),
			      es8311_dmic_mux_txt,
			      es8311_dmic_mux_values);
static const struct snd_kcontrol_new es8311_dmic_mux_controls =
	SOC_DAPM_ENUM("DMIC ROUTE", es8311_dmic_mux_enum);

static const char *const es8311_adc_sdp_mux_txt[] = {
	"FROM ADC OUT",
	"FROM EQUALIZER",
};
static const unsigned int es8311_adc_sdp_mux_values[] = {
	0, 1
};
static const struct soc_enum es8311_adc_sdp_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8311_ADC_REG1C, 6, 1,
			      ARRAY_SIZE(es8311_adc_sdp_mux_txt),
			      es8311_adc_sdp_mux_txt,
			      es8311_adc_sdp_mux_values);
static const struct snd_kcontrol_new es8311_adc_sdp_mux_controls =
	SOC_DAPM_ENUM("ADC SDP ROUTE", es8311_adc_sdp_mux_enum);

/*
 * DAC data source
 */
static const char *const es8311_dac_data_mux_txt[] = {
	"SELECT SDP LEFT DATA",
	"SELECT SDP RIGHT DATA",
};
static const unsigned int es8311_dac_data_mux_values[] = {
	0, 1
};
static const struct soc_enum  es8311_dac_data_mux_enum =
	SOC_VALUE_ENUM_SINGLE(ES8311_SDPIN_REG09, 7, 1,
			      ARRAY_SIZE(es8311_dac_data_mux_txt),
			      es8311_dac_data_mux_txt,
			      es8311_dac_data_mux_values);
static const struct snd_kcontrol_new es8311_dac_data_mux_controls =
	SOC_DAPM_ENUM("DAC SDP ROUTE", es8311_dac_data_mux_enum);

static const struct snd_soc_dapm_widget es8311_dapm_widgets[] = {
	/* Input*/
	SND_SOC_DAPM_INPUT("DMIC"),
	SND_SOC_DAPM_INPUT("AMIC"),

	SND_SOC_DAPM_PGA("INPUT PGA", ES8311_SYSTEM_REG0E,
			 6, 1, NULL, 0),
	/* ADCs */
	SND_SOC_DAPM_ADC("MONO ADC", NULL, ES8311_SYSTEM_REG0E, 5, 1),
	/* Dmic MUX */
	SND_SOC_DAPM_MUX("DMIC MUX", SND_SOC_NOPM, 0, 0,
			 &es8311_dmic_mux_controls),
	/* sdp MUX */
	SND_SOC_DAPM_MUX("SDP OUT MUX", SND_SOC_NOPM, 0, 0,
			 &es8311_adc_sdp_mux_controls),
	/* Digital Interface */
	SND_SOC_DAPM_AIF_OUT("I2S OUT", "I2S1 Capture",  1,
			     SND_SOC_NOPM, 0, 0),
	/* Render path	*/
	SND_SOC_DAPM_AIF_IN("I2S IN", "I2S1 Playback", 0,
			    SND_SOC_NOPM, 0, 0),
	/*DACs SDP DATA SRC MUX */
	SND_SOC_DAPM_MUX("DAC SDP SRC MUX", SND_SOC_NOPM, 0, 0,
			 &es8311_dac_data_mux_controls),
	SND_SOC_DAPM_DAC("MONO DAC", NULL, SND_SOC_NOPM, 0, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("DIFFERENTIAL OUT"),
};

static const struct snd_soc_dapm_route es8311_dapm_routes[] = {
	/* record route map */
	{"INPUT PGA", NULL, "AMIC"},
	{"MONO ADC", NULL, "INPUT PGA"},
	{"DMIC MUX", "DMIC DISABLE", "MONO ADC"},
	{"DMIC MUX", "DMIC ENABLE", "DMIC"},
	{"SDP OUT MUX", "FROM ADC OUT", "DMIC MUX"},
	{"SDP OUT MUX", "FROM EQUALIZER", "DMIC MUX"},
	{"I2S OUT", NULL, "SDP OUT MUX"},
	/* playback route map */
	{"DAC SDP SRC MUX", "SELECT SDP LEFT DATA", "I2S IN"},
	{"DAC SDP SRC MUX", "SELECT SDP RIGHT DATA", "I2S IN"},
	{"MONO DAC", NULL, "DAC SDP SRC MUX"},
	{"DIFFERENTIAL OUT", NULL, "MONO DAC"},
};

static int es8311_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u8 iface = 0;
	u8 adciface = 0;
	u8 daciface = 0;

	iface    = snd_soc_component_read(component, ES8311_RESET_REG00);
	adciface = snd_soc_component_read(component, ES8311_SDPOUT_REG0A);
	daciface = snd_soc_component_read(component, ES8311_SDPIN_REG09);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM: /* MASTER MODE */
		iface |= 0x40;
		break;
	case SND_SOC_DAIFMT_CBS_CFS: /* SLAVE MODE */
		iface &= 0xBF;
		break;
	default:
		return -EINVAL;
	}
	snd_soc_component_write(component, ES8311_RESET_REG00, iface);

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adciface &= 0xFC;
		daciface &= 0xFC;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adciface &= 0xFC;
		daciface &= 0xFC;
		adciface |= 0x01;
		daciface |= 0x01;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x03;
		daciface |= 0x03;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		adciface &= 0xDC;
		daciface &= 0xDC;
		adciface |= 0x23;
		daciface |= 0x23;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
	default:
		return -EINVAL;
	}

	iface = snd_soc_component_read(component, ES8311_CLK_MANAGER_REG06);
	/* clock inversion */
	if (((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_I2S) ||
	    ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_LEFT_J)) {

		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			iface    &= 0xDF;
			adciface &= 0xDF;
			daciface &= 0xDF;
			break;
		case SND_SOC_DAIFMT_IB_IF:
			iface    |= 0x20;
			adciface |= 0x20;
			daciface |= 0x20;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			iface    |= 0x20;
			adciface &= 0xDF;
			daciface &= 0xDF;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			iface    &= 0xDF;
			adciface |= 0x20;
			daciface |= 0x20;
			break;
		default:
			return -EINVAL;
		}
	}

	snd_soc_component_write(component, ES8311_CLK_MANAGER_REG06, iface);
	snd_soc_component_write(component, ES8311_SDPOUT_REG0A, adciface);
	snd_soc_component_write(component, ES8311_SDPIN_REG09, daciface);

	return 0;
}

static int es8311_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8311_priv *es8311 = snd_soc_component_get_drvdata(component);

	clk_prepare_enable(es8311->mclk_in);

	return 0;
}

static void es8311_pcm_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8311_priv *es8311 = snd_soc_component_get_drvdata(component);

	clk_disable_unprepare(es8311->mclk_in);
}

static int es8311_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u16 iface;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		iface = snd_soc_component_read(component, ES8311_SDPIN_REG09) & 0xE3;
		/* bit size */
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			iface |= 0x0c;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			iface |= 0x04;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			iface |= 0x10;
			break;
		}
		/* set iface */
		snd_soc_component_write(component, ES8311_SDPIN_REG09, iface);
	} else {
		iface = snd_soc_component_read(component, ES8311_SDPOUT_REG0A) & 0xE3;
		/* bit size */
		switch (params_format(params)) {
		case SNDRV_PCM_FORMAT_S16_LE:
			iface |= 0x0c;
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			iface |= 0x04;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			iface |= 0x10;
			break;
		}
		/* set iface */
		snd_soc_component_write(component, ES8311_SDPOUT_REG0A, iface);
	}

	return 0;
}

static int es8311_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct es8311_priv *es8311 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		if (IS_ERR(es8311->mclk_in))
			break;

		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_ON) {
			clk_disable_unprepare(es8311->mclk_in);
		} else {
			ret = clk_prepare_enable(es8311->mclk_in);
			if (ret)
				return ret;
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		break;
	}
	return 0;
}

static int es8311_set_tristate(struct snd_soc_dai *dai, int tristate)
{
	struct snd_soc_component *component = dai->component;

	if (tristate)
		snd_soc_component_update_bits(component, ES8311_CLK_MANAGER_REG07,
					      0x30, 0x30);
	else
		snd_soc_component_update_bits(component, ES8311_CLK_MANAGER_REG07,
					      0x30, 0x00);
	return 0;
}

static int es8311_mute(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	struct es8311_priv *es8311 = snd_soc_component_get_drvdata(component);

	if (mute) {
		snd_soc_component_write(component, ES8311_SYSTEM_REG12, 0x02);
		snd_soc_component_update_bits(component, ES8311_DAC_REG31, 0x60, 0x60);
		if (es8311->spk_ctl_gpio)
			gpiod_direction_output(es8311->spk_ctl_gpio, 0);
	} else {
		snd_soc_component_update_bits(component, ES8311_DAC_REG31, 0x60, 0x00);
		snd_soc_component_write(component, ES8311_SYSTEM_REG12, 0x00);
		if (es8311->spk_ctl_gpio) {
			gpiod_direction_output(es8311->spk_ctl_gpio, 1);
			if (es8311->delay_pa_drv_ms)
				msleep(es8311->delay_pa_drv_ms);
		}
	}
	return 0;
}

#define ES8311_RATES SNDRV_PCM_RATE_8000_96000
#define ES8311_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops es8311_ops = {
	.startup = es8311_pcm_startup,
	.shutdown = es8311_pcm_shutdown,
	.hw_params = es8311_pcm_hw_params,
	.set_fmt = es8311_set_dai_fmt,
	.mute_stream = es8311_mute,
	.set_tristate = es8311_set_tristate,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver es8311_dai = {
	.name = "ES8311 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ES8311_RATES,
		.formats = ES8311_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ES8311_RATES,
		.formats = ES8311_FORMATS,
	},
	.ops = &es8311_ops,
	.symmetric_rates = 1,
};

static int es8311_regs_init(struct snd_soc_component *component)
{
	/* reset codec */
	snd_soc_component_write(component, ES8311_I2C_REGFA, 0x01);
	msleep(20);
	snd_soc_component_write(component, ES8311_I2C_REGFA, 0x00);
	snd_soc_component_write(component, ES8311_RESET_REG00, 0x1F);
	snd_soc_component_write(component, ES8311_GP_REG45, 0x00);
	/* set ADC/DAC CLK */
	snd_soc_component_write(component, ES8311_CLK_MANAGER_REG01, 0x30);
	snd_soc_component_write(component, ES8311_CLK_MANAGER_REG02, 0x00);
	snd_soc_component_write(component, ES8311_CLK_MANAGER_REG03, 0x10);
	snd_soc_component_write(component, ES8311_CLK_MANAGER_REG04, 0x10);
	snd_soc_component_write(component, ES8311_CLK_MANAGER_REG05, 0x00);
	/* set system power up */
	snd_soc_component_write(component, ES8311_SYSTEM_REG0B, 0x00);
	snd_soc_component_write(component, ES8311_SYSTEM_REG0C, 0x00);
	snd_soc_component_write(component, ES8311_SYSTEM_REG10, 0x1F);
	snd_soc_component_write(component, ES8311_SYSTEM_REG11, 0x7F);
	/* chip powerup. slave mode */
	snd_soc_component_write(component, ES8311_RESET_REG00, 0x80);
	msleep(20);

	/* power up analog */
	snd_soc_component_write(component, ES8311_SYSTEM_REG0D, 0x01);
	/* power up digital */
	snd_soc_component_write(component, ES8311_CLK_MANAGER_REG01, 0x3F);
	/* set adc hpf, ADC_EQ bypass */
	snd_soc_component_write(component, ES8311_ADC_REG1C, 0x6A);
	/* ensure select Mic1p-Mic1n by default. */
	snd_soc_component_update_bits(component, ES8311_SYSTEM_REG14,
				      0x30, 0x10);

	return 0;
}

static int es8311_probe(struct snd_soc_component *component)
{
	struct es8311_priv *es8311 = snd_soc_component_get_drvdata(component);

	es8311->component = component;
	es8311_regs_init(component);

	/* Configure optional properties: */
	if (es8311->aec_mode)
		snd_soc_component_update_bits(component, ES8311_GPIO_REG44,
					      0x70, es8311->aec_mode << 4);
	if (es8311->adc_pga_gain)
		snd_soc_component_update_bits(component, ES8311_SYSTEM_REG14,
					      0x0f, es8311->adc_pga_gain);
	if (es8311->adc_volume)
		snd_soc_component_write(component, ES8311_ADC_REG17,
					es8311->adc_volume);
	if (es8311->dac_volume)
		snd_soc_component_write(component, ES8311_DAC_REG32,
					es8311->dac_volume);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_es8311 = {
	.probe			= es8311_probe,
	.set_bias_level		= es8311_set_bias_level,
	.controls		= es8311_snd_controls,
	.num_controls		= ARRAY_SIZE(es8311_snd_controls),
	.dapm_widgets		= es8311_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es8311_dapm_widgets),
	.dapm_routes		= es8311_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(es8311_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static struct regmap_config es8311_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = ES8311_MAX_REGISTER,
	.cache_type = REGCACHE_RBTREE,
};

static int es8311_parse_dt(struct i2c_client *client,
			   struct es8311_priv *es8311)
{
	struct device_node *np;
	const char *str;
	u32 v;
	int ret;

	np = client->dev.of_node;
	if (!np)
		return -EINVAL;

	es8311->delay_pa_drv_ms = 0;
	es8311->spk_ctl_gpio = devm_gpiod_get_optional(&client->dev, "spk-ctl",
			       GPIOD_OUT_LOW);
	if (!es8311->spk_ctl_gpio) {
		dev_info(&client->dev, "Don't need spk-ctl gpio\n");
	} else if (IS_ERR(es8311->spk_ctl_gpio)) {
		ret = PTR_ERR(es8311->spk_ctl_gpio);
		dev_err(&client->dev, "Unable to claim gpio spk-ctl\n");
		return ret;
	}
	ret = of_property_read_s32(np, "delay-pa-drv-ms",
				   &es8311->delay_pa_drv_ms);
	if (ret < 0 && ret != -EINVAL) {
		dev_err(&client->dev,
			"Failed to read 'rockchip,delay-pa-drv-ms': %d\n",
			ret);
		return ret;
	}

	es8311->adc_pga_gain = 0; /* ADC PGA Gain is 0dB by default reset. */
	if (!of_property_read_u32(np, "adc-pga-gain", &v)) {
		if (v >= 0 && v <= 10)
			es8311->adc_pga_gain = v;
		else
			dev_warn(&client->dev,
				 "adc-pga-gain (%d) is out of range\n", v);
	}

	es8311->adc_volume = 0; /* ADC Volume is -95dB by default reset. */
	if (!of_property_read_u32(np, "adc-volume", &v)) {
		if (v >= 0 && v <= 0xff)
			es8311->adc_volume = v;
		else
			dev_warn(&client->dev,
				 "adc-volume (0x%02x) is out of range\n", v);
	}

	es8311->dac_volume = 0; /* DAC Volume is -95dB by default reset. */
	if (!of_property_read_u32(np, "dac-volume", &v)) {
		if (v >= 0 && v <= 0xff)
			es8311->dac_volume = v;
		else
			dev_warn(&client->dev,
				 "dac-volume (0x%02x) is out of range\n", v);
	}

	es8311->aec_mode = 0; /* ADCDAT: 0 is ADC + ADC (default) */
	if (!of_property_read_string(np, "aec-mode", &str)) {
		int i;

		for (i = 0; i < ARRAY_SIZE(aec_type_txt); i++) {
			if (strcmp(str, aec_type_txt[i]) == 0) {
				es8311->aec_mode = i;
				break;
			}
		}
	}

	return 0;
}

static int es8311_i2c_probe(struct i2c_client *i2c_client,
			    const struct i2c_device_id *id)
{
	struct es8311_priv *es8311;
	struct regmap *regmap;
	int ret = 0;

	es8311 = devm_kzalloc(&i2c_client->dev,
			      sizeof(*es8311), GFP_KERNEL);
	if (es8311 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c_client, es8311);

	regmap = devm_regmap_init_i2c(i2c_client, &es8311_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	es8311->mclk_in = devm_clk_get(&i2c_client->dev, "mclk");
	if (IS_ERR(es8311->mclk_in))
		return PTR_ERR(es8311->mclk_in);

	ret = es8311_parse_dt(i2c_client, es8311);
	if (ret < 0) {
		dev_err(&i2c_client->dev, "Parse DT failed: %d\n", ret);
		return ret;
	}

	return devm_snd_soc_register_component(&i2c_client->dev,
					       &soc_component_dev_es8311,
					       &es8311_dai, 1);
}

static void es8311_i2c_shutdown(struct i2c_client *client)
{
	struct es8311_priv *es8311 = (struct es8311_priv *)i2c_get_clientdata(client);

	/* Need to reset anc clear all registers for reboot */
	snd_soc_component_write(es8311->component, ES8311_I2C_REGFA, 0x01);
}

static const struct i2c_device_id es8311_i2c_id[] = {
	{"es8311", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, es8311_i2c_id);

static const struct of_device_id es8311_of_match[] = {
	{ .compatible = "everest,es8311", },
	{},
};
MODULE_DEVICE_TABLE(of, es8311_of_match);

static struct i2c_driver es8311_i2c_driver = {
	.driver = {
		.name		= "es8311",
		.of_match_table	= of_match_ptr(es8311_of_match),
	},
	.probe		= es8311_i2c_probe,
	.shutdown	= es8311_i2c_shutdown,
	.id_table	= es8311_i2c_id,
};
module_i2c_driver(es8311_i2c_driver);

MODULE_DESCRIPTION("Everest Semi ES8311 ALSA SoC Codec Driver");
MODULE_AUTHOR("David Yang <yangxiaohua@everest-semi.com>");
MODULE_LICENSE("GPL");
