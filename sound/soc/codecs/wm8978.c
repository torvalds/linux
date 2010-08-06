/*
 * wm8978.c  --  WM8978 ALSA SoC Audio Codec driver
 *
 * Copyright (C) 2009-2010 Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 * Copyright (C) 2007 Carlos Munoz <carlos@kenati.com>
 * Copyright 2006-2009 Wolfson Microelectronics PLC.
 * Based on wm8974 and wm8990 by Liam Girdwood <lrg@slimlogic.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/div64.h>

#include "wm8978.h"

static struct snd_soc_codec *wm8978_codec;

/* wm8978 register cache. Note that register 0 is not included in the cache. */
static const u16 wm8978_reg[WM8978_CACHEREGNUM] = {
	0x0000, 0x0000, 0x0000, 0x0000,	/* 0x00...0x03 */
	0x0050, 0x0000, 0x0140, 0x0000,	/* 0x04...0x07 */
	0x0000, 0x0000, 0x0000, 0x00ff,	/* 0x08...0x0b */
	0x00ff, 0x0000, 0x0100, 0x00ff,	/* 0x0c...0x0f */
	0x00ff, 0x0000, 0x012c, 0x002c,	/* 0x10...0x13 */
	0x002c, 0x002c, 0x002c, 0x0000,	/* 0x14...0x17 */
	0x0032, 0x0000, 0x0000, 0x0000,	/* 0x18...0x1b */
	0x0000, 0x0000, 0x0000, 0x0000,	/* 0x1c...0x1f */
	0x0038, 0x000b, 0x0032, 0x0000,	/* 0x20...0x23 */
	0x0008, 0x000c, 0x0093, 0x00e9,	/* 0x24...0x27 */
	0x0000, 0x0000, 0x0000, 0x0000,	/* 0x28...0x2b */
	0x0033, 0x0010, 0x0010, 0x0100,	/* 0x2c...0x2f */
	0x0100, 0x0002, 0x0001, 0x0001,	/* 0x30...0x33 */
	0x0039, 0x0039, 0x0039, 0x0039,	/* 0x34...0x37 */
	0x0001,	0x0001,			/* 0x38...0x3b */
};

/* codec private data */
struct wm8978_priv {
	struct snd_soc_codec codec;
	unsigned int f_pllout;
	unsigned int f_mclk;
	unsigned int f_256fs;
	unsigned int f_opclk;
	int mclk_idx;
	enum wm8978_sysclk_src sysclk;
	u16 reg_cache[WM8978_CACHEREGNUM];
};

static const char *wm8978_companding[] = {"Off", "NC", "u-law", "A-law"};
static const char *wm8978_eqmode[] = {"Capture", "Playback"};
static const char *wm8978_bw[] = {"Narrow", "Wide"};
static const char *wm8978_eq1[] = {"80Hz", "105Hz", "135Hz", "175Hz"};
static const char *wm8978_eq2[] = {"230Hz", "300Hz", "385Hz", "500Hz"};
static const char *wm8978_eq3[] = {"650Hz", "850Hz", "1.1kHz", "1.4kHz"};
static const char *wm8978_eq4[] = {"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz"};
static const char *wm8978_eq5[] = {"5.3kHz", "6.9kHz", "9kHz", "11.7kHz"};
static const char *wm8978_alc3[] = {"ALC", "Limiter"};
static const char *wm8978_alc1[] = {"Off", "Right", "Left", "Both"};

static const SOC_ENUM_SINGLE_DECL(adc_compand, WM8978_COMPANDING_CONTROL, 1,
				  wm8978_companding);
static const SOC_ENUM_SINGLE_DECL(dac_compand, WM8978_COMPANDING_CONTROL, 3,
				  wm8978_companding);
static const SOC_ENUM_SINGLE_DECL(eqmode, WM8978_EQ1, 8, wm8978_eqmode);
static const SOC_ENUM_SINGLE_DECL(eq1, WM8978_EQ1, 5, wm8978_eq1);
static const SOC_ENUM_SINGLE_DECL(eq2bw, WM8978_EQ2, 8, wm8978_bw);
static const SOC_ENUM_SINGLE_DECL(eq2, WM8978_EQ2, 5, wm8978_eq2);
static const SOC_ENUM_SINGLE_DECL(eq3bw, WM8978_EQ3, 8, wm8978_bw);
static const SOC_ENUM_SINGLE_DECL(eq3, WM8978_EQ3, 5, wm8978_eq3);
static const SOC_ENUM_SINGLE_DECL(eq4bw, WM8978_EQ4, 8, wm8978_bw);
static const SOC_ENUM_SINGLE_DECL(eq4, WM8978_EQ4, 5, wm8978_eq4);
static const SOC_ENUM_SINGLE_DECL(eq5, WM8978_EQ5, 5, wm8978_eq5);
static const SOC_ENUM_SINGLE_DECL(alc3, WM8978_ALC_CONTROL_3, 8, wm8978_alc3);
static const SOC_ENUM_SINGLE_DECL(alc1, WM8978_ALC_CONTROL_1, 7, wm8978_alc1);

