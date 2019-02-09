/*
 * wm8988.c -- WM8988 ALSA SoC audio driver
 *
 * Copyright 2009 Wolfson Microelectronics plc
 * Copyright 2005 Openedhand Ltd.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
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
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "wm8988.h"

/*
 * wm8988 register cache
 * We can't read the WM8988 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const struct reg_default wm8988_reg_defaults[] = {
	{ 0, 0x0097 },
	{ 1, 0x0097 },
	{ 2, 0x0079 },
	{ 3, 0x0079 },
	{ 5, 0x0008 },
	{ 7, 0x000a },
	{ 8, 0x0000 },
	{ 10, 0x00ff },
	{ 11, 0x00ff },
	{ 12, 0x000f },
	{ 13, 0x000f },
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
	{ 31, 0x0000 },
	{ 32, 0x0000 },
	{ 33, 0x0000 },
	{ 34, 0x0050 },
	{ 35, 0x0050 },
	{ 36, 0x0050 },
	{ 37, 0x0050 },
	{ 40, 0x0079 },
	{ 41, 0x0079 },
	{ 42, 0x0079 },
};

static bool wm8988_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8988_LINVOL:
	case WM8988_RINVOL:
	case WM8988_LOUT1V:
	case WM8988_ROUT1V:
	case WM8988_ADCDAC:
	case WM8988_IFACE:
	case WM8988_SRATE:
	case WM8988_LDAC:
	case WM8988_RDAC:
	case WM8988_BASS:
	case WM8988_TREBLE:
	case WM8988_RESET:
	case WM8988_3D:
	case WM8988_ALC1:
	case WM8988_ALC2:
	case WM8988_ALC3:
	case WM8988_NGATE:
	case WM8988_LADC:
	case WM8988_RADC:
	case WM8988_ADCTL1:
	case WM8988_ADCTL2:
	case WM8988_PWR1:
	case WM8988_PWR2:
	case WM8988_ADCTL3:
	case WM8988_ADCIN:
	case WM8988_LADCIN:
	case WM8988_RADCIN:
	case WM8988_LOUTM1:
	case WM8988_LOUTM2:
	case WM8988_ROUTM1:
	case WM8988_ROUTM2:
	case WM8988_LOUT2V:
	case WM8988_ROUT2V:
	case WM8988_LPPB:
		return true;
	default:
		return false;
	}
}

/* codec private data */
struct wm8988_priv {
	struct regmap *regmap;
	unsigned int sysclk;
	const struct snd_pcm_hw_constraint_list *sysclk_constraints;
};

#define wm8988_reset(c)	snd_soc_component_write(c, WM8988_RESET, 0)

/*
 * WM8988 Controls
 */

static const char *bass_boost_txt[] = {"Linear Control", "Adaptive Boost"};
static SOC_ENUM_SINGLE_DECL(bass_boost,
			    WM8988_BASS, 7, bass_boost_txt);

static const char *bass_filter_txt[] = { "130Hz @ 48kHz", "200Hz @ 48kHz" };
static SOC_ENUM_SINGLE_DECL(bass_filter,
			    WM8988_BASS, 6, bass_filter_txt);

static const char *treble_txt[] = {"8kHz", "4kHz"};
static SOC_ENUM_SINGLE_DECL(treble,
			    WM8988_TREBLE, 6, treble_txt);

static const char *stereo_3d_lc_txt[] = {"200Hz", "500Hz"};
static SOC_ENUM_SINGLE_DECL(stereo_3d_lc,
			    WM8988_3D, 5, stereo_3d_lc_txt);

static const char *stereo_3d_uc_txt[] = {"2.2kHz", "1.5kHz"};
static SOC_ENUM_SINGLE_DECL(stereo_3d_uc,
			    WM8988_3D, 6, stereo_3d_uc_txt);

