/*
 * wm8971.c  --  WM8971 ALSA SoC Audio driver
 *
 * Copyright 2005 Lab126, Inc.
 *
 * Author: Kenneth Kiraly <kiraly@lab126.com>
 *
 * Based on wm8753.c by Liam Girdwood
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "wm8971.h"

#define	WM8971_REG_COUNT		43

static struct workqueue_struct *wm8971_workq = NULL;

/* codec private data */
struct wm8971_priv {
	unsigned int sysclk;
};

/*
 * wm8971 register cache
 * We can't read the WM8971 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const struct reg_default wm8971_reg_defaults[] = {
	{  0, 0x0097 },
	{  1, 0x0097 },
	{  2, 0x0079 },
	{  3, 0x0079 },
	{  4, 0x0000 },
	{  5, 0x0008 },
	{  6, 0x0000 },
	{  7, 0x000a },
	{  8, 0x0000 },
	{  9, 0x0000 },
	{ 10, 0x00ff },
	{ 11, 0x00ff },
	{ 12, 0x000f },
	{ 13, 0x000f },
	{ 14, 0x0000 },
	{ 15, 0x0000 },
	{ 16, 0x0000 },
	{ 17, 0x007b },
	{ 18, 0x0000 },
	{ 19, 0x0032 },
	{ 20, 0x0000 },
	{ 21, 0x00c3 },
	{ 22, 0x00c3 },
	{ 23, 0x00c0 },
	{ 24, 0x0000 },
	{ 25, 0x0000 },
	{ 26, 0x0000 },
	{ 27, 0x0000 },
	{ 28, 0x0000 },
	{ 29, 0x0000 },
	{ 30, 0x0000 },
	{ 31, 0x0000 },
	{ 32, 0x0000 },
	{ 33, 0x0000 },
	{ 34, 0x0050 },
	{ 35, 0x0050 },
	{ 36, 0x0050 },
	{ 37, 0x0050 },
	{ 38, 0x0050 },
	{ 39, 0x0050 },
	{ 40, 0x0079 },
	{ 41, 0x0079 },
	{ 42, 0x0079 },
};

#define wm8971_reset(c)	snd_soc_write(c, WM8971_RESET, 0)

/* WM8971 Controls */
static const char *wm8971_bass[] = { "Linear Control", "Adaptive Boost" };
static const char *wm8971_bass_filter[] = { "130Hz @ 48kHz",
	"200Hz @ 48kHz" };
static const char *wm8971_treble[] = { "8kHz", "4kHz" };
static const char *wm8971_alc_func[] = { "Off", "Right", "Left", "Stereo" };
static const char *wm8971_ng_type[] = { "Constant PGA Gain",
	"Mute ADC Output" };
static const char *wm8971_deemp[] = { "None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8971_mono_mux[] = {"Stereo", "Mono (Left)",
	"Mono (Right)", "Digital Mono"};
static const char *wm8971_dac_phase[] = { "Non Inverted", "Inverted" };
static const char *wm8971_lline_mux[] = {"Line", "NC", "NC", "PGA",
	"Differential"};
static const char *wm8971_rline_mux[] = {"Line", "Mic", "NC", "PGA",
	"Differential"};
static const char *wm8971_lpga_sel[] = {"Line", "NC", "NC", "Differential"};
static const char *wm8971_rpga_sel[] = {"Line", "Mic", "NC", "Differential"};
static const char *wm8971_adcpol[] = {"Normal", "L Invert", "R Invert",
	"L + R Invert"};

