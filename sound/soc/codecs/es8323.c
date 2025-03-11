// SPDX-License-Identifier: GPL-2.0-only
//
// es8323.c -- es8323 ALSA SoC audio driver
//
// Copyright 2024 Rockchip Electronics Co. Ltd.
// Copyright 2024 Everest Semiconductor Co.,Ltd.
// Copyright 2024 Loongson Technology Co.,Ltd.
//
// Author: Mark Brown <broonie@kernel.org>
//         Jianqun Xu <jay.xu@rock-chips.com>
//         Nickey Yang <nickey.yang@rock-chips.com>
// Further cleanup and restructuring by:
//         Binbin Zhou <zhoubinbin@loongson.cn>

#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "es8323.h"

struct es8323_priv {
	unsigned int sysclk;
	struct clk *mclk;
	struct regmap *regmap;
	struct snd_pcm_hw_constraint_list *sysclk_constraints;
	struct snd_soc_component *component;
};

/* es8323 register cache */
static const struct reg_default es8323_reg_defaults[] = {
	{ ES8323_CONTROL1,     0x06 },
	{ ES8323_CONTROL2,     0x1c },
	{ ES8323_CHIPPOWER,    0xc3 },
	{ ES8323_ADCPOWER,     0xfc },
	{ ES8323_DACPOWER,     0xc0 },
	{ ES8323_CHIPLOPOW1,   0x00 },
	{ ES8323_CHIPLOPOW2,   0x00 },
	{ ES8323_ANAVOLMANAG,  0x7c },
	{ ES8323_MASTERMODE,   0x80 },
	{ ES8323_ADCCONTROL1,  0x00 },
	{ ES8323_ADCCONTROL2,  0x00 },
	{ ES8323_ADCCONTROL3,  0x06 },
	{ ES8323_ADCCONTROL4,  0x00 },
	{ ES8323_ADCCONTROL5,  0x06 },
	{ ES8323_ADCCONTROL6,  0x30 },
	{ ES8323_ADC_MUTE,     0x30 },
	{ ES8323_LADC_VOL,     0xc0 },
	{ ES8323_RADC_VOL,     0xc0 },
	{ ES8323_ADCCONTROL10, 0x38 },
	{ ES8323_ADCCONTROL11, 0xb0 },
	{ ES8323_ADCCONTROL12, 0x32 },
	{ ES8323_ADCCONTROL13, 0x06 },
	{ ES8323_ADCCONTROL14, 0x00 },
	{ ES8323_DACCONTROL1,  0x00 },
	{ ES8323_DACCONTROL2,  0x06 },
	{ ES8323_DAC_MUTE,     0x30 },
	{ ES8323_LDAC_VOL,     0xc0 },
	{ ES8323_RDAC_VOL,     0xc0 },
	{ ES8323_DACCONTROL6,  0x08 },
	{ ES8323_DACCONTROL7,  0x06 },
	{ ES8323_DACCONTROL8,  0x1f },
	{ ES8323_DACCONTROL9,  0xf7 },
	{ ES8323_DACCONTROL10, 0xfd },
	{ ES8323_DACCONTROL11, 0xff },
	{ ES8323_DACCONTROL12, 0x1f },
	{ ES8323_DACCONTROL13, 0xf7 },
	{ ES8323_DACCONTROL14, 0xfd },
	{ ES8323_DACCONTROL15, 0xff },
	{ ES8323_DACCONTROL16, 0x00 },
	{ ES8323_DACCONTROL17, 0x38 },
	{ ES8323_DACCONTROL18, 0x38 },
	{ ES8323_DACCONTROL19, 0x38 },
	{ ES8323_DACCONTROL20, 0x38 },
	{ ES8323_DACCONTROL21, 0x38 },
	{ ES8323_DACCONTROL22, 0x38 },
	{ ES8323_DACCONTROL23, 0x00 },
	{ ES8323_LOUT1_VOL,    0x00 },
	{ ES8323_ROUT1_VOL,    0x00 },
};

static const char *const es8323_stereo_3d_texts[] = { "No 3D  ", "Level 1", "Level 2", "Level 3",
						      "Level 4", "Level 5", "Level 6", "Level 7" };