static const char *stereo_3d_func_txt[] = {"Capture", "Playback"};
static SOC_ENUM_SINGLE_DECL(stereo_3d_func,
			    WM8988_3D, 7, stereo_3d_func_txt);

static const char *alc_func_txt[] = {"Off", "Right", "Left", "Stereo"};
static SOC_ENUM_SINGLE_DECL(alc_func,
			    WM8988_ALC1, 7, alc_func_txt);

static const char *ng_type_txt[] = {"Constant PGA Gain",
				    "Mute ADC Output"};
static SOC_ENUM_SINGLE_DECL(ng_type,
			    WM8988_NGATE, 1, ng_type_txt);

static const char *deemph_txt[] = {"None", "32Khz", "44.1Khz", "48Khz"};
static SOC_ENUM_SINGLE_DECL(deemph,
			    WM8988_ADCDAC, 1, deemph_txt);

static const char *adcpol_txt[] = {"Normal", "L Invert", "R Invert",
				   "L + R Invert"};
static SOC_ENUM_SINGLE_DECL(adcpol,
			    WM8988_ADCDAC, 5, adcpol_txt);

static const DECLARE_TLV_DB_SCALE(pga_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -9750, 50, 1);
static const DECLARE_TLV_DB_SCALE(dac_tlv, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -12100, 100, 1);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);

static const struct snd_kcontrol_new wm8988_snd_controls[] = {

SOC_ENUM("Bass Boost", bass_boost),
SOC_ENUM("Bass Filter", bass_filter),
SOC_SINGLE("Bass Volume", WM8988_BASS, 0, 15, 1),

SOC_SINGLE("Treble Volume", WM8988_TREBLE, 0, 15, 0),
SOC_ENUM("Treble Cut-off", treble),

SOC_SINGLE("3D Switch", WM8988_3D, 0, 1, 0),
SOC_SINGLE("3D Volume", WM8988_3D, 1, 15, 0),
SOC_ENUM("3D Lower Cut-off", stereo_3d_lc),
SOC_ENUM("3D Upper Cut-off", stereo_3d_uc),
SOC_ENUM("3D Mode", stereo_3d_func),

SOC_SINGLE("ALC Capture Target Volume", WM8988_ALC1, 0, 7, 0),
SOC_SINGLE("ALC Capture Max Volume", WM8988_ALC1, 4, 7, 0),
SOC_ENUM("ALC Capture Function", alc_func),
SOC_SINGLE("ALC Capture ZC Switch", WM8988_ALC2, 7, 1, 0),
SOC_SINGLE("ALC Capture Hold Time", WM8988_ALC2, 0, 15, 0),
SOC_SINGLE("ALC Capture Decay Time", WM8988_ALC3, 4, 15, 0),
SOC_SINGLE("ALC Capture Attack Time", WM8988_ALC3, 0, 15, 0),
SOC_SINGLE("ALC Capture NG Threshold", WM8988_NGATE, 3, 31, 0),
SOC_ENUM("ALC Capture NG Type", ng_type),
SOC_SINGLE("ALC Capture NG Switch", WM8988_NGATE, 0, 1, 0),

SOC_SINGLE("ZC Timeout Switch", WM8988_ADCTL1, 0, 1, 0),

SOC_DOUBLE_R_TLV("Capture Digital Volume", WM8988_LADC, WM8988_RADC,
		 0, 255, 0, adc_tlv),
SOC_DOUBLE_R_TLV("Capture Volume", WM8988_LINVOL, WM8988_RINVOL,
		 0, 63, 0, pga_tlv),
SOC_DOUBLE_R("Capture ZC Switch", WM8988_LINVOL, WM8988_RINVOL, 6, 1, 0),
SOC_DOUBLE_R("Capture Switch", WM8988_LINVOL, WM8988_RINVOL, 7, 1, 1),

SOC_ENUM("Playback De-emphasis", deemph),

SOC_ENUM("Capture Polarity", adcpol),
SOC_SINGLE("Playback 6dB Attenuate", WM8988_ADCDAC, 7, 1, 0),
SOC_SINGLE("Capture 6dB Attenuate", WM8988_ADCDAC, 8, 1, 0),

SOC_DOUBLE_R_TLV("PCM Volume", WM8988_LDAC, WM8988_RDAC, 0, 255, 0, dac_tlv),

SOC_SINGLE_TLV("Left Mixer Left Bypass Volume", WM8988_LOUTM1, 4, 7, 1,
	       bypass_tlv),
SOC_SINGLE_TLV("Left Mixer Right Bypass Volume", WM8988_LOUTM2, 4, 7, 1,
	       bypass_tlv),
SOC_SINGLE_TLV("Right Mixer Left Bypass Volume", WM8988_ROUTM1, 4, 7, 1,
	       bypass_tlv),
SOC_SINGLE_TLV("Right Mixer Right Bypass Volume", WM8988_ROUTM2, 4, 7, 1,
	       bypass_tlv),

SOC_DOUBLE_R("Output 1 Playback ZC Switch", WM8988_LOUT1V,
	     WM8988_ROUT1V, 7, 1, 0),
SOC_DOUBLE_R_TLV("Output 1 Playback Volume", WM8988_LOUT1V, WM8988_ROUT1V,
		 0, 127, 0, out_tlv),

SOC_DOUBLE_R("Output 2 Playback ZC Switch", WM8988_LOUT2V,
	     WM8988_ROUT2V, 7, 1, 0),
SOC_DOUBLE_R_TLV("Output 2 Playback Volume", WM8988_LOUT2V, WM8988_ROUT2V,
		 0, 127, 0, out_tlv),

};