static const DECLARE_TLV_DB_SCALE(digital_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(inpga_tlv, -1200, 75, 0);
static const DECLARE_TLV_DB_SCALE(spk_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, -1500, 300, 1);

static const struct snd_kcontrol_new wm8978_snd_controls[] = {

	SOC_SINGLE("Digital Loopback Switch",
		WM8978_COMPANDING_CONTROL, 0, 1, 0),

	SOC_ENUM("ADC Companding", adc_compand),
	SOC_ENUM("DAC Companding", dac_compand),

	SOC_DOUBLE("DAC Inversion Switch", WM8978_DAC_CONTROL, 0, 1, 1, 0),

	SOC_DOUBLE_R_TLV("PCM Volume",
		WM8978_LEFT_DAC_DIGITAL_VOLUME, WM8978_RIGHT_DAC_DIGITAL_VOLUME,
		0, 255, 0, digital_tlv),

	SOC_SINGLE("High Pass Filter Switch", WM8978_ADC_CONTROL, 8, 1, 0),
	SOC_SINGLE("High Pass Cut Off", WM8978_ADC_CONTROL, 4, 7, 0),
	SOC_DOUBLE("ADC Inversion Switch", WM8978_ADC_CONTROL, 0, 1, 1, 0),

	SOC_DOUBLE_R_TLV("ADC Volume",
		WM8978_LEFT_ADC_DIGITAL_VOLUME, WM8978_RIGHT_ADC_DIGITAL_VOLUME,
		0, 255, 0, digital_tlv),

	SOC_ENUM("Equaliser Function", eqmode),
	SOC_ENUM("EQ1 Cut Off", eq1),
	SOC_SINGLE_TLV("EQ1 Volume", WM8978_EQ1,  0, 24, 1, eq_tlv),

	SOC_ENUM("Equaliser EQ2 Bandwith", eq2bw),
	SOC_ENUM("EQ2 Cut Off", eq2),
	SOC_SINGLE_TLV("EQ2 Volume", WM8978_EQ2,  0, 24, 1, eq_tlv),

	SOC_ENUM("Equaliser EQ3 Bandwith", eq3bw),
	SOC_ENUM("EQ3 Cut Off", eq3),
	SOC_SINGLE_TLV("EQ3 Volume", WM8978_EQ3,  0, 24, 1, eq_tlv),

	SOC_ENUM("Equaliser EQ4 Bandwith", eq4bw),
	SOC_ENUM("EQ4 Cut Off", eq4),
	SOC_SINGLE_TLV("EQ4 Volume", WM8978_EQ4,  0, 24, 1, eq_tlv),

	SOC_ENUM("EQ5 Cut Off", eq5),
	SOC_SINGLE_TLV("EQ5 Volume", WM8978_EQ5, 0, 24, 1, eq_tlv),

	SOC_SINGLE("DAC Playback Limiter Switch",
		WM8978_DAC_LIMITER_1, 8, 1, 0),
	SOC_SINGLE("DAC Playback Limiter Decay",
		WM8978_DAC_LIMITER_1, 4, 15, 0),
	SOC_SINGLE("DAC Playback Limiter Attack",
		WM8978_DAC_LIMITER_1, 0, 15, 0),

	SOC_SINGLE("DAC Playback Limiter Threshold",
		WM8978_DAC_LIMITER_2, 4, 7, 0),
	SOC_SINGLE("DAC Playback Limiter Boost",
		WM8978_DAC_LIMITER_2, 0, 15, 0),

	SOC_ENUM("ALC Enable Switch", alc1),
	SOC_SINGLE("ALC Capture Min Gain", WM8978_ALC_CONTROL_1, 0, 7, 0),
	SOC_SINGLE("ALC Capture Max Gain", WM8978_ALC_CONTROL_1, 3, 7, 0),

	SOC_SINGLE("ALC Capture Hold", WM8978_ALC_CONTROL_2, 4, 7, 0),
	SOC_SINGLE("ALC Capture Target", WM8978_ALC_CONTROL_2, 0, 15, 0),

	SOC_ENUM("ALC Capture Mode", alc3),
	SOC_SINGLE("ALC Capture Decay", WM8978_ALC_CONTROL_3, 4, 15, 0),
	SOC_SINGLE("ALC Capture Attack", WM8978_ALC_CONTROL_3, 0, 15, 0),

	SOC_SINGLE("ALC Capture Noise Gate Switch", WM8978_NOISE_GATE, 3, 1, 0),
	SOC_SINGLE("ALC Capture Noise Gate Threshold",
		WM8978_NOISE_GATE, 0, 7, 0),

	SOC_DOUBLE_R("Capture PGA ZC Switch",
		WM8978_LEFT_INP_PGA_CONTROL, WM8978_RIGHT_INP_PGA_CONTROL,
		7, 1, 0),

	/* OUT1 - Headphones */
	SOC_DOUBLE_R("Headphone Playback ZC Switch",
		WM8978_LOUT1_HP_CONTROL, WM8978_ROUT1_HP_CONTROL, 7, 1, 0),

	SOC_DOUBLE_R_TLV("Headphone Playback Volume",
		WM8978_LOUT1_HP_CONTROL, WM8978_ROUT1_HP_CONTROL,
		0, 63, 0, spk_tlv),

	/* OUT2 - Speakers */
	SOC_DOUBLE_R("Speaker Playback ZC Switch",
		WM8978_LOUT2_SPK_CONTROL, WM8978_ROUT2_SPK_CONTROL, 7, 1, 0),

	SOC_DOUBLE_R_TLV("Speaker Playback Volume",
		WM8978_LOUT2_SPK_CONTROL, WM8978_ROUT2_SPK_CONTROL,
		0, 63, 0, spk_tlv),

	/* OUT3/4 - Line Output */
	SOC_DOUBLE_R("Line Playback Switch",
		WM8978_OUT3_MIXER_CONTROL, WM8978_OUT4_MIXER_CONTROL, 6, 1, 1),

	/* Mixer #3: Boost (Input) mixer */
	SOC_DOUBLE_R("PGA Boost (+20dB)",
		WM8978_LEFT_ADC_BOOST_CONTROL, WM8978_RIGHT_ADC_BOOST_CONTROL,
		8, 1, 0),
	SOC_DOUBLE_R_TLV("L2/R2 Boost Volume",
		WM8978_LEFT_ADC_BOOST_CONTROL, WM8978_RIGHT_ADC_BOOST_CONTROL,
		4, 7, 0, boost_tlv),
	SOC_DOUBLE_R_TLV("Aux Boost Volume",
		WM8978_LEFT_ADC_BOOST_CONTROL, WM8978_RIGHT_ADC_BOOST_CONTROL,
		0, 7, 0, boost_tlv),

	/* Input PGA volume */
	SOC_DOUBLE_R_TLV("Input PGA Volume",
		WM8978_LEFT_INP_PGA_CONTROL, WM8978_RIGHT_INP_PGA_CONTROL,
		0, 63, 0, inpga_tlv),

	/* Headphone */
	SOC_DOUBLE_R("Headphone Switch",
		WM8978_LOUT1_HP_CONTROL, WM8978_ROUT1_HP_CONTROL, 6, 1, 1),

	/* Speaker */
	SOC_DOUBLE_R("Speaker Switch",
		WM8978_LOUT2_SPK_CONTROL, WM8978_ROUT2_SPK_CONTROL, 6, 1, 1),

	/* DAC / ADC oversampling */
	SOC_SINGLE("DAC 128x Oversampling Switch", WM8978_DAC_CONTROL, 8, 1, 0),
	SOC_SINGLE("ADC 128x Oversampling Switch", WM8978_ADC_CONTROL, 8, 1, 0),
};

/* Mixer #1: Output (OUT1, OUT2) Mixer: mix AUX, Input mixer output and DAC */
static const struct snd_kcontrol_new wm8978_left_out_mixer[] = {
	SOC_DAPM_SINGLE("Line Bypass Switch", WM8978_LEFT_MIXER_CONTROL, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Playback Switch", WM8978_LEFT_MIXER_CONTROL, 5, 1, 0),
	SOC_DAPM_SINGLE("PCM Playback Switch", WM8978_LEFT_MIXER_CONTROL, 0, 1, 0),
};

static const struct snd_kcontrol_new wm8978_right_out_mixer[] = {
	SOC_DAPM_SINGLE("Line Bypass Switch", WM8978_RIGHT_MIXER_CONTROL, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Playback Switch", WM8978_RIGHT_MIXER_CONTROL, 5, 1, 0),
	SOC_DAPM_SINGLE("PCM Playback Switch", WM8978_RIGHT_MIXER_CONTROL, 0, 1, 0),
};

/* OUT3/OUT4 Mixer not implemented */

/* Mixer #2: Input PGA Mute */
static const struct snd_kcontrol_new wm8978_left_input_mixer[] = {
	SOC_DAPM_SINGLE("L2 Switch", WM8978_INPUT_CONTROL, 2, 1, 0),
	SOC_DAPM_SINGLE("MicN Switch", WM8978_INPUT_CONTROL, 1, 1, 0),
	SOC_DAPM_SINGLE("MicP Switch", WM8978_INPUT_CONTROL, 0, 1, 0),
};
static const struct snd_kcontrol_new wm8978_right_input_mixer[] = {
	SOC_DAPM_SINGLE("R2 Switch", WM8978_INPUT_CONTROL, 6, 1, 0),
	SOC_DAPM_SINGLE("MicN Switch", WM8978_INPUT_CONTROL, 5, 1, 0),
	SOC_DAPM_SINGLE("MicP Switch", WM8978_INPUT_CONTROL, 4, 1, 0),
};

static const struct snd_soc_dapm_widget wm8978_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("Left DAC", "Left HiFi Playback",
			 WM8978_POWER_MANAGEMENT_3, 0, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Right HiFi Playback",
			 WM8978_POWER_MANAGEMENT_3, 1, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left HiFi Capture",
			 WM8978_POWER_MANAGEMENT_2, 0, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right HiFi Capture",
			 WM8978_POWER_MANAGEMENT_2, 1, 0),

	/* Mixer #1: OUT1,2 */
	SOC_MIXER_ARRAY("Left Output Mixer", WM8978_POWER_MANAGEMENT_3,
			2, 0, wm8978_left_out_mixer),
	SOC_MIXER_ARRAY("Right Output Mixer", WM8978_POWER_MANAGEMENT_3,
			3, 0, wm8978_right_out_mixer),

	SOC_MIXER_ARRAY("Left Input Mixer", WM8978_POWER_MANAGEMENT_2,
			2, 0, wm8978_left_input_mixer),
	SOC_MIXER_ARRAY("Right Input Mixer", WM8978_POWER_MANAGEMENT_2,
			3, 0, wm8978_right_input_mixer),

	SND_SOC_DAPM_PGA("Left Boost Mixer", WM8978_POWER_MANAGEMENT_2,
			 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Boost Mixer", WM8978_POWER_MANAGEMENT_2,
			 5, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Left Capture PGA", WM8978_LEFT_INP_PGA_CONTROL,
			 6, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right Capture PGA", WM8978_RIGHT_INP_PGA_CONTROL,
			 6, 1, NULL, 0),

	SND_SOC_DAPM_PGA("Left Headphone Out", WM8978_POWER_MANAGEMENT_2,
			 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Headphone Out", WM8978_POWER_MANAGEMENT_2,
			 8, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Left Speaker Out", WM8978_POWER_MANAGEMENT_3,
			 6, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Speaker Out", WM8978_POWER_MANAGEMENT_3,
			 5, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("OUT4 VMID", WM8978_POWER_MANAGEMENT_3,
			   8, 0, NULL, 0),

	SND_SOC_DAPM_MICBIAS("Mic Bias", WM8978_POWER_MANAGEMENT_1, 4, 0),

	SND_SOC_DAPM_INPUT("LMICN"),
	SND_SOC_DAPM_INPUT("LMICP"),
	SND_SOC_DAPM_INPUT("RMICN"),
	SND_SOC_DAPM_INPUT("RMICP"),
	SND_SOC_DAPM_INPUT("LAUX"),
	SND_SOC_DAPM_INPUT("RAUX"),
	SND_SOC_DAPM_INPUT("L2"),
	SND_SOC_DAPM_INPUT("R2"),
	SND_SOC_DAPM_OUTPUT("LHP"),
	SND_SOC_DAPM_OUTPUT("RHP"),
	SND_SOC_DAPM_OUTPUT("LSPK"),
	SND_SOC_DAPM_OUTPUT("RSPK"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Output mixer */
	{"Right Output Mixer", "PCM Playback Switch", "Right DAC"},
	{"Right Output Mixer", "Aux Playback Switch", "RAUX"},
	{"Right Output Mixer", "Line Bypass Switch", "Right Boost Mixer"},

	{"Left Output Mixer", "PCM Playback Switch", "Left DAC"},
	{"Left Output Mixer", "Aux Playback Switch", "LAUX"},
	{"Left Output Mixer", "Line Bypass Switch", "Left Boost Mixer"},

	/* Outputs */
	{"Right Headphone Out", NULL, "Right Output Mixer"},
	{"RHP", NULL, "Right Headphone Out"},

	{"Left Headphone Out", NULL, "Left Output Mixer"},
	{"LHP", NULL, "Left Headphone Out"},

	{"Right Speaker Out", NULL, "Right Output Mixer"},
	{"RSPK", NULL, "Right Speaker Out"},

	{"Left Speaker Out", NULL, "Left Output Mixer"},
	{"LSPK", NULL, "Left Speaker Out"},

	/* Boost Mixer */
	{"Right ADC", NULL, "Right Boost Mixer"},

	{"Right Boost Mixer", NULL, "RAUX"},
	{"Right Boost Mixer", NULL, "Right Capture PGA"},
	{"Right Boost Mixer", NULL, "R2"},

	{"Left ADC", NULL, "Left Boost Mixer"},

	{"Left Boost Mixer", NULL, "LAUX"},
	{"Left Boost Mixer", NULL, "Left Capture PGA"},
	{"Left Boost Mixer", NULL, "L2"},

	/* Input PGA */
	{"Right Capture PGA", NULL, "Right Input Mixer"},
	{"Left Capture PGA", NULL, "Left Input Mixer"},

	{"Right Input Mixer", "R2 Switch", "R2"},
	{"Right Input Mixer", "MicN Switch", "RMICN"},
	{"Right Input Mixer", "MicP Switch", "RMICP"},

	{"Left Input Mixer", "L2 Switch", "L2"},
	{"Left Input Mixer", "MicN Switch", "LMICN"},
	{"Left Input Mixer", "MicP Switch", "LMICP"},
};

static int wm8978_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8978_dapm_widgets,
				  ARRAY_SIZE(wm8978_dapm_widgets));

	/* set up the WM8978 audio map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	return 0;
}

/* PLL divisors */
struct wm8978_pll_div {
	u32 k;
	u8 n;
	u8 div2;
};

#define FIXED_PLL_SIZE (1 << 24)

static void pll_factors(struct wm8978_pll_div *pll_div, unsigned int target,
			unsigned int source)
{
	u64 k_part;
	unsigned int k, n_div, n_mod;

	n_div = target / source;
	if (n_div < 6) {
		source >>= 1;
		pll_div->div2 = 1;
		n_div = target / source;
	} else {
		pll_div->div2 = 0;
	}

	if (n_div < 6 || n_div > 12)
		dev_warn(wm8978_codec->dev,
			 "WM8978 N value exceeds recommended range! N = %u\n",
			 n_div);

	pll_div->n = n_div;
	n_mod = target - source * n_div;
	k_part = FIXED_PLL_SIZE * (long long)n_mod + source / 2;

	do_div(k_part, source);

	k = k_part & 0xFFFFFFFF;

	pll_div->k = k;
}

/* MCLK dividers */
static const int mclk_numerator[]	= {1, 3, 2, 3, 4, 6, 8, 12};
static const int mclk_denominator[]	= {1, 2, 1, 1, 1, 1, 1, 1};

/*
 * find index >= idx, such that, for a given f_out,
 * 3 * f_mclk / 4 <= f_PLLOUT < 13 * f_mclk / 4
 * f_out can be f_256fs or f_opclk, currently only used for f_256fs. Can be
 * generalised for f_opclk with suitable coefficient arrays, but currently
 * the OPCLK divisor is calculated directly, not iteratively.
 */
static int wm8978_enum_mclk(unsigned int f_out, unsigned int f_mclk,
			    unsigned int *f_pllout)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mclk_numerator); i++) {
		unsigned int f_pllout_x4 = 4 * f_out * mclk_numerator[i] /
			mclk_denominator[i];
		if (3 * f_mclk <= f_pllout_x4 && f_pllout_x4 < 13 * f_mclk) {
			*f_pllout = f_pllout_x4 / 4;
			return i;
		}
	}

	return -EINVAL;
}

