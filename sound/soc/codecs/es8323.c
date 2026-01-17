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

#include <linux/bitfield.h>
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
	{ ES8323_ADCCONTROL7,  0x30 },
	{ ES8323_LADC_VOL,     0xc0 },
	{ ES8323_RADC_VOL,     0xc0 },
	{ ES8323_ADCCONTROL10, 0x38 },
	{ ES8323_ADCCONTROL11, 0xb0 },
	{ ES8323_ADCCONTROL12, 0x32 },
	{ ES8323_ADCCONTROL13, 0x06 },
	{ ES8323_ADCCONTROL14, 0x00 },
	{ ES8323_DACCONTROL1,  0x00 },
	{ ES8323_DACCONTROL2,  0x06 },
	{ ES8323_DACCONTROL3,  0x30 },
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

	SOC_SINGLE("ALC Capture ZC Switch", ES8323_ADCCONTROL13,
		   ES8323_ADCCONTROL13_ALCZC_OFF, 1, 0),
	SOC_SINGLE("ALC Capture Decay Time", ES8323_ADCCONTROL12,
		   ES8323_ADCCONTROL12_ALCDCY_OFF, 15, 0),
	SOC_SINGLE("ALC Capture Attack Time", ES8323_ADCCONTROL12,
		   ES8323_ADCCONTROL12_ALCATK_OFF, 15, 0),
	SOC_SINGLE("ALC Capture NG Threshold", ES8323_ADCCONTROL14,
		   ES8323_ADCCONTROL14_NGTH_OFF, 31, 0),
	SOC_SINGLE("ALC Capture NG Switch", ES8323_ADCCONTROL14,
		   ES8323_ADCCONTROL14_NGAT_OFF, 1, 0),
	SOC_SINGLE("ZC Timeout Switch", ES8323_ADCCONTROL13,
		   ES8323_ADCCONTROL13_TIMEOUT_OFF, 1, 0),
	SOC_SINGLE("Capture Mute Switch", ES8323_ADCCONTROL7,
		   ES8323_ADCCONTROL7_ADCMUTE_OFF, 1, 0),

	SOC_SINGLE_TLV("Left Channel Capture Volume", ES8323_ADCCONTROL1,
		       ES8323_ADCCONTROL1_MICAMPL_OFF, 8, 0, es8323_bypass_tlv),
	SOC_SINGLE_TLV("Right Channel Capture Volume", ES8323_ADCCONTROL1,
		       ES8323_ADCCONTROL1_MICAMPR_OFF, 8, 0, es8323_bypass_tlv),
	SOC_SINGLE_TLV("Left Mixer Left Bypass Volume", ES8323_DACCONTROL17,
		       ES8323_DACCONTROL17_LI2LOVOL_OFF, 7, 1, es8323_bypass_tlv2),
	SOC_SINGLE_TLV("Right Mixer Right Bypass Volume", ES8323_DACCONTROL20,
		       ES8323_DACCONTROL20_RI2ROVOL_OFF, 7, 1, es8323_bypass_tlv2),

	SOC_DOUBLE_R_TLV("PCM Volume",
			 ES8323_LDAC_VOL, ES8323_RDAC_VOL,
			 0, 192, 1, es8323_dac_tlv),
	SOC_DOUBLE_R_TLV("Capture Digital Volume",
			 ES8323_LADC_VOL, ES8323_RADC_VOL,
			 0, 192, 1, es8323_adc_tlv),
	SOC_DOUBLE_R_TLV("Output 1 Playback Volume",
			 ES8323_LOUT1_VOL, ES8323_ROUT1_VOL,
			 0, 33, 0, es8323_out_tlv),
	SOC_DOUBLE_R_TLV("Output 2 Playback Volume",
			 ES8323_LOUT2_VOL, ES8323_ROUT2_VOL,
			 0, 33, 0, es8323_out_tlv),
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
	SOC_DAPM_SINGLE("Left Playback Switch", ES8323_DACCONTROL17, 7, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", ES8323_DACCONTROL17, 6, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new es8323_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Right Playback Switch", ES8323_DACCONTROL20, 7, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", ES8323_DACCONTROL20, 6, 1, 0),
};

