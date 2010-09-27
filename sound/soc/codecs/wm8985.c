/*
 * wm8985.c  --  WM8985 ALSA SoC Audio driver
 *
 * Copyright 2010 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO:
 *  o Add OUT3/OUT4 mixer controls.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8985.h"

#define WM8985_NUM_SUPPLIES 4
static const char *wm8985_supply_names[WM8985_NUM_SUPPLIES] = {
	"DCVDD",
	"DBVDD",
	"AVDD1",
	"AVDD2"
};

static const u16 wm8985_reg_defs[] = {
	0x0000,     /* R0  - Software Reset */
	0x0000,     /* R1  - Power management 1 */
	0x0000,     /* R2  - Power management 2 */
	0x0000,     /* R3  - Power management 3 */
	0x0050,     /* R4  - Audio Interface */
	0x0000,     /* R5  - Companding control */
	0x0140,     /* R6  - Clock Gen control */
	0x0000,     /* R7  - Additional control */
	0x0000,     /* R8  - GPIO Control */
	0x0000,     /* R9  - Jack Detect Control 1 */
	0x0000,     /* R10 - DAC Control */
	0x00FF,     /* R11 - Left DAC digital Vol */
	0x00FF,     /* R12 - Right DAC digital vol */
	0x0000,     /* R13 - Jack Detect Control 2 */
	0x0100,     /* R14 - ADC Control */
	0x00FF,     /* R15 - Left ADC Digital Vol */
	0x00FF,     /* R16 - Right ADC Digital Vol */
	0x0000,     /* R17 */
	0x012C,     /* R18 - EQ1 - low shelf */
	0x002C,     /* R19 - EQ2 - peak 1 */
	0x002C,     /* R20 - EQ3 - peak 2 */
	0x002C,     /* R21 - EQ4 - peak 3 */
	0x002C,     /* R22 - EQ5 - high shelf */
	0x0000,     /* R23 */
	0x0032,     /* R24 - DAC Limiter 1 */
	0x0000,     /* R25 - DAC Limiter 2 */
	0x0000,     /* R26 */
	0x0000,     /* R27 - Notch Filter 1 */
	0x0000,     /* R28 - Notch Filter 2 */
	0x0000,     /* R29 - Notch Filter 3 */
	0x0000,     /* R30 - Notch Filter 4 */
	0x0000,     /* R31 */
	0x0038,     /* R32 - ALC control 1 */
	0x000B,     /* R33 - ALC control 2 */
	0x0032,     /* R34 - ALC control 3 */
	0x0000,     /* R35 - Noise Gate */
	0x0008,     /* R36 - PLL N */
	0x000C,     /* R37 - PLL K 1 */
	0x0093,     /* R38 - PLL K 2 */
	0x00E9,     /* R39 - PLL K 3 */
	0x0000,     /* R40 */
	0x0000,     /* R41 - 3D control */
	0x0000,     /* R42 - OUT4 to ADC */
	0x0000,     /* R43 - Beep control */
	0x0033,     /* R44 - Input ctrl */
	0x0010,     /* R45 - Left INP PGA gain ctrl */
	0x0010,     /* R46 - Right INP PGA gain ctrl */
	0x0100,     /* R47 - Left ADC BOOST ctrl */
	0x0100,     /* R48 - Right ADC BOOST ctrl */
	0x0002,     /* R49 - Output ctrl */
	0x0001,     /* R50 - Left mixer ctrl */
	0x0001,     /* R51 - Right mixer ctrl */
	0x0039,     /* R52 - LOUT1 (HP) volume ctrl */
	0x0039,     /* R53 - ROUT1 (HP) volume ctrl */
	0x0039,     /* R54 - LOUT2 (SPK) volume ctrl */
	0x0039,     /* R55 - ROUT2 (SPK) volume ctrl */
	0x0001,     /* R56 - OUT3 mixer ctrl */
	0x0001,     /* R57 - OUT4 (MONO) mix ctrl */
	0x0001,     /* R58 */
	0x0000,     /* R59 */
	0x0004,     /* R60 - OUTPUT ctrl */
	0x0000,     /* R61 - BIAS CTRL */
	0x0180,     /* R62 */
	0x0000      /* R63 */
};

/*
 * latch bit 8 of these registers to ensure instant
 * volume updates
 */
static const int volume_update_regs[] = {
	WM8985_LEFT_DAC_DIGITAL_VOL,
	WM8985_RIGHT_DAC_DIGITAL_VOL,
	WM8985_LEFT_ADC_DIGITAL_VOL,
	WM8985_RIGHT_ADC_DIGITAL_VOL,
	WM8985_LOUT2_SPK_VOLUME_CTRL,
	WM8985_ROUT2_SPK_VOLUME_CTRL,
	WM8985_LOUT1_HP_VOLUME_CTRL,
	WM8985_ROUT1_HP_VOLUME_CTRL,
	WM8985_LEFT_INP_PGA_GAIN_CTRL,
	WM8985_RIGHT_INP_PGA_GAIN_CTRL
};

struct wm8985_priv {
	enum snd_soc_control_type control_type;
	struct regulator_bulk_data supplies[WM8985_NUM_SUPPLIES];
	unsigned int sysclk;
	unsigned int bclk;
};

static const struct {
	int div;
	int ratio;
} fs_ratios[] = {
	{ 10, 128 },
	{ 15, 192 },
	{ 20, 256 },
	{ 30, 384 },
	{ 40, 512 },
	{ 60, 768 },
	{ 80, 1024 },
	{ 120, 1536 }
};

static const int srates[] = { 48000, 32000, 24000, 16000, 12000, 8000 };

static const int bclk_divs[] = {
	1, 2, 4, 8, 16, 32
};