/*
 * Calculate internal frequencies and dividers, according to Figure 40
 * "PLL and Clock Select Circuit" in WM8978 datasheet Rev. 2.6
 */
static int wm8978_configure_pll(struct snd_soc_codec *codec)
{
	struct wm8978_priv *wm8978 = snd_soc_codec_get_drvdata(codec);
	struct wm8978_pll_div pll_div;
	unsigned int f_opclk = wm8978->f_opclk, f_mclk = wm8978->f_mclk,
		f_256fs = wm8978->f_256fs;
	unsigned int f2;

	if (!f_mclk)
		return -EINVAL;

	if (f_opclk) {
		unsigned int opclk_div;
		/* Cannot set up MCLK divider now, do later */
		wm8978->mclk_idx = -1;

		/*
		 * The user needs OPCLK. Choose OPCLKDIV to put
		 * 6 <= R = f2 / f1 < 13, 1 <= OPCLKDIV <= 4.
		 * f_opclk = f_mclk * prescale * R / 4 / OPCLKDIV, where
		 * prescale = 1, or prescale = 2. Prescale is calculated inside
		 * pll_factors(). We have to select f_PLLOUT, such that
		 * f_mclk * 3 / 4 <= f_PLLOUT < f_mclk * 13 / 4. Must be
		 * f_mclk * 3 / 16 <= f_opclk < f_mclk * 13 / 4.
		 */
		if (16 * f_opclk < 3 * f_mclk || 4 * f_opclk >= 13 * f_mclk)
			return -EINVAL;

		if (4 * f_opclk < 3 * f_mclk)
			/* Have to use OPCLKDIV */
			opclk_div = (3 * f_mclk / 4 + f_opclk - 1) / f_opclk;
		else
			opclk_div = 1;

		dev_dbg(codec->dev, "%s: OPCLKDIV=%d\n", __func__, opclk_div);

		snd_soc_update_bits(codec, WM8978_GPIO_CONTROL, 0x30,
				    (opclk_div - 1) << 4);

		wm8978->f_pllout = f_opclk * opclk_div;
	} else if (f_256fs) {
		/*
		 * Not using OPCLK, but PLL is used for the codec, choose R:
		 * 6 <= R = f2 / f1 < 13, to put 1 <= MCLKDIV <= 12.
		 * f_256fs = f_mclk * prescale * R / 4 / MCLKDIV, where
		 * prescale = 1, or prescale = 2. Prescale is calculated inside
		 * pll_factors(). We have to select f_PLLOUT, such that
		 * f_mclk * 3 / 4 <= f_PLLOUT < f_mclk * 13 / 4. Must be
		 * f_mclk * 3 / 48 <= f_256fs < f_mclk * 13 / 4. This means MCLK
		 * must be 3.781MHz <= f_MCLK <= 32.768MHz
		 */
		int idx = wm8978_enum_mclk(f_256fs, f_mclk, &wm8978->f_pllout);
		if (idx < 0)
			return idx;

		wm8978->mclk_idx = idx;

		/* GPIO1 into default mode as input - before configuring PLL */
		snd_soc_update_bits(codec, WM8978_GPIO_CONTROL, 7, 0);
	} else {
		return -EINVAL;
	}

	f2 = wm8978->f_pllout * 4;

	dev_dbg(codec->dev, "%s: f_MCLK=%uHz, f_PLLOUT=%uHz\n", __func__,
		wm8978->f_mclk, wm8978->f_pllout);

	pll_factors(&pll_div, f2, wm8978->f_mclk);

	dev_dbg(codec->dev, "%s: calculated PLL N=0x%x, K=0x%x, div2=%d\n",
		__func__, pll_div.n, pll_div.k, pll_div.div2);

	/* Turn PLL off for configuration... */
	snd_soc_update_bits(codec, WM8978_POWER_MANAGEMENT_1, 0x20, 0);

	snd_soc_write(codec, WM8978_PLL_N, (pll_div.div2 << 4) | pll_div.n);
	snd_soc_write(codec, WM8978_PLL_K1, pll_div.k >> 18);
	snd_soc_write(codec, WM8978_PLL_K2, (pll_div.k >> 9) & 0x1ff);
	snd_soc_write(codec, WM8978_PLL_K3, pll_div.k & 0x1ff);

	/* ...and on again */
	snd_soc_update_bits(codec, WM8978_POWER_MANAGEMENT_1, 0x20, 0x20);

	if (f_opclk)
		/* Output PLL (OPCLK) to GPIO1 */
		snd_soc_update_bits(codec, WM8978_GPIO_CONTROL, 7, 4);

	return 0;
}