static const struct snd_soc_dapm_widget es8323_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),

	SND_SOC_DAPM_SUPPLY("Mic Bias", ES8323_ADCPOWER,
			    ES8323_ADCPOWER_PDNMICB_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Mic Bias Gen", ES8323_ADCPOWER,
			    ES8323_ADCPOWER_PDNADCBIS_OFF, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DAC STM", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_DACSTM_RESET, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC STM", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_ADCSTM_RESET, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC DIG", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_DACDIG_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC DIG", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_ADCDIG_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC DLL", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_DACDLL_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC DLL", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_ADCDLL_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC Vref", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_ADCVREF_OFF, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC Vref", ES8323_CHIPPOWER,
			    ES8323_CHIPPOWER_DACVREF_OFF, 1, NULL, 0),

	/* Muxes */
	SND_SOC_DAPM_MUX("Left PGA Mux", ES8323_ADCPOWER,
			 ES8323_ADCPOWER_PDNAINL_OFF, 1, &es8323_left_dac_mux_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", ES8323_ADCPOWER,
			 ES8323_ADCPOWER_PDNAINR_OFF, 1, &es8323_right_dac_mux_controls),

	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0, &es8323_diffmux_controls),

	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0, &es8323_mono_adc_mux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0, &es8323_mono_adc_mux_controls),

	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0, &es8323_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0, &es8323_right_line_controls),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture",
			 ES8323_ADCPOWER, ES8323_ADCPOWER_PDNADCR_OFF, 1),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture",
			 ES8323_ADCPOWER, ES8323_ADCPOWER_PDNADCL_OFF, 1),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback",
			 ES8323_DACPOWER, ES8323_DACPOWER_PDNDACR_OFF, 1),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback",
			 ES8323_DACPOWER, ES8323_DACPOWER_PDNDACL_OFF, 1),

	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
			   &es8323_left_mixer_controls[0],
			   ARRAY_SIZE(es8323_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
			   &es8323_right_mixer_controls[0],
			   ARRAY_SIZE(es8323_right_mixer_controls)),

	SND_SOC_DAPM_PGA("Right Out 2", ES8323_DACPOWER, ES8323_DACPOWER_ROUT2_OFF, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", ES8323_DACPOWER, ES8323_DACPOWER_LOUT2_OFF, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", ES8323_DACPOWER, ES8323_DACPOWER_ROUT1_OFF, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", ES8323_DACPOWER, ES8323_DACPOWER_LOUT1_OFF, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LAMP", ES8323_ADCCONTROL1, ES8323_ADCCONTROL1_MICAMPL_OFF, 0, NULL, 0),
	SND_SOC_DAPM_PGA("RAMP", ES8323_ADCCONTROL1, ES8323_ADCCONTROL1_MICAMPR_OFF, 0, NULL, 0),

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

	{"Left ADC Mux", "Stereo", "Left PGA Mux"},
	{"Left ADC Mux", "Mono (Left)", "Left PGA Mux"},

	{"Right ADC Mux", "Stereo", "Right PGA Mux"},
	{"Right ADC Mux", "Mono (Right)", "Right PGA Mux"},

	{"Left ADC", NULL, "Left ADC Mux"},
	{"Right ADC", NULL, "Right ADC Mux"},

	{ "Mic Bias", NULL, "Mic Bias Gen" },

	{ "ADC DIG", NULL, "ADC STM" },
	{ "ADC DIG", NULL, "ADC Vref" },
	{ "ADC DIG", NULL, "ADC DLL" },

	{ "Left ADC", NULL, "ADC DIG" },
	{ "Right ADC", NULL, "ADC DIG" },

	{ "DAC DIG", NULL, "DAC STM" },
	{ "DAC DIG", NULL, "DAC Vref" },
	{ "DAC DIG", NULL, "DAC DLL" },

	{ "Left DAC", NULL, "DAC DIG" },
	{ "Right DAC", NULL, "DAC DIG" },

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
	u8 format_mode, inv_mode;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_BC_FP:
		/* Master serial port mode */
		snd_soc_component_update_bits(component, ES8323_MASTERMODE,
					      ES8323_MASTERMODE_MSC, ES8323_MASTERMODE_MSC);
		break;
	case SND_SOC_DAIFMT_BC_FC:
		/* Slave serial port mode */
		snd_soc_component_update_bits(component, ES8323_MASTERMODE,
					      ES8323_MASTERMODE_MSC, 0);
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format_mode = ES8323_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format_mode = ES8323_FMT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		format_mode = ES8323_FMT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		format_mode = ES8323_FMT_DSP;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write_field(component, ES8323_ADCCONTROL4,
				      ES8323_ADCCONTROL4_ADCFORMAT, format_mode);
	snd_soc_component_write_field(component, ES8323_DACCONTROL1,
				      ES8323_DACCONTROL1_DACFORMAT, format_mode);

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_IB_NF:
		inv_mode = 0;
		break;
	case SND_SOC_DAIFMT_IB_IF:
	case SND_SOC_DAIFMT_NB_IF:
		inv_mode = 1;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, ES8323_MASTERMODE,
				      ES8323_MASTERMODE_BCLKINV, inv_mode);
	snd_soc_component_update_bits(component, ES8323_ADCCONTROL4,
				      ES8323_ADCCONTROL4_ADCLRP, inv_mode);
	snd_soc_component_update_bits(component, ES8323_DACCONTROL1,
				      ES8323_DACCONTROL1_DACLRP, inv_mode);

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
	u8 wl_mode, fs;
	int coeff;

	coeff = get_coeff(es8323->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(es8323->sysclk / 2, params_rate(params));
		if (coeff < 0) {
			dev_err(component->dev,
				"Unable to configure sample rate %dHz with %dHz MCLK\n",
				params_rate(params), es8323->sysclk);
			return coeff;
		}

		snd_soc_component_update_bits(component, ES8323_MASTERMODE,
					      ES8323_MASTERMODE_MCLKDIV2,
					      ES8323_MASTERMODE_MCLKDIV2);
	}

	fs = FIELD_PREP(ES8323_DACCONTROL2_DACFSMODE, es8323_coeff_div[coeff].usb)
	   | FIELD_PREP(ES8323_DACCONTROL2_DACFSRATIO, es8323_coeff_div[coeff].sr);

	snd_soc_component_write_field(component, ES8323_ADCCONTROL5,
				      ES8323_ADCCONTROL5_ADCFS_MASK, fs);

	snd_soc_component_write_field(component, ES8323_DACCONTROL2,
				      ES8323_DACCONTROL2_DACFS_MASK, fs);

	/* serial audio data word length */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		wl_mode = ES8323_S16_LE;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		wl_mode = ES8323_S20_LE;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		wl_mode = ES8323_S24_LE;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		wl_mode = ES8323_S32_LE;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write_field(component, ES8323_ADCCONTROL4,
				      ES8323_ADCCONTROL4_ADCWL, wl_mode);

	snd_soc_component_write_field(component, ES8323_DACCONTROL1,
				      ES8323_DACCONTROL1_DACWL, wl_mode);

	return 0;
}

static int es8323_mute_stream(struct snd_soc_dai *dai, int mute, int stream)
{
	return snd_soc_component_update_bits(dai->component, ES8323_DACCONTROL3,
					     ES8323_DACCONTROL3_DACMUTE,
					     mute ? ES8323_DACCONTROL3_DACMUTE : 0);
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
	snd_soc_component_write(component, ES8323_DACCONTROL21, 0x80);

	return 0;
}

static int es8323_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct es8323_priv *es8323 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		ret = clk_prepare_enable(es8323->mclk);
		if (ret)
			return ret;

		snd_soc_component_write(component, ES8323_CHIPLOPOW1, 0x00);
		snd_soc_component_write(component, ES8323_CHIPLOPOW2, 0x00);
		snd_soc_component_update_bits(component, ES8323_ADCPOWER,
					      ES8323_ADCPOWER_PDNADCBIS, 0);
		break;
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_component_write(component, ES8323_CHIPLOPOW1, 0xff);
		snd_soc_component_write(component, ES8323_CHIPLOPOW2, 0xff);
		clk_disable_unprepare(es8323->mclk);
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
	{ "es8323" },
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