static SOC_ENUM_SINGLE_DECL(es8323_stereo_3d_enum, ES8323_DACCONTROL7, 2, es8323_stereo_3d_texts);

static const char *const es8323_alc_func_texts[] = { "Off", "Right", "Left", "Stereo" };
static SOC_ENUM_SINGLE_DECL(es8323_alc_function_enum,
			    ES8323_ADCCONTROL10, 6, es8323_alc_func_texts);

static const char *const es8323_ng_type_texts[] = { "Constant PGA Gain", "Mute ADC Output" };
static SOC_ENUM_SINGLE_DECL(es8323_alc_ng_type_enum, ES8323_ADCCONTROL14, 1, es8323_ng_type_texts);

static const char *const es8323_deemph_texts[] = { "None", "32Khz", "44.1Khz", "48Khz" };
static SOC_ENUM_SINGLE_DECL(es8323_playback_deemphasis_enum,
			    ES8323_DACCONTROL6, 6, es8323_deemph_texts);

static const char *const es8323_adcpol_texts[] = { "Normal", "L Invert",
						   "R Invert", "L + R Invert" };
static SOC_ENUM_SINGLE_DECL(es8323_capture_polarity_enum,
			    ES8323_ADCCONTROL6, 6, es8323_adcpol_texts);

static const DECLARE_TLV_DB_SCALE(es8323_adc_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(es8323_dac_tlv, -9600, 50, 1);
static const DECLARE_TLV_DB_SCALE(es8323_out_tlv, -4500, 150, 0);
static const DECLARE_TLV_DB_SCALE(es8323_bypass_tlv, 0, 300, 0);
static const DECLARE_TLV_DB_SCALE(es8323_bypass_tlv2, -15, 300, 0);

static const struct snd_kcontrol_new es8323_snd_controls[] = {
	SOC_ENUM("3D Mode", es8323_stereo_3d_enum),
	SOC_ENUM("ALC Capture Function", es8323_alc_function_enum),
	SOC_ENUM("ALC Capture NG Type", es8323_alc_ng_type_enum),
	SOC_ENUM("Playback De-emphasis", es8323_playback_deemphasis_enum),
	SOC_ENUM("Capture Polarity", es8323_capture_polarity_enum),
	SOC_SINGLE("ALC Capture ZC Switch", ES8323_ADCCONTROL13, 6, 1, 0),
	SOC_SINGLE("ALC Capture Decay Time", ES8323_ADCCONTROL12, 4, 15, 0),
	SOC_SINGLE("ALC Capture Attack Time", ES8323_ADCCONTROL12, 0, 15, 0),
	SOC_SINGLE("ALC Capture NG Threshold", ES8323_ADCCONTROL14, 3, 31, 0),
	SOC_SINGLE("ALC Capture NG Switch", ES8323_ADCCONTROL14, 0, 1, 0),
	SOC_SINGLE("ZC Timeout Switch", ES8323_ADCCONTROL13, 6, 1, 0),
	SOC_SINGLE("Capture Mute Switch", ES8323_ADC_MUTE, 2, 1, 0),
	SOC_SINGLE_TLV("Left Channel Capture Volume", ES8323_ADCCONTROL1, 4, 8,
		       0, es8323_bypass_tlv),
	SOC_SINGLE_TLV("Right Channel Capture Volume", ES8323_ADCCONTROL1, 0,
		       8, 0, es8323_bypass_tlv),
	SOC_SINGLE_TLV("Left Mixer Left Bypass Volume", ES8323_DACCONTROL17, 3,
		       7, 1, es8323_bypass_tlv2),
	SOC_SINGLE_TLV("Right Mixer Right Bypass Volume", ES8323_DACCONTROL20,
		       3, 7, 1, es8323_bypass_tlv2),
	SOC_DOUBLE_R_TLV("PCM Volume", ES8323_LDAC_VOL, ES8323_RDAC_VOL,
			 0, 192, 1, es8323_dac_tlv),
	SOC_DOUBLE_R_TLV("Capture Digital Volume", ES8323_LADC_VOL,
			 ES8323_RADC_VOL, 0, 192, 1, es8323_adc_tlv),
	SOC_DOUBLE_R_TLV("Output 1 Playback Volume", ES8323_LOUT1_VOL,
			 ES8323_ROUT1_VOL, 0, 33, 0, es8323_out_tlv),
	SOC_DOUBLE_R_TLV("Output 2 Playback Volume", ES8323_LOUT2_VOL,
			 ES8323_ROUT2_VOL, 0, 33, 0, es8323_out_tlv),
};

/* Left DAC Route */
static const char *const es8323_pga_sell[] = { "Line 1L", "Line 2L", "NC", "DifferentialL" };
static SOC_ENUM_SINGLE_DECL(es8323_left_dac_enum, ES8323_ADCCONTROL2, 6, es8323_pga_sell);
static const struct snd_kcontrol_new es8323_left_dac_mux_controls =
	SOC_DAPM_ENUM("Left DAC Route", es8323_left_dac_enum);

/* Right DAC Route */
static const char *const es8323_pga_selr[] = { "Line 1R", "Line 2R", "NC", "DifferentialR" };
static SOC_ENUM_SINGLE_DECL(es8323_right_dac_enum, ES8323_ADCCONTROL2, 4, es8323_pga_selr);
static const struct snd_kcontrol_new es8323_right_dac_mux_controls =
	SOC_DAPM_ENUM("Right DAC Route", es8323_right_dac_enum);

/* Left Line Mux */
static const char *const es8323_lin_sell[] = { "Line 1L", "Line 2L", "NC", "MicL" };
static SOC_ENUM_SINGLE_DECL(es8323_llin_enum, ES8323_DACCONTROL16, 3, es8323_lin_sell);
static const struct snd_kcontrol_new es8323_left_line_controls =
	SOC_DAPM_ENUM("LLIN Mux", es8323_llin_enum);

/* Right Line Mux */
static const char *const es8323_lin_selr[] = { "Line 1R", "Line 2R", "NC", "MicR" };
static SOC_ENUM_SINGLE_DECL(es8323_rlin_enum, ES8323_DACCONTROL16, 0, es8323_lin_selr);
static const struct snd_kcontrol_new es8323_right_line_controls =
	SOC_DAPM_ENUM("RLIN Mux", es8323_rlin_enum);

/* Differential Mux */
static const char *const es8323_diffmux_sel[] = { "Line 1", "Line 2" };
static SOC_ENUM_SINGLE_DECL(es8323_diffmux_enum, ES8323_ADCCONTROL3, 7, es8323_diffmux_sel);
static const struct snd_kcontrol_new es8323_diffmux_controls =
	SOC_DAPM_ENUM("Route2", es8323_diffmux_enum);

/* Mono ADC Mux */
static const char *const es8323_mono_adc_mux[] = { "Stereo", "Mono (Left)", "Mono (Right)" };
static SOC_ENUM_SINGLE_DECL(es8323_mono_adc_mux_enum, ES8323_ADCCONTROL3, 3, es8323_mono_adc_mux);
static const struct snd_kcontrol_new es8323_mono_adc_mux_controls =
	SOC_DAPM_ENUM("Mono Mux", es8323_mono_adc_mux_enum);

/* Left Mixer */
static const struct snd_kcontrol_new es8323_left_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left Playback Switch", SND_SOC_NOPM, 7, 1, 1),
	SOC_DAPM_SINGLE("Left Bypass Switch", ES8323_DACCONTROL17, 6, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new es8323_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Right Playback Switch", SND_SOC_NOPM, 6, 1, 1),
	SOC_DAPM_SINGLE("Right Bypass Switch", ES8323_DACCONTROL20, 6, 1, 0),
};

static const struct snd_soc_dapm_widget es8323_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),

	SND_SOC_DAPM_MICBIAS("Mic Bias", SND_SOC_NOPM, 3, 1),

	/* Muxes */
	SND_SOC_DAPM_MUX("Left PGA Mux", SND_SOC_NOPM, 0, 0, &es8323_left_dac_mux_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", SND_SOC_NOPM, 0, 0, &es8323_right_dac_mux_controls),
	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0, &es8323_diffmux_controls),
	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0, &es8323_mono_adc_mux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0, &es8323_mono_adc_mux_controls),
	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0, &es8323_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0, &es8323_right_line_controls),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", SND_SOC_NOPM, 4, 1),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", SND_SOC_NOPM, 5, 1),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", SND_SOC_NOPM, 6, 1),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", SND_SOC_NOPM, 7, 1),

	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
			   &es8323_left_mixer_controls[0],
			   ARRAY_SIZE(es8323_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
			   &es8323_right_mixer_controls[0],
			   ARRAY_SIZE(es8323_right_mixer_controls)),

	SND_SOC_DAPM_PGA("Right ADC Power", SND_SOC_NOPM, 6, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Left ADC Power", SND_SOC_NOPM, 7, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 2", SND_SOC_NOPM, 2, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", SND_SOC_NOPM, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", SND_SOC_NOPM, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", SND_SOC_NOPM, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LAMP", ES8323_ADCCONTROL1, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RAMP", ES8323_ADCCONTROL1, 0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("VREF"),
};