/*
 * Configure WM8978 clock dividers.
 */
static int wm8978_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
				 int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8978_priv *wm8978 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	switch (div_id) {
	case WM8978_OPCLKRATE:
		wm8978->f_opclk = div;

		if (wm8978->f_mclk)
			/*
			 * We know the MCLK frequency, the user has requested
			 * OPCLK, configure the PLL based on that and start it
			 * and OPCLK immediately. We will configure PLL to match
			 * user-requested OPCLK frquency as good as possible.
			 * In fact, it is likely, that matching the sampling
			 * rate, when it becomes known, is more important, and
			 * we will not be reconfiguring PLL then, because we
			 * must not interrupt OPCLK. But it should be fine,
			 * because typically the user will request OPCLK to run
			 * at 256fs or 512fs, and for these cases we will also
			 * find an exact MCLK divider configuration - it will
			 * be equal to or double the OPCLK divisor.
			 */
			ret = wm8978_configure_pll(codec);
		break;
	case WM8978_BCLKDIV:
		if (div & ~0x1c)
			return -EINVAL;
		snd_soc_update_bits(codec, WM8978_CLOCKING, 0x1c, div);
		break;
	default:
		return -EINVAL;
	}

	dev_dbg(codec->dev, "%s: ID %d, value %u\n", __func__, div_id, div);

	return ret;
}

