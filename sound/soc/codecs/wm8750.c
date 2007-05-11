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
#include <linux/platform_device.h>
#include <sound/driver.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "wm8750.h"

#define AUDIO_NAME "WM8750"
#define WM8750_VERSION "0.12"

/*
 * Debug
 */

#define WM8750_DEBUG 0

#ifdef WM8750_DEBUG
#define dbg(format, arg...) \
	printk(KERN_DEBUG AUDIO_NAME ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif
#define err(format, arg...) \
	printk(KERN_ERR AUDIO_NAME ": " format "\n" , ## arg)
#define info(format, arg...) \
	printk(KERN_INFO AUDIO_NAME ": " format "\n" , ## arg)
#define warn(format, arg...) \
	printk(KERN_WARNING AUDIO_NAME ": " format "\n" , ## arg)

/* codec private data */
struct wm8750_priv {
	unsigned int sysclk;
};

/*
 * wm8750 register cache
 * We can't read the WM8750 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8750_reg[] = {
	0x0097, 0x0097, 0x0079, 0x0079,  /*  0 */
	0x0000, 0x0008, 0x0000, 0x000a,  /*  4 */
	0x0000, 0x0000, 0x00ff, 0x00ff,  /*  8 */
	0x000f, 0x000f, 0x0000, 0x0000,  /* 12 */
	0x0000, 0x007b, 0x0000, 0x0032,  /* 16 */
	0x0000, 0x00c3, 0x00c3, 0x00c0,  /* 20 */
	0x0000, 0x0000, 0x0000, 0x0000,  /* 24 */
	0x0000, 0x0000, 0x0000, 0x0000,  /* 28 */
	0x0000, 0x0000, 0x0050, 0x0050,  /* 32 */
	0x0050, 0x0050, 0x0050, 0x0050,  /* 36 */
	0x0079, 0x0079, 0x0079,          /* 40 */
};

/*
 * read wm8750 register cache
 */
static inline unsigned int wm8750_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg > WM8750_CACHE_REGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8750 register cache
 */
static inline void wm8750_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg > WM8750_CACHE_REGNUM)
		return;
	cache[reg] = value;
}

static int wm8750_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8753 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	wm8750_write_reg_cache (codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

#define wm8750_reset(c)	wm8750_write(c, WM8750_RESET, 0)

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

SOC_SINGLE("Treble Volume", WM8750_TREBLE, 0, 15, 0),
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

/* add non dapm controls */
static int wm8750_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(wm8750_snd_controls); i++) {
		err = snd_ctl_add(codec->card,
				snd_soc_cnew(&wm8750_snd_controls[i],codec, NULL));
		if (err < 0)
			return err;
	}
	return 0;
}

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
	SND_SOC_DAPM_OUTPUT("MONO"),
	SND_SOC_DAPM_OUTPUT("OUT3"),

	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("LINPUT3"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT3"),
};

static const char *audio_map[][3] = {
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

	/* terminator */
	{NULL, NULL, NULL},
};

static int wm8750_add_widgets(struct snd_soc_codec *codec)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(wm8750_dapm_widgets); i++) {
		snd_soc_dapm_new_control(codec, &wm8750_dapm_widgets[i]);
	}

	/* set up audio path audio_mapnects */
	for(i = 0; audio_map[i][0] != NULL; i++) {
		snd_soc_dapm_connect_input(codec, audio_map[i][0],
			audio_map[i][1], audio_map[i][2]);
	}

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

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

static int wm8750_set_dai_sysclk(struct snd_soc_codec_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8750_priv *wm8750 = codec->private_data;

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

static int wm8750_set_dai_fmt(struct snd_soc_codec_dai *codec_dai,
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

	wm8750_write(codec, WM8750_IFACE, iface);
	return 0;
}

static int wm8750_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->codec;
	struct wm8750_priv *wm8750 = codec->private_data;
	u16 iface = wm8750_read_reg_cache(codec, WM8750_IFACE) & 0x1f3;
	u16 srate = wm8750_read_reg_cache(codec, WM8750_SRATE) & 0x1c0;
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
	wm8750_write(codec, WM8750_IFACE, iface);
	if (coeff >= 0)
		wm8750_write(codec, WM8750_SRATE, srate |
			(coeff_div[coeff].sr << 1) | coeff_div[coeff].usb);

	return 0;
}

static int wm8750_mute(struct snd_soc_codec_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8750_read_reg_cache(codec, WM8750_ADCDAC) & 0xfff7;

	if (mute)
		wm8750_write(codec, WM8750_ADCDAC, mute_reg | 0x8);
	else
		wm8750_write(codec, WM8750_ADCDAC, mute_reg);
	return 0;
}