/*
 * DAPM Controls
 */

static int wm8988_lrc_control(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u16 adctl2 = snd_soc_component_read32(component, WM8988_ADCTL2);

	/* Use the DAC to gate LRC if active, otherwise use ADC */
	if (snd_soc_component_read32(component, WM8988_PWR2) & 0x180)
		adctl2 &= ~0x4;
	else
		adctl2 |= 0x4;

	return snd_soc_component_write(component, WM8988_ADCTL2, adctl2);
}

static const char *wm8988_line_texts[] = {
	"Line 1", "Line 2", "PGA", "Differential"};

static const unsigned int wm8988_line_values[] = {
	0, 1, 3, 4};

static const struct soc_enum wm8988_lline_enum =
	SOC_VALUE_ENUM_SINGLE(WM8988_LOUTM1, 0, 7,
			      ARRAY_SIZE(wm8988_line_texts),
			      wm8988_line_texts,
			      wm8988_line_values);
static const struct snd_kcontrol_new wm8988_left_line_controls =
	SOC_DAPM_ENUM("Route", wm8988_lline_enum);

static const struct soc_enum wm8988_rline_enum =
	SOC_VALUE_ENUM_SINGLE(WM8988_ROUTM1, 0, 7,
			      ARRAY_SIZE(wm8988_line_texts),
			      wm8988_line_texts,
			      wm8988_line_values);
static const struct snd_kcontrol_new wm8988_right_line_controls =
	SOC_DAPM_ENUM("Route", wm8988_lline_enum);