static const struct soc_enum wm8971_enum[] = {
	SOC_ENUM_SINGLE(WM8971_BASS, 7, 2, wm8971_bass),	/* 0 */
	SOC_ENUM_SINGLE(WM8971_BASS, 6, 2, wm8971_bass_filter),
	SOC_ENUM_SINGLE(WM8971_TREBLE, 6, 2, wm8971_treble),
	SOC_ENUM_SINGLE(WM8971_ALC1, 7, 4, wm8971_alc_func),
	SOC_ENUM_SINGLE(WM8971_NGATE, 1, 2, wm8971_ng_type),    /* 4 */
	SOC_ENUM_SINGLE(WM8971_ADCDAC, 1, 4, wm8971_deemp),
	SOC_ENUM_SINGLE(WM8971_ADCTL1, 4, 4, wm8971_mono_mux),
	SOC_ENUM_SINGLE(WM8971_ADCTL1, 1, 2, wm8971_dac_phase),
	SOC_ENUM_SINGLE(WM8971_LOUTM1, 0, 5, wm8971_lline_mux), /* 8 */
	SOC_ENUM_SINGLE(WM8971_ROUTM1, 0, 5, wm8971_rline_mux),
	SOC_ENUM_SINGLE(WM8971_LADCIN, 6, 4, wm8971_lpga_sel),
	SOC_ENUM_SINGLE(WM8971_RADCIN, 6, 4, wm8971_rpga_sel),
	SOC_ENUM_SINGLE(WM8971_ADCDAC, 5, 4, wm8971_adcpol),    /* 12 */
	SOC_ENUM_SINGLE(WM8971_ADCIN, 6, 4, wm8971_mono_mux),
};

static const struct snd_kcontrol_new wm8971_snd_controls[] = {
	SOC_DOUBLE_R("Capture Volume", WM8971_LINVOL, WM8971_RINVOL, 0, 63, 0),
	SOC_DOUBLE_R("Capture ZC Switch", WM8971_LINVOL, WM8971_RINVOL,
		     6, 1, 0),
	SOC_DOUBLE_R("Capture Switch", WM8971_LINVOL, WM8971_RINVOL, 7, 1, 1),

	SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8971_LOUT1V,
		WM8971_ROUT1V, 7, 1, 0),
	SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8971_LOUT2V,
		WM8971_ROUT2V, 7, 1, 0),
	SOC_SINGLE("Mono Playback ZC Switch", WM8971_MOUTV, 7, 1, 0),

	SOC_DOUBLE_R("PCM Volume", WM8971_LDAC, WM8971_RDAC, 0, 255, 0),

	SOC_DOUBLE_R("Bypass Left Playback Volume", WM8971_LOUTM1,
		WM8971_LOUTM2, 4, 7, 1),
	SOC_DOUBLE_R("Bypass Right Playback Volume", WM8971_ROUTM1,
		WM8971_ROUTM2, 4, 7, 1),
	SOC_DOUBLE_R("Bypass Mono Playback Volume", WM8971_MOUTM1,
		WM8971_MOUTM2, 4, 7, 1),

	SOC_DOUBLE_R("Headphone Playback Volume", WM8971_LOUT1V,
		WM8971_ROUT1V, 0, 127, 0),
	SOC_DOUBLE_R("Speaker Playback Volume", WM8971_LOUT2V,
		WM8971_ROUT2V, 0, 127, 0),

	SOC_ENUM("Bass Boost", wm8971_enum[0]),
	SOC_ENUM("Bass Filter", wm8971_enum[1]),
	SOC_SINGLE("Bass Volume", WM8971_BASS, 0, 7, 1),

	SOC_SINGLE("Treble Volume", WM8971_TREBLE, 0, 7, 0),
	SOC_ENUM("Treble Cut-off", wm8971_enum[2]),

	SOC_SINGLE("Capture Filter Switch", WM8971_ADCDAC, 0, 1, 1),

	SOC_SINGLE("ALC Target Volume", WM8971_ALC1, 0, 7, 0),
	SOC_SINGLE("ALC Max Volume", WM8971_ALC1, 4, 7, 0),

	SOC_SINGLE("ALC Capture Target Volume", WM8971_ALC1, 0, 7, 0),
	SOC_SINGLE("ALC Capture Max Volume", WM8971_ALC1, 4, 7, 0),
	SOC_ENUM("ALC Capture Function", wm8971_enum[3]),
	SOC_SINGLE("ALC Capture ZC Switch", WM8971_ALC2, 7, 1, 0),
	SOC_SINGLE("ALC Capture Hold Time", WM8971_ALC2, 0, 15, 0),
	SOC_SINGLE("ALC Capture Decay Time", WM8971_ALC3, 4, 15, 0),
	SOC_SINGLE("ALC Capture Attack Time", WM8971_ALC3, 0, 15, 0),
	SOC_SINGLE("ALC Capture NG Threshold", WM8971_NGATE, 3, 31, 0),
	SOC_ENUM("ALC Capture NG Type", wm8971_enum[4]),
	SOC_SINGLE("ALC Capture NG Switch", WM8971_NGATE, 0, 1, 0),

	SOC_SINGLE("Capture 6dB Attenuate", WM8971_ADCDAC, 8, 1, 0),
	SOC_SINGLE("Playback 6dB Attenuate", WM8971_ADCDAC, 7, 1, 0),

	SOC_ENUM("Playback De-emphasis", wm8971_enum[5]),
	SOC_ENUM("Playback Function", wm8971_enum[6]),
	SOC_ENUM("Playback Phase", wm8971_enum[7]),

	SOC_DOUBLE_R("Mic Boost", WM8971_LADCIN, WM8971_RADCIN, 4, 3, 0),
};

