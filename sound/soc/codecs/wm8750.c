/*
 * wm8750.c -- WM8750 ALSA SoC audio driver
 *
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Richard Purdie <richard@openedhand.com>
 *
 * Based on WM8753.c
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
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "wm8750.h"

/*
 * wm8750 register cache
 * We can't read the WM8750 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const struct reg_default wm8750_reg_defaults[] = {
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

/* codec private data */
struct wm8750_priv {
	unsigned int sysclk;
};

#define wm8750_reset(c)	snd_soc_write(c, WM8750_RESET, 0)

/*
 * WM8750 Controls
 */
static const char *wm8750_bass[] = {"Linear Control", "Adaptive Boost"};
static const char *wm8750_bass_filter[] = { "130Hz @ 48kHz", "200Hz @ 48kHz" };
static const char *wm8750_treble[] = {"8kHz", "4kHz"};
static const char *wm8750_3d_lc[] = {"200Hz", "500Hz"};
static const char *wm8750_3d_uc[] = {"2.2kHz", "1.5kHz"};
static const char *wm8750_3d_func[] = {"Capture", "Playback"};
static const char *wm8750_alc_func[] = {"Off", "Right", "Left", "Stereo"};
static const char *wm8750_ng_type[] = {"Constant PGA Gain",
	"Mute ADC Output"};
static const char *wm8750_line_mux[] = {"Line 1", "Line 2", "Line 3", "PGA",
	"Differential"};
static const char *wm8750_pga_sel[] = {"Line 1", "Line 2", "Line 3",
	"Differential"};
static const char *wm8750_out3[] = {"VREF", "ROUT1 + Vol", "MonoOut",
	"ROUT1"};
static const char *wm8750_diff_sel[] = {"Line 1", "Line 2"};
static const char *wm8750_adcpol[] = {"Normal", "L Invert", "R Invert",
	"L + R Invert"};
static const char *wm8750_deemph[] = {"None", "32Khz", "44.1Khz", "48Khz"};
static const char *wm8750_mono_mux[] = {"Stereo", "Mono (Left)",
	"Mono (Right)", "Digital Mono"};

static const struct soc_enum wm8750_enum[] = {
SOC_ENUM_SINGLE(WM8750_BASS, 7, 2, wm8750_bass),
SOC_ENUM_SINGLE(WM8750_BASS, 6, 2, wm8750_bass_filter),
SOC_ENUM_SINGLE(WM8750_TREBLE, 6, 2, wm8750_treble),
SOC_ENUM_SINGLE(WM8750_3D, 5, 2, wm8750_3d_lc),
SOC_ENUM_SINGLE(WM8750_3D, 6, 2, wm8750_3d_uc),
SOC_ENUM_SINGLE(WM8750_3D, 7, 2, wm8750_3d_func),
SOC_ENUM_SINGLE(WM8750_ALC1, 7, 4, wm8750_alc_func),
SOC_ENUM_SINGLE(WM8750_NGATE, 1, 2, wm8750_ng_type),
SOC_ENUM_SINGLE(WM8750_LOUTM1, 0, 5, wm8750_line_mux),
SOC_ENUM_SINGLE(WM8750_ROUTM1, 0, 5, wm8750_line_mux),
SOC_ENUM_SINGLE(WM8750_LADCIN, 6, 4, wm8750_pga_sel), /* 10 */
SOC_ENUM_SINGLE(WM8750_RADCIN, 6, 4, wm8750_pga_sel),
SOC_ENUM_SINGLE(WM8750_ADCTL2, 7, 4, wm8750_out3),
SOC_ENUM_SINGLE(WM8750_ADCIN, 8, 2, wm8750_diff_sel),
SOC_ENUM_SINGLE(WM8750_ADCDAC, 5, 4, wm8750_adcpol),
SOC_ENUM_SINGLE(WM8750_ADCDAC, 1, 4, wm8750_deemph),
SOC_ENUM_SINGLE(WM8750_ADCIN, 6, 4, wm8750_mono_mux), /* 16 */

};