/* Left Mixer */
static const struct snd_kcontrol_new wm8988_left_mixer_controls[] = {
	SOC_DAPM_SINGLE("Playback Switch", WM8988_LOUTM1, 8, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", WM8988_LOUTM1, 7, 1, 0),
	SOC_DAPM_SINGLE("Right Playback Switch", WM8988_LOUTM2, 8, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", WM8988_LOUTM2, 7, 1, 0),
};

/* Right Mixer */
static const struct snd_kcontrol_new wm8988_right_mixer_controls[] = {
	SOC_DAPM_SINGLE("Left Playback Switch", WM8988_ROUTM1, 8, 1, 0),
	SOC_DAPM_SINGLE("Left Bypass Switch", WM8988_ROUTM1, 7, 1, 0),
	SOC_DAPM_SINGLE("Playback Switch", WM8988_ROUTM2, 8, 1, 0),
	SOC_DAPM_SINGLE("Right Bypass Switch", WM8988_ROUTM2, 7, 1, 0),
};

static const char *wm8988_pga_sel[] = {"Line 1", "Line 2", "Differential"};
static const unsigned int wm8988_pga_val[] = { 0, 1, 3 };

/* Left PGA Mux */
static const struct soc_enum wm8988_lpga_enum =
	SOC_VALUE_ENUM_SINGLE(WM8988_LADCIN, 6, 3,
			      ARRAY_SIZE(wm8988_pga_sel),
			      wm8988_pga_sel,
			      wm8988_pga_val);
static const struct snd_kcontrol_new wm8988_left_pga_controls =
	SOC_DAPM_ENUM("Route", wm8988_lpga_enum);

/* Right PGA Mux */
static const struct soc_enum wm8988_rpga_enum =
	SOC_VALUE_ENUM_SINGLE(WM8988_RADCIN, 6, 3,
			      ARRAY_SIZE(wm8988_pga_sel),
			      wm8988_pga_sel,
			      wm8988_pga_val);
static const struct snd_kcontrol_new wm8988_right_pga_controls =
	SOC_DAPM_ENUM("Route", wm8988_rpga_enum);

/* Differential Mux */
static const char *wm8988_diff_sel[] = {"Line 1", "Line 2"};
static SOC_ENUM_SINGLE_DECL(diffmux,
			    WM8988_ADCIN, 8, wm8988_diff_sel);
static const struct snd_kcontrol_new wm8988_diffmux_controls =
	SOC_DAPM_ENUM("Route", diffmux);

/* Mono ADC Mux */
static const char *wm8988_mono_mux[] = {"Stereo", "Mono (Left)",
	"Mono (Right)", "Digital Mono"};
static SOC_ENUM_SINGLE_DECL(monomux,
			    WM8988_ADCIN, 6, wm8988_mono_mux);
static const struct snd_kcontrol_new wm8988_monomux_controls =
	SOC_DAPM_ENUM("Route", monomux);

static const struct snd_soc_dapm_widget wm8988_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("Mic Bias", WM8988_PWR1, 1, 0, NULL, 0),

	SND_SOC_DAPM_MUX("Differential Mux", SND_SOC_NOPM, 0, 0,
		&wm8988_diffmux_controls),
	SND_SOC_DAPM_MUX("Left ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8988_monomux_controls),
	SND_SOC_DAPM_MUX("Right ADC Mux", SND_SOC_NOPM, 0, 0,
		&wm8988_monomux_controls),

	SND_SOC_DAPM_MUX("Left PGA Mux", WM8988_PWR1, 5, 0,
		&wm8988_left_pga_controls),
	SND_SOC_DAPM_MUX("Right PGA Mux", WM8988_PWR1, 4, 0,
		&wm8988_right_pga_controls),

	SND_SOC_DAPM_MUX("Left Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8988_left_line_controls),
	SND_SOC_DAPM_MUX("Right Line Mux", SND_SOC_NOPM, 0, 0,
		&wm8988_right_line_controls),

	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8988_PWR1, 2, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8988_PWR1, 3, 0),

	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8988_PWR2, 7, 0),
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8988_PWR2, 8, 0),

	SND_SOC_DAPM_MIXER("Left Mixer", SND_SOC_NOPM, 0, 0,
		&wm8988_left_mixer_controls[0],
		ARRAY_SIZE(wm8988_left_mixer_controls)),
	SND_SOC_DAPM_MIXER("Right Mixer", SND_SOC_NOPM, 0, 0,
		&wm8988_right_mixer_controls[0],
		ARRAY_SIZE(wm8988_right_mixer_controls)),

	SND_SOC_DAPM_PGA("Right Out 2", WM8988_PWR2, 3, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 2", WM8988_PWR2, 4, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Out 1", WM8988_PWR2, 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Left Out 1", WM8988_PWR2, 6, 0, NULL, 0),

	SND_SOC_DAPM_POST("LRC control", wm8988_lrc_control),

	SND_SOC_DAPM_OUTPUT("LOUT1"),
	SND_SOC_DAPM_OUTPUT("ROUT1"),
	SND_SOC_DAPM_OUTPUT("LOUT2"),
	SND_SOC_DAPM_OUTPUT("ROUT2"),
	SND_SOC_DAPM_OUTPUT("VREF"),

	SND_SOC_DAPM_INPUT("LINPUT1"),
	SND_SOC_DAPM_INPUT("LINPUT2"),
	SND_SOC_DAPM_INPUT("RINPUT1"),
	SND_SOC_DAPM_INPUT("RINPUT2"),
};

static const struct snd_soc_dapm_route wm8988_dapm_routes[] = {

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

	{ "Left Line Mux", "Line 1", "LINPUT1" },
	{ "Left Line Mux", "Line 2", "LINPUT2" },
	{ "Left Line Mux", "PGA", "Left PGA Mux" },
	{ "Left Line Mux", "Differential", "Differential Mux" },

	{ "Right Line Mux", "Line 1", "RINPUT1" },
	{ "Right Line Mux", "Line 2", "RINPUT2" },
	{ "Right Line Mux", "PGA", "Right PGA Mux" },
	{ "Right Line Mux", "Differential", "Differential Mux" },

	{ "Left Mixer", "Playback Switch", "Left DAC" },
	{ "Left Mixer", "Left Bypass Switch", "Left Line Mux" },
	{ "Left Mixer", "Right Playback Switch", "Right DAC" },
	{ "Left Mixer", "Right Bypass Switch", "Right Line Mux" },

	{ "Right Mixer", "Left Playback Switch", "Left DAC" },
	{ "Right Mixer", "Left Bypass Switch", "Left Line Mux" },
	{ "Right Mixer", "Playback Switch", "Right DAC" },
	{ "Right Mixer", "Right Bypass Switch", "Right Line Mux" },

	{ "Left Out 1", NULL, "Left Mixer" },
	{ "LOUT1", NULL, "Left Out 1" },
	{ "Right Out 1", NULL, "Right Mixer" },
	{ "ROUT1", NULL, "Right Out 1" },

	{ "Left Out 2", NULL, "Left Mixer" },
	{ "LOUT2", NULL, "Left Out 2" },
	{ "Right Out 2", NULL, "Right Mixer" },
	{ "ROUT2", NULL, "Right Out 2" },
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

	return -EINVAL;
}

/* The set of rates we can generate from the above for each SYSCLK */

static const unsigned int rates_12288[] = {
	8000, 12000, 16000, 24000, 32000, 48000, 96000,
};

static const struct snd_pcm_hw_constraint_list constraints_12288 = {
	.count	= ARRAY_SIZE(rates_12288),
	.list	= rates_12288,
};

static const unsigned int rates_112896[] = {
	8000, 11025, 22050, 44100,
};

static const struct snd_pcm_hw_constraint_list constraints_112896 = {
	.count	= ARRAY_SIZE(rates_112896),
	.list	= rates_112896,
};

static const unsigned int rates_12[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 41100, 48000,
	48000, 88235, 96000,
};

static const struct snd_pcm_hw_constraint_list constraints_12 = {
	.count	= ARRAY_SIZE(rates_12),
	.list	= rates_12,
};

/*
 * Note that this should be called from init rather than from hw_params.
 */
static int wm8988_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct wm8988_priv *wm8988 = snd_soc_component_get_drvdata(component);

	switch (freq) {
	case 11289600:
	case 18432000:
	case 22579200:
	case 36864000:
		wm8988->sysclk_constraints = &constraints_112896;
		wm8988->sysclk = freq;
		return 0;

	case 12288000:
	case 16934400:
	case 24576000:
	case 33868800:
		wm8988->sysclk_constraints = &constraints_12288;
		wm8988->sysclk = freq;
		return 0;

	case 12000000:
	case 24000000:
		wm8988->sysclk_constraints = &constraints_12;
		wm8988->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int wm8988_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
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

	snd_soc_component_write(component, WM8988_IFACE, iface);
	return 0;
}

static int wm8988_pcm_startup(struct snd_pcm_substream *substream,
			      struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8988_priv *wm8988 = snd_soc_component_get_drvdata(component);

	/* The set of sample rates that can be supported depends on the
	 * MCLK supplied to the CODEC - enforce this.
	 */
	if (!wm8988->sysclk) {
		dev_err(component->dev,
			"No MCLK configured, call set_sysclk() on init\n");
		return -EINVAL;
	}

	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_RATE,
				   wm8988->sysclk_constraints);

	return 0;
}