/*
 * @freq:	when .set_pll() us not used, freq is codec MCLK input frequency
 */
static int wm8978_set_dai_sysclk(struct snd_soc_dai *codec_dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8978_priv *wm8978 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	dev_dbg(codec->dev, "%s: ID %d, freq %u\n", __func__, clk_id, freq);

	if (freq) {
		wm8978->f_mclk = freq;

		/* Even if MCLK is used for system clock, might have to drive OPCLK */
		if (wm8978->f_opclk)
			ret = wm8978_configure_pll(codec);

		/* Our sysclk is fixed to 256 * fs, will configure in .hw_params()  */

		if (!ret)
			wm8978->sysclk = clk_id;
	}

	if (wm8978->sysclk == WM8978_PLL && (!freq || clk_id == WM8978_MCLK)) {
		/* Clock CODEC directly from MCLK */
		snd_soc_update_bits(codec, WM8978_CLOCKING, 0x100, 0);

		/* GPIO1 into default mode as input - before configuring PLL */
		snd_soc_update_bits(codec, WM8978_GPIO_CONTROL, 7, 0);

		/* Turn off PLL */
		snd_soc_update_bits(codec, WM8978_POWER_MANAGEMENT_1, 0x20, 0);
		wm8978->sysclk = WM8978_MCLK;
		wm8978->f_pllout = 0;
		wm8978->f_opclk = 0;
	}

	return ret;
}