static int eqmode_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);
static int eqmode_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol);

static const DECLARE_TLV_DB_SCALE(dac_tlv, -12700, 50, 1);
static const DECLARE_TLV_DB_SCALE(adc_tlv, -12700, 50, 1);
static const DECLARE_TLV_DB_SCALE(out_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(lim_thresh_tlv, -600, 100, 0);
static const DECLARE_TLV_DB_SCALE(lim_boost_tlv, 0, 100, 0);
static const DECLARE_TLV_DB_SCALE(alc_min_tlv, -1200, 600, 0);
static const DECLARE_TLV_DB_SCALE(alc_max_tlv, -675, 600, 0);
static const DECLARE_TLV_DB_SCALE(alc_tar_tlv, -2250, 150, 0);
static const DECLARE_TLV_DB_SCALE(pga_vol_tlv, -1200, 75, 0);
static const DECLARE_TLV_DB_SCALE(boost_tlv, -1200, 300, 1);
static const DECLARE_TLV_DB_SCALE(eq_tlv, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(aux_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(bypass_tlv, -1500, 300, 0);
static const DECLARE_TLV_DB_SCALE(pga_boost_tlv, 0, 2000, 0);

static const char *alc_sel_text[] = { "Off", "Right", "Left", "Stereo" };
static const SOC_ENUM_SINGLE_DECL(alc_sel, WM8985_ALC_CONTROL_1, 7,
				  alc_sel_text);

static const char *alc_mode_text[] = { "ALC", "Limiter" };
static const SOC_ENUM_SINGLE_DECL(alc_mode, WM8985_ALC_CONTROL_3, 8,
				  alc_mode_text);

static const char *filter_mode_text[] = { "Audio", "Application" };
static const SOC_ENUM_SINGLE_DECL(filter_mode, WM8985_ADC_CONTROL, 7,
				  filter_mode_text);

static const char *eq_bw_text[] = { "Narrow", "Wide" };
static const char *eqmode_text[] = { "Capture", "Playback" };
static const SOC_ENUM_SINGLE_EXT_DECL(eqmode, eqmode_text);

static const char *eq1_cutoff_text[] = {
	"80Hz", "105Hz", "135Hz", "175Hz"
};
static const SOC_ENUM_SINGLE_DECL(eq1_cutoff, WM8985_EQ1_LOW_SHELF, 5,
				  eq1_cutoff_text);
static const char *eq2_cutoff_text[] = {
	"230Hz", "300Hz", "385Hz", "500Hz"
};
static const SOC_ENUM_SINGLE_DECL(eq2_bw, WM8985_EQ2_PEAK_1, 8, eq_bw_text);
static const SOC_ENUM_SINGLE_DECL(eq2_cutoff, WM8985_EQ2_PEAK_1, 5,
				  eq2_cutoff_text);
static const char *eq3_cutoff_text[] = {
	"650Hz", "850Hz", "1.1kHz", "1.4kHz"
};
static const SOC_ENUM_SINGLE_DECL(eq3_bw, WM8985_EQ3_PEAK_2, 8, eq_bw_text);
static const SOC_ENUM_SINGLE_DECL(eq3_cutoff, WM8985_EQ3_PEAK_2, 5,
				  eq3_cutoff_text);
static const char *eq4_cutoff_text[] = {
	"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz"
};
static const SOC_ENUM_SINGLE_DECL(eq4_bw, WM8985_EQ4_PEAK_3, 8, eq_bw_text);
static const SOC_ENUM_SINGLE_DECL(eq4_cutoff, WM8985_EQ4_PEAK_3, 5,
				  eq4_cutoff_text);
static const char *eq5_cutoff_text[] = {
	"5.3kHz", "6.9kHz", "9kHz", "11.7kHz"
};
static const SOC_ENUM_SINGLE_DECL(eq5_cutoff, WM8985_EQ5_HIGH_SHELF, 5,
				  eq5_cutoff_text);

static const char *speaker_mode_text[] = { "Class A/B", "Class D" };
static const SOC_ENUM_SINGLE_DECL(speaker_mode, 0x17, 8, speaker_mode_text);

static const char *depth_3d_text[] = {
	"Off",
	"6.67%",
	"13.3%",
	"20%",
	"26.7%",
	"33.3%",
	"40%",
	"46.6%",
	"53.3%",
	"60%",
	"66.7%",
	"73.3%",
	"80%",
	"86.7%",
	"93.3%",
	"100%"
};
static const SOC_ENUM_SINGLE_DECL(depth_3d, WM8985_3D_CONTROL, 0,
				  depth_3d_text);

static const struct snd_kcontrol_new wm8985_snd_controls[] = {
	SOC_SINGLE("Digital Loopback Switch", WM8985_COMPANDING_CONTROL,
		0, 1, 0),

	SOC_ENUM("ALC Capture Function", alc_sel),
	SOC_SINGLE_TLV("ALC Capture Max Volume", WM8985_ALC_CONTROL_1,
		3, 7, 0, alc_max_tlv),
	SOC_SINGLE_TLV("ALC Capture Min Volume", WM8985_ALC_CONTROL_1,
		0, 7, 0, alc_min_tlv),
	SOC_SINGLE_TLV("ALC Capture Target Volume", WM8985_ALC_CONTROL_2,
		0, 15, 0, alc_tar_tlv),
	SOC_SINGLE("ALC Capture Attack", WM8985_ALC_CONTROL_3, 0, 10, 0),
	SOC_SINGLE("ALC Capture Hold", WM8985_ALC_CONTROL_2, 4, 10, 0),
	SOC_SINGLE("ALC Capture Decay", WM8985_ALC_CONTROL_3, 4, 10, 0),
	SOC_ENUM("ALC Mode", alc_mode),
	SOC_SINGLE("ALC Capture NG Switch", WM8985_NOISE_GATE,
		3, 1, 0),
	SOC_SINGLE("ALC Capture NG Threshold", WM8985_NOISE_GATE,
		0, 7, 1),

	SOC_DOUBLE_R_TLV("Capture Volume", WM8985_LEFT_ADC_DIGITAL_VOL,
		WM8985_RIGHT_ADC_DIGITAL_VOL, 0, 255, 0, adc_tlv),
	SOC_DOUBLE_R("Capture PGA ZC Switch", WM8985_LEFT_INP_PGA_GAIN_CTRL,
		WM8985_RIGHT_INP_PGA_GAIN_CTRL, 7, 1, 0),
	SOC_DOUBLE_R_TLV("Capture PGA Volume", WM8985_LEFT_INP_PGA_GAIN_CTRL,
		WM8985_RIGHT_INP_PGA_GAIN_CTRL, 0, 63, 0, pga_vol_tlv),

	SOC_DOUBLE_R_TLV("Capture PGA Boost Volume",
		WM8985_LEFT_ADC_BOOST_CTRL, WM8985_RIGHT_ADC_BOOST_CTRL,
		8, 1, 0, pga_boost_tlv),

	SOC_DOUBLE("ADC Inversion Switch", WM8985_ADC_CONTROL, 0, 1, 1, 0),
	SOC_SINGLE("ADC 128x Oversampling Switch", WM8985_ADC_CONTROL, 8, 1, 0),

	SOC_DOUBLE_R_TLV("Playback Volume", WM8985_LEFT_DAC_DIGITAL_VOL,
		WM8985_RIGHT_DAC_DIGITAL_VOL, 0, 255, 0, dac_tlv),

	SOC_SINGLE("DAC Playback Limiter Switch", WM8985_DAC_LIMITER_1, 8, 1, 0),
	SOC_SINGLE("DAC Playback Limiter Decay", WM8985_DAC_LIMITER_1, 4, 10, 0),
	SOC_SINGLE("DAC Playback Limiter Attack", WM8985_DAC_LIMITER_1, 0, 11, 0),
	SOC_SINGLE_TLV("DAC Playback Limiter Threshold", WM8985_DAC_LIMITER_2,
		4, 7, 1, lim_thresh_tlv),
	SOC_SINGLE_TLV("DAC Playback Limiter Boost Volume", WM8985_DAC_LIMITER_2,
		0, 12, 0, lim_boost_tlv),
	SOC_DOUBLE("DAC Inversion Switch", WM8985_DAC_CONTROL, 0, 1, 1, 0),
	SOC_SINGLE("DAC Auto Mute Switch", WM8985_DAC_CONTROL, 2, 1, 0),
	SOC_SINGLE("DAC 128x Oversampling Switch", WM8985_DAC_CONTROL, 3, 1, 0),

	SOC_DOUBLE_R_TLV("Headphone Playback Volume", WM8985_LOUT1_HP_VOLUME_CTRL,
		WM8985_ROUT1_HP_VOLUME_CTRL, 0, 63, 0, out_tlv),
	SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8985_LOUT1_HP_VOLUME_CTRL,
		WM8985_ROUT1_HP_VOLUME_CTRL, 7, 1, 0),
	SOC_DOUBLE_R("Headphone Switch", WM8985_LOUT1_HP_VOLUME_CTRL,
		WM8985_ROUT1_HP_VOLUME_CTRL, 6, 1, 1),

	SOC_DOUBLE_R_TLV("Speaker Playback Volume", WM8985_LOUT2_SPK_VOLUME_CTRL,
		WM8985_ROUT2_SPK_VOLUME_CTRL, 0, 63, 0, out_tlv),
	SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8985_LOUT2_SPK_VOLUME_CTRL,
		WM8985_ROUT2_SPK_VOLUME_CTRL, 7, 1, 0),
	SOC_DOUBLE_R("Speaker Switch", WM8985_LOUT2_SPK_VOLUME_CTRL,
		WM8985_ROUT2_SPK_VOLUME_CTRL, 6, 1, 1),

	SOC_SINGLE("High Pass Filter Switch", WM8985_ADC_CONTROL, 8, 1, 0),
	SOC_ENUM("High Pass Filter Mode", filter_mode),
	SOC_SINGLE("High Pass Filter Cutoff", WM8985_ADC_CONTROL, 4, 7, 0),

	SOC_DOUBLE_R_TLV("Aux Bypass Volume",
		WM8985_LEFT_MIXER_CTRL, WM8985_RIGHT_MIXER_CTRL, 6, 7, 0,
		aux_tlv),

	SOC_DOUBLE_R_TLV("Input PGA Bypass Volume",
		WM8985_LEFT_MIXER_CTRL, WM8985_RIGHT_MIXER_CTRL, 2, 7, 0,
		bypass_tlv),

	SOC_ENUM_EXT("Equalizer Function", eqmode, eqmode_get, eqmode_put),
	SOC_ENUM("EQ1 Cutoff", eq1_cutoff),
	SOC_SINGLE_TLV("EQ1 Volume", WM8985_EQ1_LOW_SHELF,  0, 24, 1, eq_tlv),
	SOC_ENUM("EQ2 Bandwith", eq2_bw),
	SOC_ENUM("EQ2 Cutoff", eq2_cutoff),
	SOC_SINGLE_TLV("EQ2 Volume", WM8985_EQ2_PEAK_1, 0, 24, 1, eq_tlv),
	SOC_ENUM("EQ3 Bandwith", eq3_bw),
	SOC_ENUM("EQ3 Cutoff", eq3_cutoff),
	SOC_SINGLE_TLV("EQ3 Volume", WM8985_EQ3_PEAK_2, 0, 24, 1, eq_tlv),
	SOC_ENUM("EQ4 Bandwith", eq4_bw),
	SOC_ENUM("EQ4 Cutoff", eq4_cutoff),
	SOC_SINGLE_TLV("EQ4 Volume", WM8985_EQ4_PEAK_3, 0, 24, 1, eq_tlv),
	SOC_ENUM("EQ5 Cutoff", eq5_cutoff),
	SOC_SINGLE_TLV("EQ5 Volume", WM8985_EQ5_HIGH_SHELF, 0, 24, 1, eq_tlv),

	SOC_ENUM("3D Depth", depth_3d),

	SOC_ENUM("Speaker Mode", speaker_mode)
};

static const struct snd_kcontrol_new left_out_mixer[] = {
	SOC_DAPM_SINGLE("Line Switch", WM8985_LEFT_MIXER_CTRL, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Switch", WM8985_LEFT_MIXER_CTRL, 5, 1, 0),
	SOC_DAPM_SINGLE("PCM Switch", WM8985_LEFT_MIXER_CTRL, 0, 1, 0),
};

static const struct snd_kcontrol_new right_out_mixer[] = {
	SOC_DAPM_SINGLE("Line Switch", WM8985_RIGHT_MIXER_CTRL, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Switch", WM8985_RIGHT_MIXER_CTRL, 5, 1, 0),
	SOC_DAPM_SINGLE("PCM Switch", WM8985_RIGHT_MIXER_CTRL, 0, 1, 0),
};

static const struct snd_kcontrol_new left_input_mixer[] = {
	SOC_DAPM_SINGLE("L2 Switch", WM8985_INPUT_CTRL, 2, 1, 0),
	SOC_DAPM_SINGLE("MicN Switch", WM8985_INPUT_CTRL, 1, 1, 0),
	SOC_DAPM_SINGLE("MicP Switch", WM8985_INPUT_CTRL, 0, 1, 0),
};

static const struct snd_kcontrol_new right_input_mixer[] = {
	SOC_DAPM_SINGLE("R2 Switch", WM8985_INPUT_CTRL, 6, 1, 0),
	SOC_DAPM_SINGLE("MicN Switch", WM8985_INPUT_CTRL, 5, 1, 0),
	SOC_DAPM_SINGLE("MicP Switch", WM8985_INPUT_CTRL, 4, 1, 0),
};

static const struct snd_kcontrol_new left_boost_mixer[] = {
	SOC_DAPM_SINGLE_TLV("L2 Volume", WM8985_LEFT_ADC_BOOST_CTRL,
		4, 7, 0, boost_tlv),
	SOC_DAPM_SINGLE_TLV("AUXL Volume", WM8985_LEFT_ADC_BOOST_CTRL,
		0, 7, 0, boost_tlv)
};

static const struct snd_kcontrol_new right_boost_mixer[] = {
	SOC_DAPM_SINGLE_TLV("R2 Volume", WM8985_RIGHT_ADC_BOOST_CTRL,
		4, 7, 0, boost_tlv),
	SOC_DAPM_SINGLE_TLV("AUXR Volume", WM8985_RIGHT_ADC_BOOST_CTRL,
		0, 7, 0, boost_tlv)
};

static const struct snd_soc_dapm_widget wm8985_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8985_POWER_MANAGEMENT_3,
		0, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8985_POWER_MANAGEMENT_3,
		1, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8985_POWER_MANAGEMENT_2,
		0, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8985_POWER_MANAGEMENT_2,
		1, 0),

	SND_SOC_DAPM_MIXER("Left Output Mixer", WM8985_POWER_MANAGEMENT_3,
		2, 0, left_out_mixer, ARRAY_SIZE(left_out_mixer)),
	SND_SOC_DAPM_MIXER("Right Output Mixer", WM8985_POWER_MANAGEMENT_3,
		3, 0, right_out_mixer, ARRAY_SIZE(right_out_mixer)),

	SND_SOC_DAPM_MIXER("Left Input Mixer", WM8985_POWER_MANAGEMENT_2,
		2, 0, left_input_mixer, ARRAY_SIZE(left_input_mixer)),
	SND_SOC_DAPM_MIXER("Right Input Mixer", WM8985_POWER_MANAGEMENT_2,
		3, 0, right_input_mixer, ARRAY_SIZE(right_input_mixer)),

	SND_SOC_DAPM_MIXER("Left Boost Mixer", WM8985_POWER_MANAGEMENT_2,
		4, 0, left_boost_mixer, ARRAY_SIZE(left_boost_mixer)),
	SND_SOC_DAPM_MIXER("Right Boost Mixer", WM8985_POWER_MANAGEMENT_2,
		5, 0, right_boost_mixer, ARRAY_SIZE(right_boost_mixer)),

	SND_SOC_DAPM_PGA("Left Capture PGA", WM8985_LEFT_INP_PGA_GAIN_CTRL,
		6, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right Capture PGA", WM8985_RIGHT_INP_PGA_GAIN_CTRL,
		6, 1, NULL, 0),

	SND_SOC_DAPM_PGA("Left Headphone Out", WM8985_POWER_MANAGEMENT_2,
		7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Headphone Out", WM8985_POWER_MANAGEMENT_2,
		8, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Left Speaker Out", WM8985_POWER_MANAGEMENT_3,
		5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Speaker Out", WM8985_POWER_MANAGEMENT_3,
		6, 0, NULL, 0),

	SND_SOC_DAPM_MICBIAS("Mic Bias", WM8985_POWER_MANAGEMENT_1, 4, 0),

	SND_SOC_DAPM_INPUT("LIN"),
	SND_SOC_DAPM_INPUT("LIP"),
	SND_SOC_DAPM_INPUT("RIN"),
	SND_SOC_DAPM_INPUT("RIP"),
	SND_SOC_DAPM_INPUT("AUXL"),
	SND_SOC_DAPM_INPUT("AUXR"),
	SND_SOC_DAPM_INPUT("L2"),
	SND_SOC_DAPM_INPUT("R2"),
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("SPKL"),
	SND_SOC_DAPM_OUTPUT("SPKR")
};

static const struct snd_soc_dapm_route audio_map[] = {
	{ "Right Output Mixer", "PCM Switch", "Right DAC" },
	{ "Right Output Mixer", "Aux Switch", "AUXR" },
	{ "Right Output Mixer", "Line Switch", "Right Boost Mixer" },

	{ "Left Output Mixer", "PCM Switch", "Left DAC" },
	{ "Left Output Mixer", "Aux Switch", "AUXL" },
	{ "Left Output Mixer", "Line Switch", "Left Boost Mixer" },

	{ "Right Headphone Out", NULL, "Right Output Mixer" },
	{ "HPR", NULL, "Right Headphone Out" },

	{ "Left Headphone Out", NULL, "Left Output Mixer" },
	{ "HPL", NULL, "Left Headphone Out" },

	{ "Right Speaker Out", NULL, "Right Output Mixer" },
	{ "SPKR", NULL, "Right Speaker Out" },

	{ "Left Speaker Out", NULL, "Left Output Mixer" },
	{ "SPKL", NULL, "Left Speaker Out" },

	{ "Right ADC", NULL, "Right Boost Mixer" },

	{ "Right Boost Mixer", "AUXR Volume", "AUXR" },
	{ "Right Boost Mixer", NULL, "Right Capture PGA" },
	{ "Right Boost Mixer", "R2 Volume", "R2" },

	{ "Left ADC", NULL, "Left Boost Mixer" },

	{ "Left Boost Mixer", "AUXL Volume", "AUXL" },
	{ "Left Boost Mixer", NULL, "Left Capture PGA" },
	{ "Left Boost Mixer", "L2 Volume", "L2" },

	{ "Right Capture PGA", NULL, "Right Input Mixer" },
	{ "Left Capture PGA", NULL, "Left Input Mixer" },

	{ "Right Input Mixer", "R2 Switch", "R2" },
	{ "Right Input Mixer", "MicN Switch", "RIN" },
	{ "Right Input Mixer", "MicP Switch", "RIP" },

	{ "Left Input Mixer", "L2 Switch", "L2" },
	{ "Left Input Mixer", "MicN Switch", "LIN" },
	{ "Left Input Mixer", "MicP Switch", "LIP" },
};

static int eqmode_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int reg;

	reg = snd_soc_read(codec, WM8985_EQ1_LOW_SHELF);
	if (reg & WM8985_EQ3DMODE)
		ucontrol->value.integer.value[0] = 1;
	else
		ucontrol->value.integer.value[0] = 0;

	return 0;
}