static const struct snd_soc_dapm_route es8323_dapm_routes[] = {
	/*12.22*/
	{"Left PGA Mux", "Line 1L", "LINPUT1"},
	{"Left PGA Mux", "Line 2L", "LINPUT2"},
	{"Left PGA Mux", "DifferentialL", "Differential Mux"},

	{"Right PGA Mux", "Line 1R", "RINPUT1"},
	{"Right PGA Mux", "Line 2R", "RINPUT2"},
	{"Right PGA Mux", "DifferentialR", "Differential Mux"},

	{"Differential Mux", "Line 1", "LINPUT1"},
	{"Differential Mux", "Line 1", "RINPUT1"},
	{"Differential Mux", "Line 2", "LINPUT2"},
	{"Differential Mux", "Line 2", "RINPUT2"},

	{"Left ADC Mux", "Stereo", "Right PGA Mux"},
	{"Left ADC Mux", "Stereo", "Left PGA Mux"},
	{"Left ADC Mux", "Mono (Left)", "Left PGA Mux"},

	{"Right ADC Mux", "Stereo", "Left PGA Mux"},
	{"Right ADC Mux", "Stereo", "Right PGA Mux"},
	{"Right ADC Mux", "Mono (Right)", "Right PGA Mux"},

	{"Left ADC Power", NULL, "Left ADC Mux"},
	{"Right ADC Power", NULL, "Right ADC Mux"},
	{"Left ADC", NULL, "Left ADC Power"},
	{"Right ADC", NULL, "Right ADC Power"},

	{"Left Line Mux", "Line 1L", "LINPUT1"},
	{"Left Line Mux", "Line 2L", "LINPUT2"},
	{"Left Line Mux", "MicL", "Left PGA Mux"},

	{"Right Line Mux", "Line 1R", "RINPUT1"},
	{"Right Line Mux", "Line 2R", "RINPUT2"},
	{"Right Line Mux", "MicR", "Right PGA Mux"},

	{"Left Mixer", "Left Playback Switch", "Left DAC"},
	{"Left Mixer", "Left Bypass Switch", "Left Line Mux"},

	{"Right Mixer", "Right Playback Switch", "Right DAC"},
	{"Right Mixer", "Right Bypass Switch", "Right Line Mux"},

	{"Left Out 1", NULL, "Left Mixer"},
	{"LOUT1", NULL, "Left Out 1"},
	{"Right Out 1", NULL, "Right Mixer"},
	{"ROUT1", NULL, "Right Out 1"},

	{"Left Out 2", NULL, "Left Mixer"},
	{"LOUT2", NULL, "Left Out 2"},
	{"Right Out 2", NULL, "Right Mixer"},
	{"ROUT2", NULL, "Right Out 2"},
};