/*
 * Set ADC and Voice DAC format.
 */
static int wm8978_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	/*
	 * BCLK polarity mask = 0x100, LRC clock polarity mask = 0x80,
	 * Data Format mask = 0x18: all will be calculated anew
	 */
	u16 iface = snd_soc_read(codec, WM8978_AUDIO_INTERFACE) & ~0x198;
	u16 clk = snd_soc_read(codec, WM8978_CLOCKING);

	dev_dbg(codec->dev, "%s\n", __func__);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clk |= 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		clk &= ~1;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x10;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x8;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x18;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x180;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x100;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x80;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8978_AUDIO_INTERFACE, iface);
	snd_soc_write(codec, WM8978_CLOCKING, clk);

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8978_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wm8978_priv *wm8978 = snd_soc_codec_get_drvdata(codec);
	/* Word length mask = 0x60 */
	u16 iface_ctl = snd_soc_read(codec, WM8978_AUDIO_INTERFACE) & ~0x60;
	/* Sampling rate mask = 0xe (for filters) */
	u16 add_ctl = snd_soc_read(codec, WM8978_ADDITIONAL_CONTROL) & ~0xe;
	u16 clking = snd_soc_read(codec, WM8978_CLOCKING);
	enum wm8978_sysclk_src current_clk_id = clking & 0x100 ?
		WM8978_PLL : WM8978_MCLK;
	unsigned int f_sel, diff, diff_best = INT_MAX;
	int i, best = 0;

	if (!wm8978->f_mclk)
		return -EINVAL;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface_ctl |= 0x20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface_ctl |= 0x40;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface_ctl |= 0x60;
		break;
	}

	/* filter coefficient */
	switch (params_rate(params)) {
	case 8000:
		add_ctl |= 0x5 << 1;
		break;
	case 11025:
		add_ctl |= 0x4 << 1;
		break;
	case 16000:
		add_ctl |= 0x3 << 1;
		break;
	case 22050:
		add_ctl |= 0x2 << 1;
		break;
	case 32000:
		add_ctl |= 0x1 << 1;
		break;
	case 44100:
	case 48000:
		break;
	}

	/* Sampling rate is known now, can configure the MCLK divider */
	wm8978->f_256fs = params_rate(params) * 256;

	if (wm8978->sysclk == WM8978_MCLK) {
		wm8978->mclk_idx = -1;
		f_sel = wm8978->f_mclk;
	} else {
		if (!wm8978->f_pllout) {
			/* We only enter here, if OPCLK is not used */
			int ret = wm8978_configure_pll(codec);
			if (ret < 0)
				return ret;
		}
		f_sel = wm8978->f_pllout;
	}

	if (wm8978->mclk_idx < 0) {
		/* Either MCLK is used directly, or OPCLK is used */
		if (f_sel < wm8978->f_256fs || f_sel > 12 * wm8978->f_256fs)
			return -EINVAL;

		for (i = 0; i < ARRAY_SIZE(mclk_numerator); i++) {
			diff = abs(wm8978->f_256fs * 3 -
				   f_sel * 3 * mclk_denominator[i] / mclk_numerator[i]);

			if (diff < diff_best) {
				diff_best = diff;
				best = i;
			}

			if (!diff)
				break;
		}
	} else {
		/* OPCLK not used, codec driven by PLL */
		best = wm8978->mclk_idx;
		diff = 0;
	}

	if (diff)
		dev_warn(codec->dev, "Imprecise sampling rate: %uHz%s\n",
			f_sel * mclk_denominator[best] / mclk_numerator[best] / 256,
			wm8978->sysclk == WM8978_MCLK ?
			", consider using PLL" : "");

	dev_dbg(codec->dev, "%s: fmt %d, rate %u, MCLK divisor #%d\n", __func__,
		params_format(params), params_rate(params), best);

	/* MCLK divisor mask = 0xe0 */
	snd_soc_update_bits(codec, WM8978_CLOCKING, 0xe0, best << 5);

	snd_soc_write(codec, WM8978_AUDIO_INTERFACE, iface_ctl);
	snd_soc_write(codec, WM8978_ADDITIONAL_CONTROL, add_ctl);

	if (wm8978->sysclk != current_clk_id) {
		if (wm8978->sysclk == WM8978_PLL)
			/* Run CODEC from PLL instead of MCLK */
			snd_soc_update_bits(codec, WM8978_CLOCKING,
					    0x100, 0x100);
		else
			/* Clock CODEC directly from MCLK */
			snd_soc_update_bits(codec, WM8978_CLOCKING, 0x100, 0);
	}

	return 0;
}