static int wm8988_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct wm8988_priv *wm8988 = snd_soc_component_get_drvdata(component);
	u16 iface = snd_soc_component_read32(component, WM8988_IFACE) & 0x1f3;
	u16 srate = snd_soc_component_read32(component, WM8988_SRATE) & 0x180;
	int coeff;

	coeff = get_coeff(wm8988->sysclk, params_rate(params));
	if (coeff < 0) {
		coeff = get_coeff(wm8988->sysclk / 2, params_rate(params));
		srate |= 0x40;
	}
	if (coeff < 0) {
		dev_err(component->dev,
			"Unable to configure sample rate %dHz with %dHz MCLK\n",
			params_rate(params), wm8988->sysclk);
		return coeff;
	}

	/* bit size */
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		iface |= 0x0004;
		break;
	case 24:
		iface |= 0x0008;
		break;
	case 32:
		iface |= 0x000c;
		break;
	}

	/* set iface & srate */
	snd_soc_component_write(component, WM8988_IFACE, iface);
	if (coeff >= 0)
		snd_soc_component_write(component, WM8988_SRATE, srate |
			(coeff_div[coeff].sr << 1) | coeff_div[coeff].usb);

	return 0;
}

static int wm8988_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;
	u16 mute_reg = snd_soc_component_read32(component, WM8988_ADCDAC) & 0xfff7;

	if (mute)
		snd_soc_component_write(component, WM8988_ADCDAC, mute_reg | 0x8);
	else
		snd_soc_component_write(component, WM8988_ADCDAC, mute_reg);
	return 0;
}