struct coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:4;
	u8 usb:1;
};

/* codec hifi mclk clock divider coefficients */
static const struct coeff_div es8323_coeff_div[] = {
	/* 8k */
	{12288000, 8000, 1536, 0xa, 0x0},
	{11289600, 8000, 1408, 0x9, 0x0},
	{18432000, 8000, 2304, 0xc, 0x0},
	{16934400, 8000, 2112, 0xb, 0x0},
	{12000000, 8000, 1500, 0xb, 0x1},

	/* 11.025k */
	{11289600, 11025, 1024, 0x7, 0x0},
	{16934400, 11025, 1536, 0xa, 0x0},
	{12000000, 11025, 1088, 0x9, 0x1},

	/* 16k */
	{12288000, 16000, 768, 0x6, 0x0},
	{18432000, 16000, 1152, 0x8, 0x0},
	{12000000, 16000, 750, 0x7, 0x1},

	/* 22.05k */
	{11289600, 22050, 512, 0x4, 0x0},
	{16934400, 22050, 768, 0x6, 0x0},
	{12000000, 22050, 544, 0x6, 0x1},

	/* 32k */
	{12288000, 32000, 384, 0x3, 0x0},
	{18432000, 32000, 576, 0x5, 0x0},
	{12000000, 32000, 375, 0x4, 0x1},

	/* 44.1k */
	{11289600, 44100, 256, 0x2, 0x0},
	{16934400, 44100, 384, 0x3, 0x0},
	{12000000, 44100, 272, 0x3, 0x1},

	/* 48k */
	{12288000, 48000, 256, 0x2, 0x0},
	{18432000, 48000, 384, 0x3, 0x0},
	{12000000, 48000, 250, 0x2, 0x1},

	/* 88.2k */
	{11289600, 88200, 128, 0x0, 0x0},
	{16934400, 88200, 192, 0x1, 0x0},
	{12000000, 88200, 136, 0x1, 0x1},

	/* 96k */
	{12288000, 96000, 128, 0x0, 0x0},
	{18432000, 96000, 192, 0x1, 0x0},
	{12000000, 96000, 125, 0x0, 0x1},
};