static const struct snd_kcontrol_new wm8750_snd_controls[] = {

SOC_DOUBLE_R("Capture Volume", WM8750_LINVOL, WM8750_RINVOL, 0, 63, 0),
SOC_DOUBLE_R("Capture ZC Switch", WM8750_LINVOL, WM8750_RINVOL, 6, 1, 0),
SOC_DOUBLE_R("Capture Switch", WM8750_LINVOL, WM8750_RINVOL, 7, 1, 1),

SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8750_LOUT1V,
	WM8750_ROUT1V, 7, 1, 0),
SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8750_LOUT2V,
	WM8750_ROUT2V, 7, 1, 0),

SOC_ENUM("Playback De-emphasis", wm8750_enum[15]),

SOC_ENUM("Capture Polarity", wm8750_enum[14]),
SOC_SINGLE("Playback 6dB Attenuate", WM8750_ADCDAC, 7, 1, 0),
SOC_SINGLE("Capture 6dB Attenuate", WM8750_ADCDAC, 8, 1, 0),

SOC_DOUBLE_R("PCM Volume", WM8750_LDAC, WM8750_RDAC, 0, 255, 0),

SOC_ENUM("Bass Boost", wm8750_enum[0]),
SOC_ENUM("Bass Filter", wm8750_enum[1]),
SOC_SINGLE("Bass Volume", WM8750_BASS, 0, 15, 1),

SOC_SINGLE("Treble Volume", WM8750_TREBLE, 0, 15, 1),
SOC_ENUM("Treble Cut-off", wm8750_enum[2]),

SOC_SINGLE("3D Switch", WM8750_3D, 0, 1, 0),
SOC_SINGLE("3D Volume", WM8750_3D, 1, 15, 0),
SOC_ENUM("3D Lower Cut-off", wm8750_enum[3]),
SOC_ENUM("3D Upper Cut-off", wm8750_enum[4]),
SOC_ENUM("3D Mode", wm8750_enum[5]),

SOC_SINGLE("ALC Capture Target Volume", WM8750_ALC1, 0, 7, 0),
SOC_SINGLE("ALC Capture Max Volume", WM8750_ALC1, 4, 7, 0),
SOC_ENUM("ALC Capture Function", wm8750_enum[6]),
SOC_SINGLE("ALC Capture ZC Switch", WM8750_ALC2, 7, 1, 0),
SOC_SINGLE("ALC Capture Hold Time", WM8750_ALC2, 0, 15, 0),
SOC_SINGLE("ALC Capture Decay Time", WM8750_ALC3, 4, 15, 0),
SOC_SINGLE("ALC Capture Attack Time", WM8750_ALC3, 0, 15, 0),
SOC_SINGLE("ALC Capture NG Threshold", WM8750_NGATE, 3, 31, 0),
SOC_ENUM("ALC Capture NG Type", wm8750_enum[4]),
SOC_SINGLE("ALC Capture NG Switch", WM8750_NGATE, 0, 1, 0),

SOC_SINGLE("Left ADC Capture Volume", WM8750_LADC, 0, 255, 0),
SOC_SINGLE("Right ADC Capture Volume", WM8750_RADC, 0, 255, 0),

SOC_SINGLE("ZC Timeout Switch", WM8750_ADCTL1, 0, 1, 0),
SOC_SINGLE("Playback Invert Switch", WM8750_ADCTL1, 1, 1, 0),

SOC_SINGLE("Right Speaker Playback Invert Switch", WM8750_ADCTL2, 4, 1, 0),