static int wm8988_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8988_priv *wm8988 = snd_soc_component_get_drvdata(component);
	u16 pwr_reg = snd_soc_component_read32(component, WM8988_PWR1) & ~0x1c1;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VREF, VMID=2x50k, digital enabled */
		snd_soc_component_write(component, WM8988_PWR1, pwr_reg | 0x00c0);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			regcache_sync(wm8988->regmap);

			/* VREF, VMID=2x5k */
			snd_soc_component_write(component, WM8988_PWR1, pwr_reg | 0x1c1);

			/* Charge caps */
			msleep(100);
		}

		/* VREF, VMID=2*500k, digital stopped */
		snd_soc_component_write(component, WM8988_PWR1, pwr_reg | 0x0141);
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_write(component, WM8988_PWR1, 0x0000);
		break;
	}
	return 0;
}

#define WM8988_RATES SNDRV_PCM_RATE_8000_96000

#define WM8988_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8988_ops = {
	.startup = wm8988_pcm_startup,
	.hw_params = wm8988_pcm_hw_params,
	.set_fmt = wm8988_set_dai_fmt,
	.set_sysclk = wm8988_set_dai_sysclk,
	.digital_mute = wm8988_mute,
};

static struct snd_soc_dai_driver wm8988_dai = {
	.name = "wm8988-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8988_RATES,
		.formats = WM8988_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8988_RATES,
		.formats = WM8988_FORMATS,
	 },
	.ops = &wm8988_ops,
	.symmetric_rates = 1,
};