static unsigned int rates_12288[] = {
	8000, 12000, 16000, 24000, 24000, 32000, 48000, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count = ARRAY_SIZE(rates_12288),
	.list = rates_12288,
};

static unsigned int rates_112896[] = {
	8000, 11025, 22050, 44100,
};

static struct snd_pcm_hw_constraint_list constraints_112896 = {
	.count = ARRAY_SIZE(rates_112896),
	.list = rates_112896,
};

static unsigned int rates_12[] = {
	8000, 11025, 12000, 16000, 22050, 24000,
	32000, 44100, 48000, 48000, 88235, 96000,
};

static struct snd_pcm_hw_constraint_list constraints_12 = {
	.count = ARRAY_SIZE(rates_12),
	.list = rates_12,
};

static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(es8323_coeff_div); i++) {
		if (es8323_coeff_div[i].rate == rate &&
		    es8323_coeff_div[i].mclk == mclk)
			return i;
	}

	return -EINVAL;
}

static int es8323_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);

	switch (freq) {
	case 11289600:
	case 18432000:
	case 22579200:
	case 36864000:
		es8323->sysclk_constraints = &constraints_112896;
		break;
	case 12288000:
	case 16934400:
	case 24576000:
	case 33868800:
		es8323->sysclk_constraints = &constraints_12288;
		break;
	case 12000000:
	case 24000000:
		es8323->sysclk_constraints = &constraints_12;
		break;
	default:
		return -EINVAL;
	}

	es8323->sysclk = freq;
	return 0;
}

static int es8323_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u8 iface = snd_soc_component_read(component, ES8323_MASTERMODE);
	u8 adciface = snd_soc_component_read(component, ES8323_ADC_IFACE);
	u8 daciface = snd_soc_component_read(component, ES8323_DAC_IFACE);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_BC_FP:
		iface |= 0x80;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		iface &= 0x7f;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		adciface &= 0xfc;
		daciface &= 0xf8;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		adciface &= 0xfd;
		daciface &= 0xf9;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		adciface &= 0xfe;
		daciface &= 0xfa;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		adciface &= 0xff;
		daciface &= 0xfb;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		iface &= 0xdf;
		adciface &= 0xdf;
		daciface &= 0xbf;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x20;
		adciface |= 0x20;
		daciface |= 0x40;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x20;
		adciface &= 0xdf;
		daciface &= 0xbf;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface &= 0xdf;
		adciface |= 0x20;
		daciface |= 0x40;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, ES8323_MASTERMODE, iface);
	snd_soc_component_write(component, ES8323_ADC_IFACE, adciface);
	snd_soc_component_write(component, ES8323_DAC_IFACE, daciface);

	return 0;
}