/* Unimplemented */
/* ADCDAC Bit 0 - ADCHPD */
/* ADCDAC Bit 4 - HPOR */
/* ADCTL1 Bit 2,3 - DATSEL */
/* ADCTL1 Bit 4,5 - DMONOMIX */
/* ADCTL1 Bit 6,7 - VSEL */
/* ADCTL2 Bit 2 - LRCM */
/* ADCTL2 Bit 3 - TRI */
/* ADCTL3 Bit 5 - HPFLREN */
/* ADCTL3 Bit 6 - VROI */
/* ADCTL3 Bit 7,8 - ADCLRM */
/* ADCIN Bit 4 - LDCM */
/* ADCIN Bit 5 - RDCM */

SOC_DOUBLE_R("Mic Boost", WM8750_LADCIN, WM8750_RADCIN, 4, 3, 0),

SOC_DOUBLE_R("Bypass Left Playback Volume", WM8750_LOUTM1,
	WM8750_LOUTM2, 4, 7, 1),
SOC_DOUBLE_R("Bypass Right Playback Volume", WM8750_ROUTM1,
	WM8750_ROUTM2, 4, 7, 1),
SOC_DOUBLE_R("Bypass Mono Playback Volume", WM8750_MOUTM1,
	WM8750_MOUTM2, 4, 7, 1),

SOC_SINGLE("Mono Playback ZC Switch", WM8750_MOUTV, 7, 1, 0),

SOC_DOUBLE_R("Headphone Playback Volume", WM8750_LOUT1V, WM8750_ROUT1V,
	0, 127, 0),
SOC_DOUBLE_R("Speaker Playback Volume", WM8750_LOUT2V, WM8750_ROUT2V,
	0, 127, 0),

SOC_SINGLE("Mono Playback Volume", WM8750_MOUTV, 0, 127, 0),

};

/*
 * DAPM Controls
 */

/* Left Mixer */
static const struct snd_kcontrol_new wm8750_left_mixer_controls[] = {
SOC_DAPM_SINGLE("Playback Switch", WM8750_LOUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8750_LOUTM1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8750_LOUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8750_LOUTM2, 7, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new wm8750_right_mixer_controls[] = {
SOC_DAPM_SINGLE("Left Playback Switch", WM8750_ROUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8750_ROUTM1, 7, 1, 0),
SOC_DAPM_SINGLE("Playback Switch", WM8750_ROUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8750_ROUTM2, 7, 1, 0),
};

/* Mono Mixer */
static const struct snd_kcontrol_new wm8750_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("Left Playback Switch", WM8750_MOUTM1, 8, 1, 0),
SOC_DAPM_SINGLE("Left Bypass Switch", WM8750_MOUTM1, 7, 1, 0),
SOC_DAPM_SINGLE("Right Playback Switch", WM8750_MOUTM2, 8, 1, 0),
SOC_DAPM_SINGLE("Right Bypass Switch", WM8750_MOUTM2, 7, 1, 0),
};

/* Left Line Mux */
static const struct snd_kcontrol_new wm8750_left_line_controls =
SOC_DAPM_ENUM("Route", wm8750_enum[8]);

/* Right Line Mux */
static const struct snd_kcontrol_new wm8750_right_line_controls =
SOC_DAPM_ENUM("Route", wm8750_enum[9]);

/* Left PGA Mux */
static const struct snd_kcontrol_new wm8750_left_pga_controls =
SOC_DAPM_ENUM("Route", wm8750_enum[10]);

/* Right PGA Mux */
static const struct snd_kcontrol_new wm8750_right_pga_controls =
SOC_DAPM_ENUM("Route", wm8750_enum[11]);

/* Out 3 Mux */
static const struct snd_kcontrol_new wm8750_out3_controls =
SOC_DAPM_ENUM("Route", wm8750_enum[12]);

/* Differential Mux */
static const struct snd_kcontrol_new wm8750_diffmux_controls =
SOC_DAPM_ENUM("Route", wm8750_enum[13]);

/* Mono ADC Mux */
static const struct snd_kcontrol_new wm8750_monomux_controls =
SOC_DAPM_ENUM("Route", wm8750_enum[16]);

static const struct snd_soc_dapm_widget wm8750_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
		&wm8750_left_mixer_controls[0],
		ARRAY_SIZE(wm8750_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
		&wm8750_right_mixer_controls[0],
		ARRAY_SIZE(wm8750_right_mixer_controls)),
	SND_SOC_DAPM_MIXER("Mono Mixer", WM8750_PWR2, 2, 0,
		&wm8750_mono_mixer_controls[0],
		ARRAY_SIZE(wm8750_mono_mixer_controls)),

	SND_SOC_DAPM_PGA("Right Out 2", WM8750_PWR2, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", WM8750_PWR2, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", WM8750_PWR2, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", WM8750_PWR2, 6, 0, NULL, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8750_PWR2, 7, 0),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8750_PWR2, 8, 0),

	SND_SOC_DAPM_MICBIAS("Mic Bias", WM8750_PWR1, 1, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8750_PWR1, 2, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8750_PWR1, 3, 0),

	SND_SOC_DAPM_MUX("Left PGA Mux", WM8750_PWR1, 5, 0,
		&wm8750_left_pga_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", WM8750_PWR1, 4, 0,
		&wm8750_right_pga_controls),
	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8750_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8750_right_line_controls),

	SND_SOC_DAPM_MUX("Out3 Mux", SND_SOC_NOPM, 0, 0, &wm8750_out3_controls),
	SND_SOC_DAPM_PGA("Out 3", WM8750_PWR2, 1, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Mono Out 1", WM8750_PWR2, 2, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
		&wm8750_diffmux_controls),
	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8750_monomux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8750_monomux_controls),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("MONO1"),
	SND_SOC_DAPM_OUTPUT("OUT3"),
	SND_SOC_DAPM_OUTPUT("VREF"),

	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("LINPUT3"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT3"),
};