static int eqmode_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	unsigned int regpwr2, regpwr3;
	unsigned int reg_eq;

	if (ucontrol->value.integer.value[0] != 0
			&& ucontrol->value.integer.value[0] != 1)
		return -EINVAL;

	reg_eq = snd_soc_read(codec, WM8985_EQ1_LOW_SHELF);
	switch ((reg_eq & WM8985_EQ3DMODE) >> WM8985_EQ3DMODE_SHIFT) {
	case 0:
		if (!ucontrol->value.integer.value[0])
			return 0;
		break;
	case 1:
		if (ucontrol->value.integer.value[0])
			return 0;
		break;
	}

	regpwr2 = snd_soc_read(codec, WM8985_POWER_MANAGEMENT_2);
	regpwr3 = snd_soc_read(codec, WM8985_POWER_MANAGEMENT_3);
	/* disable the DACs and ADCs */
	snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_2,
			    WM8985_ADCENR_MASK | WM8985_ADCENL_MASK, 0);
	snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_3,
			    WM8985_DACENR_MASK | WM8985_DACENL_MASK, 0);
	snd_soc_update_bits(codec, WM8985_ADDITIONAL_CONTROL,
			    WM8985_M128ENB_MASK, WM8985_M128ENB);
	/* set the desired eqmode */
	snd_soc_update_bits(codec, WM8985_EQ1_LOW_SHELF,
			    WM8985_EQ3DMODE_MASK,
			    ucontrol->value.integer.value[0]
			    << WM8985_EQ3DMODE_SHIFT);
	/* restore DAC/ADC configuration */
	snd_soc_write(codec, WM8985_POWER_MANAGEMENT_2, regpwr2);
	snd_soc_write(codec, WM8985_POWER_MANAGEMENT_3, regpwr3);
	return 0;
}