/*
 * DAPM Controls
 */

/* Left Mixer */
static const struct snd_kcontrol_new wm8971_left_mixer_controls[] = {
SOC_DAPM_SINGLE("Playback Switch", WM8971_LOUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8971_LOUTM1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8971_LOUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8971_LOUTM2, 7, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new wm8971_right_mixer_controls[] = {
SOC_DAPM_SINGLE("Left Playback Switch", WM8971_ROUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8971_ROUTM1, 7, 1, 0),
SOC_DAPM_SINGLE("Playback Switch", WM8971_ROUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8971_ROUTM2, 7, 1, 0),
};

/* Mono Mixer */
static const struct snd_kcontrol_new wm8971_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("Left Playback Switch", WM8971_MOUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8971_MOUTM1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8971_MOUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8971_MOUTM2, 7, 1, 0),
};

/* Left Line Mux */
static const struct snd_kcontrol_new wm8971_left_line_controls =
SOC_DAPM_ENUM("Route", wm8971_enum[8]);

/* Right Line Mux */
static const struct snd_kcontrol_new wm8971_right_line_controls =
SOC_DAPM_ENUM("Route", wm8971_enum[9]);

/* Left PGA Mux */
static const struct snd_kcontrol_new wm8971_left_pga_controls =
SOC_DAPM_ENUM("Route", wm8971_enum[10]);

/* Right PGA Mux */
static const struct snd_kcontrol_new wm8971_right_pga_controls =
SOC_DAPM_ENUM("Route", wm8971_enum[11]);

/* Mono ADC Mux */
static const struct snd_kcontrol_new wm8971_monomux_controls =
SOC_DAPM_ENUM("Route", wm8971_enum[13]);

static const struct snd_soc_dapm_widget wm8971_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
		&wm8971_left_mixer_controls[0],
		ARRAY_SIZE(wm8971_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
		&wm8971_right_mixer_controls[0],
		ARRAY_SIZE(wm8971_right_mixer_controls)),
	SND_SOC_DAPM_MIXER("Mono Mixer", WM8971_PWR2, 2, 0,
		&wm8971_mono_mixer_controls[0],
		ARRAY_SIZE(wm8971_mono_mixer_controls)),

	SND_SOC_DAPM_PGA("Right Out 2", WM8971_PWR2, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", WM8971_PWR2, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", WM8971_PWR2, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", WM8971_PWR2, 6, 0, NULL, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8971_PWR2, 7, 0),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8971_PWR2, 8, 0),
	SND_SOC_DAPM_PGA("Mono Out 1", WM8971_PWR2, 2, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Mic Bias", WM8971_PWR1, 1, 0, NULL, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8971_PWR1, 2, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8971_PWR1, 3, 0),

	SND_SOC_DAPM_MUX("Left PGA Mux", WM8971_PWR1, 5, 0,
		&wm8971_left_pga_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", WM8971_PWR1, 4, 0,
		&wm8971_right_pga_controls),
	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8971_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8971_right_line_controls),

	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8971_monomux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8971_monomux_controls),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("MONO"),

	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("MIC"),
};