static const struct snd_soc_dapm_route wm8750_dapm_routes[] = {
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
	{"Mono Out 1", NULL, "Mono Mixer"},
	{"MONO1", NULL, "Mono Out 1"},

	/* out 3 */
	{"Out3 Mux", "VREF", "VREF"},
	{"Out3 Mux", "ROUT1 + Vol", "ROUT1"},
	{"Out3 Mux", "ROUT1", "Right Mixer"},
	{"Out3 Mux", "MonoOut", "MONO1"},
	{"Out 3", NULL, "Out3 Mux"},
	{"OUT3", NULL, "Out 3"},

	/* Left Line Mux */
	{"Left Line Mux", "Line 1", "LINPUT1"},
	{"Left Line Mux", "Line 2", "LINPUT2"},
	{"Left Line Mux", "Line 3", "LINPUT3"},
	{"Left Line Mux", "PGA", "Left PGA Mux"},
	{"Left Line Mux", "Differential", "Differential Mux"},

	/* Right Line Mux */
	{"Right Line Mux", "Line 1", "RINPUT1"},
	{"Right Line Mux", "Line 2", "RINPUT2"},
	{"Right Line Mux", "Line 3", "RINPUT3"},
	{"Right Line Mux", "PGA", "Right PGA Mux"},
	{"Right Line Mux", "Differential", "Differential Mux"},

	/* Left PGA Mux */
	{"Left PGA Mux", "Line 1", "LINPUT1"},
	{"Left PGA Mux", "Line 2", "LINPUT2"},
	{"Left PGA Mux", "Line 3", "LINPUT3"},
	{"Left PGA Mux", "Differential", "Differential Mux"},

	/* Right PGA Mux */
	{"Right PGA Mux", "Line 1", "RINPUT1"},
	{"Right PGA Mux", "Line 2", "RINPUT2"},
	{"Right PGA Mux", "Line 3", "RINPUT3"},
	{"Right PGA Mux", "Differential", "Differential Mux"},

	/* Differential Mux */
	{"Differential Mux", "Line 1", "LINPUT1"},
	{"Differential Mux", "Line 1", "RINPUT1"},
	{"Differential Mux", "Line 2", "LINPUT2"},
	{"Differential Mux", "Line 2", "RINPUT2"},

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

static inline int get_coeff(int mclk, int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}

	printk(KERN_ERR "wm8750: could not get coeff for mclk %d @ rate %d\n",
		mclk, rate);
	return -EINVAL;
}