static int wm8985_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8985_dapm_widgets,
				  ARRAY_SIZE(wm8985_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map,
				ARRAY_SIZE(audio_map));
	return 0;
}

static int wm8985_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, WM8985_SOFTWARE_RESET, 0x0);
}

static int wm8985_dac_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	return snd_soc_update_bits(codec, WM8985_DAC_CONTROL,
				   WM8985_SOFTMUTE_MASK,
				   !!mute << WM8985_SOFTMUTE_SHIFT);
}

static int wm8985_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec;
	u16 format, master, bcp, lrp;

	codec = dai->codec;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = 0x2;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		format = 0x0;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = 0x1;
		break;
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		format = 0x3;
		break;
	default:
		dev_err(dai->dev, "Unknown dai format\n");
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8985_AUDIO_INTERFACE,
			    WM8985_FMT_MASK, format << WM8985_FMT_SHIFT);

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		master = 0;
		break;
	default:
		dev_err(dai->dev, "Unknown master/slave configuration\n");
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8985_CLOCK_GEN_CONTROL,
			    WM8985_MS_MASK, master << WM8985_MS_SHIFT);

	/* frame inversion is not valid for dsp modes */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_IB_IF:
		case SND_SOC_DAIFMT_NB_IF:
			return -EINVAL;
		default:
			break;
		}
		break;
	default:
		break;
	}

	bcp = lrp = 0;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		bcp = lrp = 1;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		bcp = 1;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		lrp = 1;
		break;
	default:
		dev_err(dai->dev, "Unknown polarity configuration\n");
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8985_AUDIO_INTERFACE,
			    WM8985_LRP_MASK, lrp << WM8985_LRP_SHIFT);
	snd_soc_update_bits(codec, WM8985_AUDIO_INTERFACE,
			    WM8985_BCP_MASK, bcp << WM8985_BCP_SHIFT);
	return 0;
}