static const struct snd_soc_dapm_route wm8971_dapm_routes[] = {
	/* left mixer */
	{"Left Mixer", "Playback Switch", "Left DAC"},
	{"Left Mixer", "Left Bypass Switch", "Left Line Mux"},
	{"Left Mixer", "Right Playback Switch", "Right DAC"},
	{"Left Mixer", "Right Bypass Switch", "Right Line Mux"},

	/* right mixer */
	{"Right Mixer", "Left Playback Switch", "Left DAC"},
	{"Right Mixer", "Left Bypass Switch", "Left Line Mux"},
	{"Right Mixer", "Playback Switch", "Right DAC"},
	{"Right Mixer", "Right Bypass Switch", "Right Line Mux"},

	/* left out 1 */
	{"Left Out 1", NULL, "Left Mixer"},
	{"LOUT1", NULL, "Left Out 1"},

	/* left out 2 */
	{"Left Out 2", NULL, "Left Mixer"},
	{"LOUT2", NULL, "Left Out 2"},

	/* right out 1 */
	{"Right Out 1", NULL, "Right Mixer"},
	{"ROUT1", NULL, "Right Out 1"},

	/* right out 2 */
	{"Right Out 2", NULL, "Right Mixer"},
	{"ROUT2", NULL, "Right Out 2"},

	/* mono mixer */
	{"Mono Mixer", "Left Playback Switch", "Left DAC"},
	{"Mono Mixer", "Left Bypass Switch", "Left Line Mux"},
	{"Mono Mixer", "Right Playback Switch", "Right DAC"},
	{"Mono Mixer", "Right Bypass Switch", "Right Line Mux"},

	/* mono out */
	{"Mono Out", NULL, "Mono Mixer"},
	{"MONO1", NULL, "Mono Out"},

	/* Left Line Mux */
	{"Left Line Mux", "Line", "LINPUT1"},
	{"Left Line Mux", "PGA", "Left PGA Mux"},
	{"Left Line Mux", "Differential", "Differential Mux"},

	/* Right Line Mux */
	{"Right Line Mux", "Line", "RINPUT1"},
	{"Right Line Mux", "Mic", "MIC"},
	{"Right Line Mux", "PGA", "Right PGA Mux"},
	{"Right Line Mux", "Differential", "Differential Mux"},

	/* Left PGA Mux */
	{"Left PGA Mux", "Line", "LINPUT1"},
	{"Left PGA Mux", "Differential", "Differential Mux"},

	/* Right PGA Mux */
	{"Right PGA Mux", "Line", "RINPUT1"},
	{"Right PGA Mux", "Differential", "Differential Mux"},

	/* Differential Mux */
	{"Differential Mux", "Line", "LINPUT1"},
	{"Differential Mux", "Line", "RINPUT1"},

	/* Left ADC Mux */
	{"Left ADC Mux", "Stereo", "Left PGA Mux"},
	{"Left ADC Mux", "Mono (Left)", "Left PGA Mux"},
	{"Left ADC Mux", "Digital Mono", "Left PGA Mux"},

	/* Right ADC Mux */
	{"Right ADC Mux", "Stereo", "Right PGA Mux"},
	{"Right ADC Mux", "Mono (Right)", "Right PGA Mux"},
	{"Right ADC Mux", "Digital Mono", "Right PGA Mux"},

	/* ADC */
	{"Left ADC", NULL, "Left ADC Mux"},
	{"Right ADC", NULL, "Right ADC Mux"},
};

struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u8 sr:5;
	u8 usb:1;
};