static int wm8750_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8750_priv *wm8750 = snd_soc_codec_get_drvdata(codec);

	switch (freq) {
	case 11289600:
	case 12000000:
	case 12288000:
	case 16934400:
	case 18432000:
		wm8750->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int wm8750_set_dai_fmt(struct snd_soc_dai *codec_dai,
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

	snd_soc_write(codec, WM8750_IFACE, iface);
	return 0;
}

static int wm8750_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct wm8750_priv *wm8750 = snd_soc_codec_get_drvdata(codec);
	u16 iface = snd_soc_read(codec, WM8750_IFACE) & 0x1f3;
	u16 srate = snd_soc_read(codec, WM8750_SRATE) & 0x1c0;
	int coeff = get_coeff(wm8750->sysclk, params_rate(params));

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
	snd_soc_write(codec, WM8750_IFACE, iface);
	if (coeff >= 0)
		snd_soc_write(codec, WM8750_SRATE, srate |
			(coeff_div[coeff].sr << 1) | coeff_div[coeff].usb);

	return 0;
}

static int wm8750_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = snd_soc_read(codec, WM8750_ADCDAC) & 0xfff7;

	if (mute)
		snd_soc_write(codec, WM8750_ADCDAC, mute_reg | 0x8);
	else
		snd_soc_write(codec, WM8750_ADCDAC, mute_reg);
	return 0;
}

static int wm8750_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	u16 pwr_reg = snd_soc_read(codec, WM8750_PWR1) & 0xfe3e;

	switch (level) {
	case SND_SOC_BIAS_ON:
		/* set vmid to 50k and unmute dac */
		snd_soc_write(codec, WM8750_PWR1, pwr_reg | 0x00c0);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			snd_soc_cache_sync(codec);

			/* Set VMID to 5k */
			snd_soc_write(codec, WM8750_PWR1, pwr_reg | 0x01c1);

			/* ...and ramp */
			msleep(1000);
		}

		/* mute dac and set vmid to 500k, enable VREF */
		snd_soc_write(codec, WM8750_PWR1, pwr_reg | 0x0141);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, WM8750_PWR1, 0x0001);
		break;
	}
	codec->dapm.bias_level = level;
	return 0;
}

#define WM8750_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
	SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define WM8750_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8750_dai_ops = {
	.hw_params	= wm8750_pcm_hw_params,
	.digital_mute	= wm8750_mute,
	.set_fmt	= wm8750_set_dai_fmt,
	.set_sysclk	= wm8750_set_dai_sysclk,
};

static struct snd_soc_dai_driver wm8750_dai = {
	.name = "wm8750-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8750_RATES,
		.formats = WM8750_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8750_RATES,
		.formats = WM8750_FORMATS,},
	.ops = &wm8750_dai_ops,
};