static int wm8988_probe(struct snd_soc_component *component)
{
	int ret = 0;

	ret = wm8988_reset(component);
	if (ret < 0) {
		dev_err(component->dev, "Failed to issue reset\n");
		return ret;
	}

	/* set the update bits (we always update left then right) */
	snd_soc_component_update_bits(component, WM8988_RADC, 0x0100, 0x0100);
	snd_soc_component_update_bits(component, WM8988_RDAC, 0x0100, 0x0100);
	snd_soc_component_update_bits(component, WM8988_ROUT1V, 0x0100, 0x0100);
	snd_soc_component_update_bits(component, WM8988_ROUT2V, 0x0100, 0x0100);
	snd_soc_component_update_bits(component, WM8988_RINVOL, 0x0100, 0x0100);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_wm8988 = {
	.probe			= wm8988_probe,
	.set_bias_level		= wm8988_set_bias_level,
	.controls		= wm8988_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8988_snd_controls),
	.dapm_widgets		= wm8988_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8988_dapm_widgets),
	.dapm_routes		= wm8988_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8988_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config wm8988_regmap = {
	.reg_bits = 7,
	.val_bits = 9,

	.max_register = WM8988_LPPB,
	.writeable_reg = wm8988_writeable,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm8988_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8988_reg_defaults),
};

#if defined(CONFIG_SPI_MASTER)
static int wm8988_spi_probe(struct spi_device *spi)
{
	struct wm8988_priv *wm8988;
	int ret;

	wm8988 = devm_kzalloc(&spi->dev, sizeof(struct wm8988_priv),
			      GFP_KERNEL);
	if (wm8988 == NULL)
		return -ENOMEM;

	wm8988->regmap = devm_regmap_init_spi(spi, &wm8988_regmap);
	if (IS_ERR(wm8988->regmap)) {
		ret = PTR_ERR(wm8988->regmap);
		dev_err(&spi->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	spi_set_drvdata(spi, wm8988);

	ret = devm_snd_soc_register_component(&spi->dev,
			&soc_component_dev_wm8988, &wm8988_dai, 1);
	return ret;
}

static struct spi_driver wm8988_spi_driver = {
	.driver = {
		.name	= "wm8988",
	},
	.probe		= wm8988_spi_probe,
};
#endif /* CONFIG_SPI_MASTER */

#if IS_ENABLED(CONFIG_I2C)
static int wm8988_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8988_priv *wm8988;
	int ret;

	wm8988 = devm_kzalloc(&i2c->dev, sizeof(struct wm8988_priv),
			      GFP_KERNEL);
	if (wm8988 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8988);

	wm8988->regmap = devm_regmap_init_i2c(i2c, &wm8988_regmap);
	if (IS_ERR(wm8988->regmap)) {
		ret = PTR_ERR(wm8988->regmap);
		dev_err(&i2c->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8988, &wm8988_dai, 1);
	return ret;
}

static const struct i2c_device_id wm8988_i2c_id[] = {
	{ "wm8988", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8988_i2c_id);

static struct i2c_driver wm8988_i2c_driver = {
	.driver = {
		.name = "wm8988",
	},
	.probe =    wm8988_i2c_probe,
	.id_table = wm8988_i2c_id,
};
#endif

static int __init wm8988_modinit(void)
{
	int ret = 0;
#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8988_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8988 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8988_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8988 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8988_modinit);

static void __exit wm8988_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&wm8988_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8988_spi_driver);
#endif
}
module_exit(wm8988_exit);


MODULE_DESCRIPTION("ASoC WM8988 driver");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