static int wm8978_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	dev_dbg(codec->dev, "%s: %d\n", __func__, mute);

	if (mute)
		snd_soc_update_bits(codec, WM8978_DAC_CONTROL, 0x40, 0x40);
	else
		snd_soc_update_bits(codec, WM8978_DAC_CONTROL, 0x40, 0);

	return 0;
}

static int wm8978_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 power1 = snd_soc_read(codec, WM8978_POWER_MANAGEMENT_1) & ~3;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		power1 |= 1;  /* VMID 75k */
		snd_soc_write(codec, WM8978_POWER_MANAGEMENT_1, power1);
		break;
	case SND_SOC_BIAS_STANDBY:
		/* bit 3: enable bias, bit 2: enable I/O tie off buffer */
		power1 |= 0xc;

		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			/* Initial cap charge at VMID 5k */
			snd_soc_write(codec, WM8978_POWER_MANAGEMENT_1,
				      power1 | 0x3);
			mdelay(100);
		}

		power1 |= 0x2;  /* VMID 500k */
		snd_soc_write(codec, WM8978_POWER_MANAGEMENT_1, power1);
		break;
	case SND_SOC_BIAS_OFF:
		/* Preserve PLL - OPCLK may be used by someone */
		snd_soc_update_bits(codec, WM8978_POWER_MANAGEMENT_1, ~0x20, 0);
		snd_soc_write(codec, WM8978_POWER_MANAGEMENT_2, 0);
		snd_soc_write(codec, WM8978_POWER_MANAGEMENT_3, 0);
		break;
	}

	dev_dbg(codec->dev, "%s: %d, %x\n", __func__, level, power1);

	codec->bias_level = level;
	return 0;
}

#define WM8978_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops wm8978_dai_ops = {
	.hw_params	= wm8978_hw_params,
	.digital_mute	= wm8978_mute,
	.set_fmt	= wm8978_set_dai_fmt,
	.set_clkdiv	= wm8978_set_dai_clkdiv,
	.set_sysclk	= wm8978_set_dai_sysclk,
};

/* Also supports 12kHz */
struct snd_soc_dai wm8978_dai = {
	.name = "WM8978 HiFi",
	.id = 1,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = WM8978_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = WM8978_FORMATS,
	},
	.ops = &wm8978_dai_ops,
};
EXPORT_SYMBOL_GPL(wm8978_dai);

static int wm8978_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	wm8978_set_bias_level(codec, SND_SOC_BIAS_OFF);
	/* Also switch PLL off */
	snd_soc_write(codec, WM8978_POWER_MANAGEMENT_1, 0);

	return 0;
}