static int wm8985_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	int i;
	struct snd_soc_codec *codec;
	struct wm8985_priv *wm8985;
	u16 blen, srate_idx;
	unsigned int tmp;
	int srate_best;

	codec = dai->codec;
	wm8985 = snd_soc_codec_get_drvdata(codec);

	wm8985->bclk = snd_soc_params_to_bclk(params);
	if ((int)wm8985->bclk < 0)
		return wm8985->bclk;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		blen = 0x0;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		blen = 0x1;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		blen = 0x2;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		blen = 0x3;
		break;
	default:
		dev_err(dai->dev, "Unsupported word length %u\n",
			params_format(params));
		return -EINVAL;
	}

	snd_soc_update_bits(codec, WM8985_AUDIO_INTERFACE,
			    WM8985_WL_MASK, blen << WM8985_WL_SHIFT);

	/*
	 * match to the nearest possible sample rate and rely
	 * on the array index to configure the SR register
	 */
	srate_idx = 0;
	srate_best = abs(srates[0] - params_rate(params));
	for (i = 1; i < ARRAY_SIZE(srates); ++i) {
		if (abs(srates[i] - params_rate(params)) >= srate_best)
			continue;
		srate_idx = i;
		srate_best = abs(srates[i] - params_rate(params));
	}

	dev_dbg(dai->dev, "Selected SRATE = %d\n", srates[srate_idx]);
	snd_soc_update_bits(codec, WM8985_ADDITIONAL_CONTROL,
			    WM8985_SR_MASK, srate_idx << WM8985_SR_SHIFT);

	dev_dbg(dai->dev, "Target BCLK = %uHz\n", wm8985->bclk);
	dev_dbg(dai->dev, "SYSCLK = %uHz\n", wm8985->sysclk);

	for (i = 0; i < ARRAY_SIZE(fs_ratios); ++i) {
		if (wm8985->sysclk / params_rate(params)
				== fs_ratios[i].ratio)
			break;
	}

	if (i == ARRAY_SIZE(fs_ratios)) {
		dev_err(dai->dev, "Unable to configure MCLK ratio %u/%u\n",
			wm8985->sysclk, params_rate(params));
		return -EINVAL;
	}

	dev_dbg(dai->dev, "MCLK ratio = %dfs\n", fs_ratios[i].ratio);
	snd_soc_update_bits(codec, WM8985_CLOCK_GEN_CONTROL,
			    WM8985_MCLKDIV_MASK, i << WM8985_MCLKDIV_SHIFT);

	/* select the appropriate bclk divider */
	tmp = (wm8985->sysclk / fs_ratios[i].div) * 10;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); ++i) {
		if (wm8985->bclk == tmp / bclk_divs[i])
			break;
	}

	if (i == ARRAY_SIZE(bclk_divs)) {
		dev_err(dai->dev, "No matching BCLK divider found\n");
		return -EINVAL;
	}

	dev_dbg(dai->dev, "BCLK div = %d\n", i);
	snd_soc_update_bits(codec, WM8985_CLOCK_GEN_CONTROL,
			    WM8985_BCLKDIV_MASK, i << WM8985_BCLKDIV_SHIFT);
	return 0;
}