static int es8323_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);

	if (es8323->sysclk) {
		snd_pcm_hw_constraint_list(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_RATE,
					   es8323->sysclk_constraints);
	}

	return 0;
}

static int es8323_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);
	u16 srate = snd_soc_component_read(component, ES8323_MASTERMODE) & 0x80;
	u16 adciface = snd_soc_component_read(component, ES8323_ADC_IFACE) & 0xe3;
	u16 daciface = snd_soc_component_read(component, ES8323_DAC_IFACE) & 0xc7;
	int coeff;

	coeff = get_coeff(es8323->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(es8323->sysclk / 2, params_rate(params));
		srate |= 0x40;
	}

	if (coeff < 0) {
		dev_err(component->dev,
			"Unable to configure sample rate %dHz with %dHz MCLK\n",
			params_rate(params), es8323->sysclk);
		return coeff;
	}

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		adciface |= 0xc;
		daciface |= 0x18;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		adciface |= 0x4;
		daciface |= 0x8;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		adciface |= 0x10;
		daciface |= 0x20;
		break;
	}

	snd_soc_component_write(component, ES8323_DAC_IFACE, daciface);
	snd_soc_component_write(component, ES8323_ADC_IFACE, adciface);

	snd_soc_component_write(component, ES8323_MASTERMODE, srate);
	snd_soc_component_write(component, ES8323_ADCCONTROL5,
				es8323_coeff_div[coeff].sr |
				(es8323_coeff_div[coeff].usb) << 4);
	snd_soc_component_write(component, ES8323_DACCONTROL2,
				es8323_coeff_div[coeff].sr |
				(es8323_coeff_div[coeff].usb) << 4);

	snd_soc_component_write(component, ES8323_DACPOWER, 0x3c);

	return 0;
}

static int es8323_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	struct snd_soc_component *component = dai->component;
	u32 val = mute ? 0x6 : 0x2;

	snd_soc_component_write(component, ES8323_DAC_MUTE, val);

	return 0;
}

static const struct snd_soc_dai_ops es8323_ops = {
	.startup	= es8323_pcm_startup,
	.hw_params	= es8323_pcm_hw_params,
	.set_fmt	= es8323_set_dai_fmt,
	.set_sysclk	= es8323_set_dai_sysclk,
	.mute_stream	= es8323_mute_stream,
};

#define ES8323_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver es8323_dai = {
	.name = "ES8323 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = ES8323_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = ES8323_FORMATS,
	},
	.ops = &es8323_ops,
	.symmetric_rate = 1,
};

static int es8323_probe(struct snd_soc_component *component)
{
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);
	int ret;

	es8323->component = component;

	es8323->mclk = devm_clk_get_optional(component->dev, "mclk");
	if (IS_ERR(es8323->mclk)) {
		dev_err(component->dev, "unable to get mclk\n");
		return PTR_ERR(es8323->mclk);
	}

	if (!es8323->mclk)
		dev_warn(component->dev, "assuming static mclk\n");

	ret = clk_prepare_enable(es8323->mclk);
	if (ret) {
		dev_err(component->dev, "unable to enable mclk\n");
		return ret;
	}

	snd_soc_component_write(component, ES8323_CONTROL2, 0x60);
	snd_soc_component_write(component, ES8323_CHIPPOWER, 0x00);
	snd_soc_component_write(component, ES8323_DACCONTROL17, 0xB8);

	return 0;
}