static int wm8750_dapm_event(struct snd_soc_codec *codec, int event)
{
	u16 pwr_reg = wm8750_read_reg_cache(codec, WM8750_PWR1) & 0xfe3e;

	switch (event) {
	case SNDRV_CTL_POWER_D0: /* full On */
		/* set vmid to 50k and unmute dac */
		wm8750_write(codec, WM8750_PWR1, pwr_reg | 0x00c0);
		break;
	case SNDRV_CTL_POWER_D1: /* partial On */
	case SNDRV_CTL_POWER_D2: /* partial On */
		/* set vmid to 5k for quick power up */
		wm8750_write(codec, WM8750_PWR1, pwr_reg | 0x01c1);
		break;
	case SNDRV_CTL_POWER_D3hot: /* Off, with power */
		/* mute dac and set vmid to 500k, enable VREF */
		wm8750_write(codec, WM8750_PWR1, pwr_reg | 0x0141);
		break;
	case SNDRV_CTL_POWER_D3cold: /* Off, without power */
		wm8750_write(codec, WM8750_PWR1, 0x0001);
		break;
	}
	codec->dapm_state = event;
	return 0;
}

#define WM8750_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
		SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
		SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)

#define WM8750_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

struct snd_soc_codec_dai wm8750_dai = {
	.name = "WM8750",
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
	.ops = {
		.hw_params = wm8750_pcm_hw_params,
	},
	.dai_ops = {
		.digital_mute = wm8750_mute,
		.set_fmt = wm8750_set_dai_fmt,
		.set_sysclk = wm8750_set_dai_sysclk,
	},
};
EXPORT_SYMBOL_GPL(wm8750_dai);

static void wm8750_work(struct work_struct *work)
{
	struct snd_soc_codec *codec =
		container_of(work, struct snd_soc_codec, delayed_work.work);
	wm8750_dapm_event(codec, codec->dapm_state);
}

static int wm8750_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	wm8750_dapm_event(codec, SNDRV_CTL_POWER_D3cold);
	return 0;
}

static int wm8750_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8750_reg); i++) {
		if (i == WM8750_RESET)
			continue;
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}

	wm8750_dapm_event(codec, SNDRV_CTL_POWER_D3hot);

	/* charge wm8750 caps */
	if (codec->suspend_dapm_state == SNDRV_CTL_POWER_D0) {
		wm8750_dapm_event(codec, SNDRV_CTL_POWER_D2);
		codec->dapm_state = SNDRV_CTL_POWER_D0;
		schedule_delayed_work(&codec->delayed_work, msecs_to_jiffies(1000));
	}

	return 0;
}

/*
 * initialise the WM8750 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int wm8750_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev->codec;
	int reg, ret = 0;

	codec->name = "WM8750";
	codec->owner = THIS_MODULE;
	codec->read = wm8750_read_reg_cache;
	codec->write = wm8750_write;
	codec->dapm_event = wm8750_dapm_event;
	codec->dai = &wm8750_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = sizeof(wm8750_reg);
	codec->reg_cache = kmemdup(wm8750_reg, sizeof(wm8750_reg), GFP_KERNEL);
	if (codec->reg_cache == NULL)
		return -ENOMEM;

	wm8750_reset(codec);

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "wm8750: failed to create pcms\n");
		goto pcm_err;
	}

	/* charge output caps */
	wm8750_dapm_event(codec, SNDRV_CTL_POWER_D2);
	codec->dapm_state = SNDRV_CTL_POWER_D3hot;
	schedule_delayed_work(&codec->delayed_work, msecs_to_jiffies(1000));

	/* set the update bits */
	reg = wm8750_read_reg_cache(codec, WM8750_LDAC);
	wm8750_write(codec, WM8750_LDAC, reg | 0x0100);
	reg = wm8750_read_reg_cache(codec, WM8750_RDAC);
	wm8750_write(codec, WM8750_RDAC, reg | 0x0100);
	reg = wm8750_read_reg_cache(codec, WM8750_LOUT1V);
	wm8750_write(codec, WM8750_LOUT1V, reg | 0x0100);
	reg = wm8750_read_reg_cache(codec, WM8750_ROUT1V);
	wm8750_write(codec, WM8750_ROUT1V, reg | 0x0100);
	reg = wm8750_read_reg_cache(codec, WM8750_LOUT2V);
	wm8750_write(codec, WM8750_LOUT2V, reg | 0x0100);
	reg = wm8750_read_reg_cache(codec, WM8750_ROUT2V);
	wm8750_write(codec, WM8750_ROUT2V, reg | 0x0100);
	reg = wm8750_read_reg_cache(codec, WM8750_LINVOL);
	wm8750_write(codec, WM8750_LINVOL, reg | 0x0100);
	reg = wm8750_read_reg_cache(codec, WM8750_RINVOL);
	wm8750_write(codec, WM8750_RINVOL, reg | 0x0100);

	wm8750_add_controls(codec);
	wm8750_add_widgets(codec);
	ret = snd_soc_register_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "wm8750: failed to register card\n");
		goto card_err;
	}
	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	kfree(codec->reg_cache);
	return ret;
}