struct pll_div {
	u32 div2:1;
	u32 n:4;
	u32 k:24;
};

#define FIXED_PLL_SIZE ((1ULL << 24) * 10)
static int pll_factors(struct pll_div *pll_div, unsigned int target,
		       unsigned int source)
{
	u64 Kpart;
	unsigned long int K, Ndiv, Nmod;

	pll_div->div2 = 0;
	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div->div2 = 1;
		Ndiv = target / source;
	}

	if (Ndiv < 6 || Ndiv > 12) {
		printk(KERN_ERR "%s: WM8985 N value is not within"
		       " the recommended range: %lu\n", __func__, Ndiv);
		return -EINVAL;
	}
	pll_div->n = Ndiv;

	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (u64)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xffffffff;
	if ((K % 10) >= 5)
		K += 5;
	K /= 10;
	pll_div->k = K;

	return 0;
}

static int wm8985_set_pll(struct snd_soc_dai *dai, int pll_id,
			  int source, unsigned int freq_in,
			  unsigned int freq_out)
{
	int ret;
	struct snd_soc_codec *codec;
	struct pll_div pll_div;

	codec = dai->codec;
	if (freq_in && freq_out) {
		ret = pll_factors(&pll_div, freq_out * 4 * 2, freq_in);
		if (ret)
			return ret;
	}

	/* disable the PLL before reprogramming it */
	snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
			    WM8985_PLLEN_MASK, 0);
	
	if (!freq_in || !freq_out)
		return 0;

	/* set PLLN and PRESCALE */
	snd_soc_write(codec, WM8985_PLL_N,
		      (pll_div.div2 << WM8985_PLL_PRESCALE_SHIFT)
		      | pll_div.n);
	/* set PLLK */
	snd_soc_write(codec, WM8985_PLL_K_3, pll_div.k & 0x1ff);
	snd_soc_write(codec, WM8985_PLL_K_2, (pll_div.k >> 9) & 0x1ff);
	snd_soc_write(codec, WM8985_PLL_K_1, (pll_div.k >> 18));
	/* set the source of the clock to be the PLL */
	snd_soc_update_bits(codec, WM8985_CLOCK_GEN_CONTROL,
			    WM8985_CLKSEL_MASK, WM8985_CLKSEL);
	/* enable the PLL */
	snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
			    WM8985_PLLEN_MASK, WM8985_PLLEN);
	return 0;
}