static int wm8978_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	struct wm8978_priv *wm8978 = snd_soc_codec_get_drvdata(codec);
	int i;
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8978_reg); i++) {
		if (i == WM8978_RESET)
			continue;
		if (cache[i] != wm8978_reg[i])
			snd_soc_write(codec, i, cache[i]);
	}

	wm8978_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	if (wm8978->f_pllout)
		/* Switch PLL on */
		snd_soc_update_bits(codec, WM8978_POWER_MANAGEMENT_1, 0x20, 0x20);

	return 0;
}

static int wm8978_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8978_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8978_codec;
	codec = wm8978_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8978_snd_controls,
			     ARRAY_SIZE(wm8978_snd_controls));
	wm8978_add_widgets(codec);

pcm_err:
	return ret;
}

/* power down chip */
static int wm8978_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8978 = {
	.probe		= wm8978_probe,
	.remove		= wm8978_remove,
	.suspend	= wm8978_suspend,
	.resume		= wm8978_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8978);

/*
 * These registers contain an "update" bit - bit 8. This means, for example,
 * that one can write new DAC digital volume for both channels, but only when
 * the update bit is set, will also the volume be updated - simultaneously for
 * both channels.
 */
static const int update_reg[] = {
	WM8978_LEFT_DAC_DIGITAL_VOLUME,
	WM8978_RIGHT_DAC_DIGITAL_VOLUME,
	WM8978_LEFT_ADC_DIGITAL_VOLUME,
	WM8978_RIGHT_ADC_DIGITAL_VOLUME,
	WM8978_LEFT_INP_PGA_CONTROL,
	WM8978_RIGHT_INP_PGA_CONTROL,
	WM8978_LOUT1_HP_CONTROL,
	WM8978_ROUT1_HP_CONTROL,
	WM8978_LOUT2_SPK_CONTROL,
	WM8978_ROUT2_SPK_CONTROL,
};

static __devinit int wm8978_register(struct wm8978_priv *wm8978)
{
	int ret, i;
	struct snd_soc_codec *codec = &wm8978->codec;

	if (wm8978_codec) {
		dev_err(codec->dev, "Another WM8978 is registered\n");
		return -EINVAL;
	}

	/*
	 * Set default system clock to PLL, it is more precise, this is also the
	 * default hardware setting
	 */
	wm8978->sysclk = WM8978_PLL;

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	snd_soc_codec_set_drvdata(codec, wm8978);
	codec->name = "WM8978";
	codec->owner = THIS_MODULE;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8978_set_bias_level;
	codec->dai = &wm8978_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = WM8978_CACHEREGNUM;
	codec->reg_cache = &wm8978->reg_cache;

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, SND_SOC_I2C);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache I/O: %d\n", ret);
		goto err;
	}

	memcpy(codec->reg_cache, wm8978_reg, sizeof(wm8978_reg));

	/*
	 * Set the update bit in all registers, that have one. This way all
	 * writes to those registers will also cause the update bit to be
	 * written.
	 */
	for (i = 0; i < ARRAY_SIZE(update_reg); i++)
		((u16 *)codec->reg_cache)[update_reg[i]] |= 0x100;

	/* Reset the codec */
	ret = snd_soc_write(codec, WM8978_RESET, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		goto err;
	}

	wm8978_dai.dev = codec->dev;

	wm8978_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	wm8978_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		goto err;
	}

	ret = snd_soc_register_dai(&wm8978_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		goto err_codec;
	}

	return 0;

err_codec:
	snd_soc_unregister_codec(codec);
err:
	kfree(wm8978);
	return ret;
}

static __devexit void wm8978_unregister(struct wm8978_priv *wm8978)
{
	wm8978_set_bias_level(&wm8978->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm8978_dai);
	snd_soc_unregister_codec(&wm8978->codec);
	kfree(wm8978);
	wm8978_codec = NULL;
}

static __devinit int wm8978_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8978_priv *wm8978;
	struct snd_soc_codec *codec;

	wm8978 = kzalloc(sizeof(struct wm8978_priv), GFP_KERNEL);
	if (wm8978 == NULL)
		return -ENOMEM;

	codec = &wm8978->codec;
	codec->hw_write = (hw_write_t)i2c_master_send;

	i2c_set_clientdata(i2c, wm8978);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8978_register(wm8978);
}

static __devexit int wm8978_i2c_remove(struct i2c_client *client)
{
	struct wm8978_priv *wm8978 = i2c_get_clientdata(client);
	wm8978_unregister(wm8978);
	return 0;
}

static const struct i2c_device_id wm8978_i2c_id[] = {
	{ "wm8978", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8978_i2c_id);

static struct i2c_driver wm8978_i2c_driver = {
	.driver = {
		.name = "WM8978",
		.owner = THIS_MODULE,
	},
	.probe =    wm8978_i2c_probe,
	.remove =   __devexit_p(wm8978_i2c_remove),
	.id_table = wm8978_i2c_id,
};

static int __init wm8978_modinit(void)
{
	return i2c_add_driver(&wm8978_i2c_driver);
}
module_init(wm8978_modinit);

static void __exit wm8978_exit(void)
{
	i2c_del_driver(&wm8978_i2c_driver);
}
module_exit(wm8978_exit);

MODULE_DESCRIPTION("ASoC WM8978 codec driver");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL");