static int wm8750_suspend(struct snd_soc_codec *codec)
{
	wm8750_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8750_resume(struct snd_soc_codec *codec)
{
	wm8750_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

static int wm8750_probe(struct snd_soc_codec *codec)
{
	int ret;

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, SND_SOC_REGMAP);
	if (ret < 0) {
		printk(KERN_ERR "wm8750: failed to set cache I/O: %d\n", ret);
		return ret;
	}

	ret = wm8750_reset(codec);
	if (ret < 0) {
		printk(KERN_ERR "wm8750: failed to reset: %d\n", ret);
		return ret;
	}

	/* charge output caps */
	wm8750_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	/* set the update bits */
	snd_soc_update_bits(codec, WM8750_LDAC, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8750_RDAC, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8750_LOUT1V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8750_ROUT1V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8750_LOUT2V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8750_ROUT2V, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8750_LINVOL, 0x0100, 0x0100);
	snd_soc_update_bits(codec, WM8750_RINVOL, 0x0100, 0x0100);

	return ret;
}

static int wm8750_remove(struct snd_soc_codec *codec)
{
	wm8750_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8750 = {
	.probe =	wm8750_probe,
	.remove =	wm8750_remove,
	.suspend =	wm8750_suspend,
	.resume =	wm8750_resume,
	.set_bias_level = wm8750_set_bias_level,

	.controls = wm8750_snd_controls,
	.num_controls = ARRAY_SIZE(wm8750_snd_controls),
	.dapm_widgets = wm8750_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8750_dapm_widgets),
	.dapm_routes = wm8750_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm8750_dapm_routes),
};

static const struct of_device_id wm8750_of_match[] = {
	{ .compatible = "wlf,wm8750", },
	{ .compatible = "wlf,wm8987", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8750_of_match);

static const struct regmap_config wm8750_regmap = {
	.reg_bits = 7,
	.val_bits = 9,
	.max_register = WM8750_MOUTV,

	.reg_defaults = wm8750_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8750_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

#if defined(CONFIG_SPI_MASTER)
static int __devinit wm8750_spi_probe(struct spi_device *spi)
{
	struct wm8750_priv *wm8750;
	struct regmap *regmap;
	int ret;

	wm8750 = devm_kzalloc(&spi->dev, sizeof(struct wm8750_priv),
			      GFP_KERNEL);
	if (wm8750 == NULL)
		return -ENOMEM;

	regmap = devm_regmap_init_spi(spi, &wm8750_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	spi_set_drvdata(spi, wm8750);

	ret = snd_soc_register_codec(&spi->dev,
			&soc_codec_dev_wm8750, &wm8750_dai, 1);
	return ret;
}

static int __devexit wm8750_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	return 0;
}

static const struct spi_device_id wm8750_spi_ids[] = {
	{ "wm8750", 0 },
	{ "wm8987", 0 },
	{ },
};
MODULE_DEVICE_TABLE(spi, wm8750_spi_ids);

static struct spi_driver wm8750_spi_driver = {
	.driver = {
		.name	= "wm8750",
		.owner	= THIS_MODULE,
		.of_match_table = wm8750_of_match,
	},
	.id_table	= wm8750_spi_ids,
	.probe		= wm8750_spi_probe,
	.remove		= __devexit_p(wm8750_spi_remove),
};
#endif /* CONFIG_SPI_MASTER */

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8750_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8750_priv *wm8750;
	struct regmap *regmap;
	int ret;

	wm8750 = devm_kzalloc(&i2c->dev, sizeof(struct wm8750_priv),
			      GFP_KERNEL);
	if (wm8750 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8750);

	regmap = devm_regmap_init_i2c(i2c, &wm8750_regmap);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret =  snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_wm8750, &wm8750_dai, 1);
	return ret;
}

static __devexit int wm8750_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id wm8750_i2c_id[] = {
	{ "wm8750", 0 },
	{ "wm8987", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8750_i2c_id);

static struct i2c_driver wm8750_i2c_driver = {
	.driver = {
		.name = "wm8750",
		.owner = THIS_MODULE,
		.of_match_table = wm8750_of_match,
	},
	.probe =    wm8750_i2c_probe,
	.remove =   __devexit_p(wm8750_i2c_remove),
	.id_table = wm8750_i2c_id,
};
#endif

static int __init wm8750_modinit(void)
{
	int ret = 0;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8750_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8750 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8750_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8750 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8750_modinit);

static void __exit wm8750_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8750_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8750_spi_driver);
#endif
}
module_exit(wm8750_exit);

MODULE_DESCRIPTION("ASoC WM8750 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