static int wm8985_set_sysclk(struct snd_soc_dai *dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec;
	struct wm8985_priv *wm8985;

	codec = dai->codec;
	wm8985 = snd_soc_codec_get_drvdata(codec);

	switch (clk_id) {
	case WM8985_CLKSRC_MCLK:
		snd_soc_update_bits(codec, WM8985_CLOCK_GEN_CONTROL,
				    WM8985_CLKSEL_MASK, 0);
		snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
				    WM8985_PLLEN_MASK, 0);
		break;
	case WM8985_CLKSRC_PLL:
		snd_soc_update_bits(codec, WM8985_CLOCK_GEN_CONTROL,
				    WM8985_CLKSEL_MASK, WM8985_CLKSEL);
		break;
	default:
		dev_err(dai->dev, "Unknown clock source %d\n", clk_id);
		return -EINVAL;
	}

	wm8985->sysclk = freq;
	return 0;
}

static void wm8985_sync_cache(struct snd_soc_codec *codec)
{
	short i;
	u16 *cache;

	if (!codec->cache_sync)
		return;
	codec->cache_only = 0;
	/* restore cache */
	cache = codec->reg_cache;
	for (i = 0; i < codec->driver->reg_cache_size; i++) {
		if (i == WM8985_SOFTWARE_RESET
				|| cache[i] == wm8985_reg_defs[i])
			continue;
		snd_soc_write(codec, i, cache[i]);
	}
	codec->cache_sync = 0;
}

static int wm8985_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	int ret;
	struct wm8985_priv *wm8985;

	wm8985 = snd_soc_codec_get_drvdata(codec);
	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		/* VMID at 75k */
		snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
				    WM8985_VMIDSEL_MASK,
				    1 << WM8985_VMIDSEL_SHIFT);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(wm8985->supplies),
						    wm8985->supplies);
			if (ret) {
				dev_err(codec->dev,
					"Failed to enable supplies: %d\n",
					ret);
				return ret;
			}

			wm8985_sync_cache(codec);

			/* enable anti-pop features */
			snd_soc_update_bits(codec, WM8985_OUT4_TO_ADC,
					    WM8985_POBCTRL_MASK,
					    WM8985_POBCTRL);
			/* enable thermal shutdown */
			snd_soc_update_bits(codec, WM8985_OUTPUT_CTRL0,
					    WM8985_TSDEN_MASK, WM8985_TSDEN);
			snd_soc_update_bits(codec, WM8985_OUTPUT_CTRL0,
					    WM8985_TSOPCTRL_MASK,
					    WM8985_TSOPCTRL);
			/* enable BIASEN */
			snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
					    WM8985_BIASEN_MASK, WM8985_BIASEN);
			/* VMID at 75k */
			snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
					    WM8985_VMIDSEL_MASK,
					    1 << WM8985_VMIDSEL_SHIFT);
			msleep(500);
			/* disable anti-pop features */
			snd_soc_update_bits(codec, WM8985_OUT4_TO_ADC,
					    WM8985_POBCTRL_MASK, 0);
		}
		/* VMID at 300k */
		snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
				    WM8985_VMIDSEL_MASK,
				    2 << WM8985_VMIDSEL_SHIFT);
		break;
	case SND_SOC_BIAS_OFF:
		/* disable thermal shutdown */
		snd_soc_update_bits(codec, WM8985_OUTPUT_CTRL0,
				    WM8985_TSOPCTRL_MASK, 0);
		snd_soc_update_bits(codec, WM8985_OUTPUT_CTRL0,
				    WM8985_TSDEN_MASK, 0);
		/* disable VMIDSEL and BIASEN */
		snd_soc_update_bits(codec, WM8985_POWER_MANAGEMENT_1,
				    WM8985_VMIDSEL_MASK | WM8985_BIASEN_MASK,
				    0);
		snd_soc_write(codec, WM8985_POWER_MANAGEMENT_1, 0);
		snd_soc_write(codec, WM8985_POWER_MANAGEMENT_2, 0);
		snd_soc_write(codec, WM8985_POWER_MANAGEMENT_3, 0);

		codec->cache_sync = 1;

		regulator_bulk_disable(ARRAY_SIZE(wm8985->supplies),
				       wm8985->supplies);
		break;
	}

	codec->bias_level = level;
	return 0;
}