/* codec hifi mclk clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{12288000, 8000, 1536, 0x6, 0x0},
	{11289600, 8000, 1408, 0x16, 0x0},
	{18432000, 8000, 2304, 0x7, 0x0},
	{16934400, 8000, 2112, 0x17, 0x0},
	{12000000, 8000, 1500, 0x6, 0x1},

	/* 11.025k */
	{11289600, 11025, 1024, 0x18, 0x0},
	{16934400, 11025, 1536, 0x19, 0x0},
	{12000000, 11025, 1088, 0x19, 0x1},

	/* 16k */
	{12288000, 16000, 768, 0xa, 0x0},
	{18432000, 16000, 1152, 0xb, 0x0},
	{12000000, 16000, 750, 0xa, 0x1},

	/* 22.05k */
	{11289600, 22050, 512, 0x1a, 0x0},
	{16934400, 22050, 768, 0x1b, 0x0},
	{12000000, 22050, 544, 0x1b, 0x1},

	/* 32k */
	{12288000, 32000, 384, 0xc, 0x0},
	{18432000, 32000, 576, 0xd, 0x0},
	{12000000, 32000, 375, 0xa, 0x1},

	/* 44.1k */
	{11289600, 44100, 256, 0x10, 0x0},
	{16934400, 44100, 384, 0x11, 0x0},
	{12000000, 44100, 272, 0x11, 0x1},

	/* 48k */
	{12288000, 48000, 256, 0x0, 0x0},
	{18432000, 48000, 384, 0x1, 0x0},
	{12000000, 48000, 250, 0x0, 0x1},

	/* 88.2k */
	{11289600, 88200, 128, 0x1e, 0x0},
	{16934400, 88200, 192, 0x1f, 0x0},
	{12000000, 88200, 136, 0x1f, 0x1},

	/* 96k */
	{12288000, 96000, 128, 0xe, 0x0},
	{18432000, 96000, 192, 0xf, 0x0},
	{12000000, 96000, 125, 0xe, 0x1},
};

static int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}
	return -EINVAL;
}

static int wm8971_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8971_priv *wm8971 = snd_soc_codec_get_drvdata(codec);

	switch (freq) {
	case 11289600:
	case 12000000:
	case 12288000:
	case 16934400:
	case 18432000:
		wm8971->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int wm8971_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = 0x0040;
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
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x0013;
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

	snd_soc_write(codec, WM8971_IFACE, iface);
	return 0;
}

static int wm8971_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8971_priv *wm8971 = snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec, WM8971_IFACE) & 0x1f3;
	u16 srate = snd_soc_read(codec, WM8971_SRATE) & 0x1c0;
	int coeff = get_coeff(wm8971->sysclk, params_rate(params));

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
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x000c;
		break;
	}

	/* set iface & srate */
	snd_soc_write(codec, WM8971_IFACE, iface);
	if (coeff >= 0)
		snd_soc_write(codec, WM8971_SRATE, srate |
			(coeff_div[coeff].sr << 1) | coeff_div[coeff].usb);

	return 0;
}

static int wm8971_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, WM8971_ADCDAC) & 0xfff7;

	if (mute)
		snd_soc_write(codec, WM8971_ADCDAC, mute_reg | 0x8);
	else
		snd_soc_write(codec, WM8971_ADCDAC, mute_reg);
	return 0;
}

static int wm8971_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	u16 pwr_reg = snd_soc_read(codec, WM8971_PWR1) & 0xfe3e;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* set vmid to 50k and unmute dac */
		snd_soc_write(codec, WM8971_PWR1, pwr_reg | 0x00c1);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF)
			snd_soc_cache_sync(codec);

		/* mute dac and set vmid to 500k, enable VREF */
		snd_soc_write(codec, WM8971_PWR1, pwr_reg | 0x0140);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, WM8971_PWR1, 0x0001);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define WM8971_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define WM8971_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8971_dai_ops = {
	.hw_params	= wm8971_pcm_hw_params,
	.digital_mute	= wm8971_mute,
	.set_fmt	= wm8971_set_dai_fmt,
	.set_sysclk	= wm8971_set_dai_sysclk,
};

static struct snd_soc_dai_driver wm8971_dai = {
	.name = "wm8971-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8971_RATES,
		.formats = WM8971_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8971_RATES,
		.formats = WM8971_FORMATS,},
	.ops = &wm8971_dai_ops,
};

static void wm8971_work(struct work_struct *work)
{
	struct snd_soc_dapm_context *dapm =
		container_of(work, struct snd_soc_dapm_context,
			     delayed_work.work);
	struct snd_soc_codec *codec = dapm->codec;
	wm8971_set_bias_level(codec, codec->dapm.bias_level);
}