static int es8323_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		ret = clk_prepare_enable(es8323->mclk);
		if (ret)
			return ret;

		snd_soc_component_write(component, ES8323_CHIPPOWER, 0xf0);
		usleep_range(18000, 20000);
		snd_soc_component_write(component, ES8323_DACPOWER, 0x3c);
		snd_soc_component_write(component, ES8323_ANAVOLMANAG, 0x7c);
		snd_soc_component_write(component, ES8323_CHIPLOPOW1, 0x00);
		snd_soc_component_write(component, ES8323_CHIPLOPOW2, 0x00);
		snd_soc_component_write(component, ES8323_CHIPPOWER, 0x00);
		snd_soc_component_write(component, ES8323_ADCPOWER, 0x09);
		snd_soc_component_write(component, ES8323_ADCCONTROL14, 0x00);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_component_write(component, ES8323_ANAVOLMANAG, 0x7c);
		snd_soc_component_write(component, ES8323_CHIPLOPOW1, 0x00);
		snd_soc_component_write(component, ES8323_CHIPLOPOW2, 0x00);
		snd_soc_component_write(component, ES8323_CHIPPOWER, 0x00);
		snd_soc_component_write(component, ES8323_ADCPOWER, 0x59);
		break;
	case SND_SOC_BIAS_OFF:
		clk_disable_unprepare(es8323->mclk);
		snd_soc_component_write(component, ES8323_ADCPOWER, 0xff);
		snd_soc_component_write(component, ES8323_DACPOWER, 0xC0);
		snd_soc_component_write(component, ES8323_CHIPLOPOW1, 0xff);
		snd_soc_component_write(component, ES8323_CHIPLOPOW2, 0xff);
		snd_soc_component_write(component, ES8323_CHIPPOWER, 0xff);
		snd_soc_component_write(component, ES8323_ANAVOLMANAG, 0x7b);
		break;
	}

	return 0;
}

static void es8323_remove(struct snd_soc_component *component)
{
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);

	clk_disable_unprepare(es8323->mclk);
	es8323_set_bias_level(component, SND_SOC_BIAS_OFF);
}

static int es8323_suspend(struct snd_soc_component *component)
{
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(es8323->regmap, true);
	regcache_mark_dirty(es8323->regmap);

	return 0;
}

static int es8323_resume(struct snd_soc_component *component)
{
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(es8323->regmap, false);
	regcache_sync(es8323->regmap);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_es8323 = {
	.probe			= es8323_probe,
	.remove			= es8323_remove,
	.suspend		= es8323_suspend,
	.resume			= es8323_resume,
	.set_bias_level		= es8323_set_bias_level,
	.controls		= es8323_snd_controls,
	.num_controls		= ARRAY_SIZE(es8323_snd_controls),
	.dapm_widgets		= es8323_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es8323_dapm_widgets),
	.dapm_routes		= es8323_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(es8323_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config es8323_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.use_single_read	= true,
	.use_single_write	= true,
	.max_register		= 0x53,
	.reg_defaults		= es8323_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(es8323_reg_defaults),
	.cache_type		= REGCACHE_MAPLE,
};

static int es8323_i2c_probe(struct i2c_client *i2c_client)
{
	struct es8323_priv *es8323;
	struct device *dev = &i2c_client->dev;

	es8323 = devm_kzalloc(dev, sizeof(*es8323), GFP_KERNEL);
	if (!es8323)
		return -ENOMEM;

	i2c_set_clientdata(i2c_client, es8323);

	es8323->regmap = devm_regmap_init_i2c(i2c_client, &es8323_regmap);
	if (IS_ERR(es8323->regmap))
		return PTR_ERR(es8323->regmap);

	return devm_snd_soc_register_component(dev,
					       &soc_component_dev_es8323,
					       &es8323_dai, 1);
}

static const struct i2c_device_id es8323_i2c_id[] = {
	{ "es8323", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es8323_i2c_id);

static const struct acpi_device_id es8323_acpi_match[] = {
	{ "ESSX8323", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, es8323_acpi_match);

static const struct of_device_id es8323_of_match[] = {
	{ .compatible = "everest,es8323" },
	{ }
};
MODULE_DEVICE_TABLE(of, es8323_of_match);

static struct i2c_driver es8323_i2c_driver = {
	.driver = {
		.name = "ES8323",
		.acpi_match_table = es8323_acpi_match,
		.of_match_table = es8323_of_match,
	},
	.probe = es8323_i2c_probe,
	.id_table = es8323_i2c_id,
};
module_i2c_driver(es8323_i2c_driver);

MODULE_DESCRIPTION("Everest Semi ES8323 ALSA SoC Codec Driver");
MODULE_AUTHOR("Mark Brown <broonie@kernel.org>");
MODULE_AUTHOR("Binbin Zhou <zhoubinbin@loongson.cn>");
MODULE_LICENSE("GPL");