#ifdef CONFIG_PM
static int wm8985_suspend(struct snd_soc_codec *codec, pm_message_t state)
{
	wm8985_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8985_resume(struct snd_soc_codec *codec)
{
	wm8985_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}
#else
#define wm8985_suspend NULL
#define wm8985_resume NULL
#endif

static int wm8985_remove(struct snd_soc_codec *codec)
{
	struct wm8985_priv *wm8985;

	wm8985 = snd_soc_codec_get_drvdata(codec);
	wm8985_set_bias_level(codec, SND_SOC_BIAS_OFF);
	regulator_bulk_free(ARRAY_SIZE(wm8985->supplies), wm8985->supplies);
	return 0;
}

static int wm8985_probe(struct snd_soc_codec *codec)
{
	size_t i;
	struct wm8985_priv *wm8985;
	int ret;
	u16 *cache;

	wm8985 = snd_soc_codec_get_drvdata(codec);

	ret = snd_soc_codec_set_cache_io(codec, 7, 9, wm8985->control_type);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cache i/o: %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(wm8985->supplies); i++)
		wm8985->supplies[i].supply = wm8985_supply_names[i];

	ret = regulator_bulk_get(codec->dev, ARRAY_SIZE(wm8985->supplies),
				 wm8985->supplies);
	if (ret) {
		dev_err(codec->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(ARRAY_SIZE(wm8985->supplies),
				    wm8985->supplies);
	if (ret) {
		dev_err(codec->dev, "Failed to enable supplies: %d\n", ret);
		goto err_reg_get;
	}

	ret = wm8985_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset: %d\n", ret);
		goto err_reg_enable;
	}

	cache = codec->reg_cache;
	/* latch volume update bits */
	for (i = 0; i < ARRAY_SIZE(volume_update_regs); ++i)
		cache[volume_update_regs[i]] |= 0x100;
	/* enable BIASCUT */
	cache[WM8985_BIAS_CTRL] |= WM8985_BIASCUT;
	codec->cache_sync = 1;

	snd_soc_add_controls(codec, wm8985_snd_controls,
			     ARRAY_SIZE(wm8985_snd_controls));
	wm8985_add_widgets(codec);

	wm8985_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;

err_reg_enable:
	regulator_bulk_disable(ARRAY_SIZE(wm8985->supplies), wm8985->supplies);
err_reg_get:
	regulator_bulk_free(ARRAY_SIZE(wm8985->supplies), wm8985->supplies);
	return ret;
}

static struct snd_soc_dai_ops wm8985_dai_ops = {
	.digital_mute = wm8985_dac_mute,
	.hw_params = wm8985_hw_params,
	.set_fmt = wm8985_set_fmt,
	.set_sysclk = wm8985_set_sysclk,
	.set_pll = wm8985_set_pll
};

#define WM8985_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver wm8985_dai = {
	.name = "wm8985-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = WM8985_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = WM8985_FORMATS,
	},
	.ops = &wm8985_dai_ops,
	.symmetric_rates = 1
};

static struct snd_soc_codec_driver soc_codec_dev_wm8985 = {
	.probe = wm8985_probe,
	.remove = wm8985_remove,
	.suspend = wm8985_suspend,
	.resume = wm8985_resume,
	.set_bias_level = wm8985_set_bias_level,
	.reg_cache_size = ARRAY_SIZE(wm8985_reg_defs),
	.reg_word_size = sizeof(u16),
	.reg_cache_default = wm8985_reg_defs
};

#if defined(CONFIG_SPI_MASTER)
static int __devinit wm8985_spi_probe(struct spi_device *spi)
{
	struct wm8985_priv *wm8985;
	int ret;

	wm8985 = kzalloc(sizeof *wm8985, GFP_KERNEL);
	if (IS_ERR(wm8985))
		return PTR_ERR(wm8985);

	wm8985->control_type = SND_SOC_SPI;
	spi_set_drvdata(spi, wm8985);

	ret = snd_soc_register_codec(&spi->dev,
				     &soc_codec_dev_wm8985, &wm8985_dai, 1);
	if (ret < 0)
		kfree(wm8985);
	return ret;
}

static int __devexit wm8985_spi_remove(struct spi_device *spi)
{
	snd_soc_unregister_codec(&spi->dev);
	kfree(spi_get_drvdata(spi));
	return 0;
}

static struct spi_driver wm8985_spi_driver = {
	.driver = {
		.name = "wm8985",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = wm8985_spi_probe,
	.remove = __devexit_p(wm8985_spi_remove)
};
#endif

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8985_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8985_priv *wm8985;
	int ret;

	wm8985 = kzalloc(sizeof *wm8985, GFP_KERNEL);
	if (IS_ERR(wm8985))
		return PTR_ERR(wm8985);

	wm8985->control_type = SND_SOC_I2C;
	i2c_set_clientdata(i2c, wm8985);

	ret = snd_soc_register_codec(&i2c->dev,
				     &soc_codec_dev_wm8985, &wm8985_dai, 1);
	if (ret < 0)
		kfree(wm8985);
	return ret;
}

static __devexit int wm8985_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	kfree(i2c_get_clientdata(client));
	return 0;
}

static const struct i2c_device_id wm8985_i2c_id[] = {
	{ "wm8985", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8985_i2c_id);

static struct i2c_driver wm8985_i2c_driver = {
	.driver = {
		.name = "wm8985",
		.owner = THIS_MODULE,
	},
	.probe = wm8985_i2c_probe,
	.remove = __devexit_p(wm8985_i2c_remove),
	.id_table = wm8985_i2c_id
};
#endif

static int __init wm8985_modinit(void)
{
	int ret = 0;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8985_i2c_driver);
	if (ret) {
		printk(KERN_ERR "Failed to register wm8985 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8985_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8985 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8985_modinit);

static void __exit wm8985_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8985_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8985_spi_driver);
#endif
}
module_exit(wm8985_exit);

MODULE_DESCRIPTION("ASoC WM8985 driver");
MODULE_AUTHOR("Dimitris Papastamos <dp@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