static int wm8971_suspend(struct snd_soc_codec *codec)
{
	wm8971_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8971_resume(struct snd_soc_codec *codec)
{
	u16 reg;

	wm8971_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* charge wm8971 caps */
	if (codec->dapm.suspend_bias_level == SND_SOC_BIAS_ON) {
		reg = snd_soc_read(codec, WM8971_PWR1) & 0xfe3e;
		snd_soc_write(codec, WM8971_PWR1, reg | 0x01c0);
		codec->dapm.bias_level = SND_SOC_BIAS_ON;
		queue_delayed_work(wm8971_workq, &codec->dapm.delayed_work,
			msecs_to_jiffies(1000));
	}

	return 0;
}

static int wm8971_probe(struct snd_soc_codec *codec)
{
	int ret = 0;
	u16 reg;

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, SND_SOC_REGMAP);
	if (ret < 0) {
		printk(KERN_ERR "wm8971: failed to set cache I/O: %d\n", ret);
		return ret;
	}

	INIT_DELAYED_WORK(&codec->dapm.delayed_work, wm8971_work);
	wm8971_workq = create_workqueue("wm8971");
	if (wm8971_workq == NULL)
		return -ENOMEM;

	wm8971_reset(codec);

	/* charge output caps - set vmid to 5k for quick power up */
	reg = snd_soc_read(codec, WM8971_PWR1) & 0xfe3e;
	snd_soc_write(codec, WM8971_PWR1, reg | 0x01c0);
	codec->dapm.bias_level = SND_SOC_BIAS_STANDBY;
	queue_delayed_work(wm8971_workq, &codec->dapm.delayed_work,
		msecs_to_jiffies(1000));

	/* set the update bits */
	snd_soc_update_bits(codec, WM8971_LDAC, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8971_RDAC, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8971_LOUT1V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8971_ROUT1V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8971_LOUT2V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8971_ROUT2V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8971_LINVOL, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8971_RINVOL, 0x0100, 0x0100);

	return ret;
}


/* power down chip */
static int wm8971_remove(struct snd_soc_codec *codec)
{
	wm8971_set_bias_level(codec, SND_SOC_BIAS_OFF);

	if (wm8971_workq)
		destroy_workqueue(wm8971_workq);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8971 = {
	.probe =	wm8971_probe,
	.remove =	wm8971_remove,
	.suspend =	wm8971_suspend,
	.resume =	wm8971_resume,
	.set_bias_level = wm8971_set_bias_level,

	.controls = wm8971_snd_controls,
	.num_controls = ARRAY_SIZE(wm8971_snd_controls),
	.dapm_widgets = wm8971_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8971_dapm_widgets),
	.dapm_routes = wm8971_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm8971_dapm_routes),
};

static const struct regmap_config wm8971_regmap = {
	.reg_bits = 7,
	.val_bits = 9,
	.max_register = WM8971_MOUTV,

	.reg_defaults = wm8971_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8971_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static __devinit int wm8971_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8971_priv *wm8971;
	struct regmap *regmap;
	int ret;

	wm8971 = devm_kzalloc(&i2c->dev, sizeof(struct wm8971_priv),
			      GFP_KERNEL);
	if (wm8971 == NULL)
		return -ENOMEM;

	regmap = devm_regmap_init_i2c(i2c, &wm8971_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	i2c_set_clientdata(i2c, wm8971);

	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8971, &wm8971_dai, 1);

	return ret;
}

static __devexit int wm8971_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id wm8971_i2c_id[] = {
	{ "wm8971", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8971_i2c_id);

static struct i2c_driver wm8971_i2c_driver = {
	.driver = {
		.name = "wm8971",
		.owner = THIS_MODULE,
	},
	.probe =    wm8971_i2c_probe,
	.remove =   __devexit_p(wm8971_i2c_remove),
	.id_table = wm8971_i2c_id,
};

module_i2c_driver(wm8971_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8971 driver");
MODULE_AUTHOR("Lab126");
MODULE_LICENSE("GPL");