/* If the i2c layer weren't so broken, we could pass this kind of data
   around */
static struct snd_soc_device *wm8750_socdev;

#if defined (CONFIG_I2C) || defined (CONFIG_I2C_MODULE)

/*
 * WM8731 2 wire address is determined by GPIO5
 * state during powerup.
 *    low  = 0x1a
 *    high = 0x1b
 */
static unsigned short normal_i2c[] = { 0, I2C_CLIENT_END };

/* Magic definition of all other variables and things */
I2C_CLIENT_INSMOD;

static struct i2c_driver wm8750_i2c_driver;
static struct i2c_client client_template;

static int wm8750_codec_probe(struct i2c_adapter *adap, int addr, int kind)
{
	struct snd_soc_device *socdev = wm8750_socdev;
	struct wm8750_setup_data *setup = socdev->codec_data;
	struct snd_soc_codec *codec = socdev->codec;
	struct i2c_client *i2c;
	int ret;

	if (addr != setup->i2c_address)
		return -ENODEV;

	client_template.adapter = adap;
	client_template.addr = addr;

	i2c = kmemdup(&client_template, sizeof(client_template), GFP_KERNEL);
	if (i2c == NULL) {
		kfree(codec);
		return -ENOMEM;
	}
	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;

	ret = i2c_attach_client(i2c);
	if (ret < 0) {
		err("failed to attach codec at addr %x\n", addr);
		goto err;
	}

	ret = wm8750_init(socdev);
	if (ret < 0) {
	err("failed to initialise WM8750\n");
		goto err;
	}
	return ret;

err:
	kfree(codec);
	kfree(i2c);
	return ret;
}

static int wm8750_i2c_detach(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);
	i2c_detach_client(client);
	kfree(codec->reg_cache);
	kfree(client);
	return 0;
}

static int wm8750_i2c_attach(struct i2c_adapter *adap)
{
	return i2c_probe(adap, &addr_data, wm8750_codec_probe);
}

/* corgi i2c codec control layer */
static struct i2c_driver wm8750_i2c_driver = {
	.driver = {
		.name = "WM8750 I2C Codec",
		.owner = THIS_MODULE,
	},
	.id =             I2C_DRIVERID_WM8750,
	.attach_adapter = wm8750_i2c_attach,
	.detach_client =  wm8750_i2c_detach,
	.command =        NULL,
};

static struct i2c_client client_template = {
	.name =   "WM8750",
	.driver = &wm8750_i2c_driver,
};
#endif

static int wm8750_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct wm8750_setup_data *setup = socdev->codec_data;
	struct snd_soc_codec *codec;
	struct wm8750_priv *wm8750;
	int ret = 0;

	info("WM8750 Audio Codec %s", WM8750_VERSION);
	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	wm8750 = kzalloc(sizeof(struct wm8750_priv), GFP_KERNEL);
	if (wm8750 == NULL) {
		kfree(codec);
		return -ENOMEM;
	}

	codec->private_data = wm8750;
	socdev->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
	wm8750_socdev = socdev;
	INIT_DELAYED_WORK(&codec->delayed_work, wm8750_work);
	
#if defined (CONFIG_I2C) || defined (CONFIG_I2C_MODULE)
	if (setup->i2c_address) {
		normal_i2c[0] = setup->i2c_address;
		codec->hw_write = (hw_write_t)i2c_master_send;
		ret = i2c_add_driver(&wm8750_i2c_driver);
		if (ret != 0)
			printk(KERN_ERR "can't add i2c driver");
	}
#else
		/* Add other interfaces here */
#endif

	return ret;
}

/*
 * This function forces any delayed work to be queued and run.
 */
static int run_delayed_work(struct delayed_work *dwork)
{
	int ret;

	/* cancel any work waiting to be queued. */
	ret = cancel_delayed_work(dwork);

	/* if there was any work waiting then we run it now and
	 * wait for it's completion */
	if (ret) {
		schedule_delayed_work(dwork, 0);
		flush_scheduled_work();
	}
	return ret;
}

/* power down chip */
static int wm8750_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->codec;

	if (codec->control_data)
		wm8750_dapm_event(codec, SNDRV_CTL_POWER_D3cold);
	run_delayed_work(&codec->delayed_work);
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
#if defined (CONFIG_I2C) || defined (CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8750_i2c_driver);
#endif
	kfree(codec->private_data);
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8750 = {
	.probe = 	wm8750_probe,
	.remove = 	wm8750_remove,
	.suspend = 	wm8750_suspend,
	.resume =	wm8750_resume,
};

EXPORT_SYMBOL_GPL(soc_codec_dev_wm8750);

MODULE_DESCRIPTION("ASoC WM8750 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
