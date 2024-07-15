// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm8983.c  --  WM8983 ALSA SoC Audio driver
 *
 * Copyright 2011 Wolfson Microelectronics plc
 *
 * Author: Dimitris Papastamos <dp@opensource.wolfsonmicro.com>
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8983.h"

static const struct reg_default wm8983_defaults[] = {
	{ 0x01, 0x0000 },     /* R1  - Power management 1 */
	{ 0x02, 0x0000 },     /* R2  - Power management 2 */
	{ 0x03, 0x0000 },     /* R3  - Power management 3 */
	{ 0x04, 0x0050 },     /* R4  - Audio Interface */
	{ 0x05, 0x0000 },     /* R5  - Companding control */
	{ 0x06, 0x0140 },     /* R6  - Clock Gen control */
	{ 0x07, 0x0000 },     /* R7  - Additional control */
	{ 0x08, 0x0000 },     /* R8  - GPIO Control */
	{ 0x09, 0x0000 },     /* R9  - Jack Detect Control 1 */
	{ 0x0A, 0x0000 },     /* R10 - DAC Control */
	{ 0x0B, 0x00FF },     /* R11 - Left DAC digital Vol */
	{ 0x0C, 0x00FF },     /* R12 - Right DAC digital vol */
	{ 0x0D, 0x0000 },     /* R13 - Jack Detect Control 2 */
	{ 0x0E, 0x0100 },     /* R14 - ADC Control */
	{ 0x0F, 0x00FF },     /* R15 - Left ADC Digital Vol */
	{ 0x10, 0x00FF },     /* R16 - Right ADC Digital Vol */
	{ 0x12, 0x012C },     /* R18 - EQ1 - low shelf */
	{ 0x13, 0x002C },     /* R19 - EQ2 - peak 1 */
	{ 0x14, 0x002C },     /* R20 - EQ3 - peak 2 */
	{ 0x15, 0x002C },     /* R21 - EQ4 - peak 3 */
	{ 0x16, 0x002C },     /* R22 - EQ5 - high shelf */
	{ 0x18, 0x0032 },     /* R24 - DAC Limiter 1 */
	{ 0x19, 0x0000 },     /* R25 - DAC Limiter 2 */
	{ 0x1B, 0x0000 },     /* R27 - Notch Filter 1 */
	{ 0x1C, 0x0000 },     /* R28 - Notch Filter 2 */
	{ 0x1D, 0x0000 },     /* R29 - Notch Filter 3 */
	{ 0x1E, 0x0000 },     /* R30 - Notch Filter 4 */
	{ 0x20, 0x0038 },     /* R32 - ALC control 1 */
	{ 0x21, 0x000B },     /* R33 - ALC control 2 */
	{ 0x22, 0x0032 },     /* R34 - ALC control 3 */
	{ 0x23, 0x0000 },     /* R35 - Noise Gate */
	{ 0x24, 0x0008 },     /* R36 - PLL N */
	{ 0x25, 0x000C },     /* R37 - PLL K 1 */
	{ 0x26, 0x0093 },     /* R38 - PLL K 2 */
	{ 0x27, 0x00E9 },     /* R39 - PLL K 3 */
	{ 0x29, 0x0000 },     /* R41 - 3D control */
	{ 0x2A, 0x0000 },     /* R42 - OUT4 to ADC */
	{ 0x2B, 0x0000 },     /* R43 - Beep control */
	{ 0x2C, 0x0033 },     /* R44 - Input ctrl */
	{ 0x2D, 0x0010 },     /* R45 - Left INP PGA gain ctrl */
	{ 0x2E, 0x0010 },     /* R46 - Right INP PGA gain ctrl */
	{ 0x2F, 0x0100 },     /* R47 - Left ADC BOOST ctrl */
	{ 0x30, 0x0100 },     /* R48 - Right ADC BOOST ctrl */
	{ 0x31, 0x0002 },     /* R49 - Output ctrl */
	{ 0x32, 0x0001 },     /* R50 - Left mixer ctrl */
	{ 0x33, 0x0001 },     /* R51 - Right mixer ctrl */
	{ 0x34, 0x0039 },     /* R52 - LOUT1 (HP) volume ctrl */
	{ 0x35, 0x0039 },     /* R53 - ROUT1 (HP) volume ctrl */
	{ 0x36, 0x0039 },     /* R54 - LOUT2 (SPK) volume ctrl */
	{ 0x37, 0x0039 },     /* R55 - ROUT2 (SPK) volume ctrl */
	{ 0x38, 0x0001 },     /* R56 - OUT3 mixer ctrl */
	{ 0x39, 0x0001 },     /* R57 - OUT4 (MONO) mix ctrl */
	{ 0x3D, 0x0000 },      /* R61 - BIAS CTRL */
};

/* vol/gain update regs */
static const int vol_update_regs[] = {
	WM8983_LEFT_DAC_DIGITAL_VOL,
	WM8983_RIGHT_DAC_DIGITAL_VOL,
	WM8983_LEFT_ADC_DIGITAL_VOL,
	WM8983_RIGHT_ADC_DIGITAL_VOL,
	WM8983_LOUT1_HP_VOLUME_CTRL,
	WM8983_ROUT1_HP_VOLUME_CTRL,
	WM8983_LOUT2_SPK_VOLUME_CTRL,
	WM8983_ROUT2_SPK_VOLUME_CTRL,
	WM8983_LEFT_INP_PGA_GAIN_CTRL,
	WM8983_RIGHT_INP_PGA_GAIN_CTRL
};

struct wm8983_priv {
	struct regmap *regmap;
	u32 sysclk;
	u32 bclk;
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
static SOC_ENUM_SINGLE_DECL(alc_sel, WM8983_ALC_CONTROL_1, 7, alc_sel_text);

static const char *alc_mode_text[] = { "ALC", "Limiter" };
static SOC_ENUM_SINGLE_DECL(alc_mode, WM8983_ALC_CONTROL_3, 8, alc_mode_text);

static const char *filter_mode_text[] = { "Audio", "Application" };
static SOC_ENUM_SINGLE_DECL(filter_mode, WM8983_ADC_CONTROL, 7,
			    filter_mode_text);

static const char *eq_bw_text[] = { "Narrow", "Wide" };
static const char *eqmode_text[] = { "Capture", "Playback" };
static SOC_ENUM_SINGLE_EXT_DECL(eqmode, eqmode_text);

static const char *eq1_cutoff_text[] = {
	"80Hz", "105Hz", "135Hz", "175Hz"
};
static SOC_ENUM_SINGLE_DECL(eq1_cutoff, WM8983_EQ1_LOW_SHELF, 5,
			    eq1_cutoff_text);
static const char *eq2_cutoff_text[] = {
	"230Hz", "300Hz", "385Hz", "500Hz"
};
static SOC_ENUM_SINGLE_DECL(eq2_bw, WM8983_EQ2_PEAK_1, 8, eq_bw_text);
static SOC_ENUM_SINGLE_DECL(eq2_cutoff, WM8983_EQ2_PEAK_1, 5, eq2_cutoff_text);
static const char *eq3_cutoff_text[] = {
	"650Hz", "850Hz", "1.1kHz", "1.4kHz"
};
static SOC_ENUM_SINGLE_DECL(eq3_bw, WM8983_EQ3_PEAK_2, 8, eq_bw_text);
static SOC_ENUM_SINGLE_DECL(eq3_cutoff, WM8983_EQ3_PEAK_2, 5, eq3_cutoff_text);
static const char *eq4_cutoff_text[] = {
	"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz"
};
static SOC_ENUM_SINGLE_DECL(eq4_bw, WM8983_EQ4_PEAK_3, 8, eq_bw_text);
static SOC_ENUM_SINGLE_DECL(eq4_cutoff, WM8983_EQ4_PEAK_3, 5, eq4_cutoff_text);
static const char *eq5_cutoff_text[] = {
	"5.3kHz", "6.9kHz", "9kHz", "11.7kHz"
};
static SOC_ENUM_SINGLE_DECL(eq5_cutoff, WM8983_EQ5_HIGH_SHELF, 5,
			    eq5_cutoff_text);

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
static SOC_ENUM_SINGLE_DECL(depth_3d, WM8983_3D_CONTROL, 0,
			    depth_3d_text);

static const struct snd_kcontrol_new wm8983_snd_controls[] = {
	SOC_SINGLE("Digital Loopback Switch", WM8983_COMPANDING_CONTROL,
		   0, 1, 0),

	SOC_ENUM("ALC Capture Function", alc_sel),
	SOC_SINGLE_TLV("ALC Capture Max Volume", WM8983_ALC_CONTROL_1,
		       3, 7, 0, alc_max_tlv),
	SOC_SINGLE_TLV("ALC Capture Min Volume", WM8983_ALC_CONTROL_1,
		       0, 7, 0, alc_min_tlv),
	SOC_SINGLE_TLV("ALC Capture Target Volume", WM8983_ALC_CONTROL_2,
		       0, 15, 0, alc_tar_tlv),
	SOC_SINGLE("ALC Capture Attack", WM8983_ALC_CONTROL_3, 0, 10, 0),
	SOC_SINGLE("ALC Capture Hold", WM8983_ALC_CONTROL_2, 4, 10, 0),
	SOC_SINGLE("ALC Capture Decay", WM8983_ALC_CONTROL_3, 4, 10, 0),
	SOC_ENUM("ALC Mode", alc_mode),
	SOC_SINGLE("ALC Capture NG Switch", WM8983_NOISE_GATE,
		   3, 1, 0),
	SOC_SINGLE("ALC Capture NG Threshold", WM8983_NOISE_GATE,
		   0, 7, 1),

	SOC_DOUBLE_R_TLV("Capture Volume", WM8983_LEFT_ADC_DIGITAL_VOL,
			 WM8983_RIGHT_ADC_DIGITAL_VOL, 0, 255, 0, adc_tlv),
	SOC_DOUBLE_R("Capture PGA ZC Switch", WM8983_LEFT_INP_PGA_GAIN_CTRL,
		     WM8983_RIGHT_INP_PGA_GAIN_CTRL, 7, 1, 0),
	SOC_DOUBLE_R_TLV("Capture PGA Volume", WM8983_LEFT_INP_PGA_GAIN_CTRL,
			 WM8983_RIGHT_INP_PGA_GAIN_CTRL, 0, 63, 0, pga_vol_tlv),

	SOC_DOUBLE_R_TLV("Capture PGA Boost Volume",
			 WM8983_LEFT_ADC_BOOST_CTRL, WM8983_RIGHT_ADC_BOOST_CTRL,
			 8, 1, 0, pga_boost_tlv),

	SOC_DOUBLE("ADC Inversion Switch", WM8983_ADC_CONTROL, 0, 1, 1, 0),
	SOC_SINGLE("ADC 128x Oversampling Switch", WM8983_ADC_CONTROL, 8, 1, 0),

	SOC_DOUBLE_R_TLV("Playback Volume", WM8983_LEFT_DAC_DIGITAL_VOL,
			 WM8983_RIGHT_DAC_DIGITAL_VOL, 0, 255, 0, dac_tlv),

	SOC_SINGLE("DAC Playback Limiter Switch", WM8983_DAC_LIMITER_1, 8, 1, 0),
	SOC_SINGLE("DAC Playback Limiter Decay", WM8983_DAC_LIMITER_1, 4, 10, 0),
	SOC_SINGLE("DAC Playback Limiter Attack", WM8983_DAC_LIMITER_1, 0, 11, 0),
	SOC_SINGLE_TLV("DAC Playback Limiter Threshold", WM8983_DAC_LIMITER_2,
		       4, 7, 1, lim_thresh_tlv),
	SOC_SINGLE_TLV("DAC Playback Limiter Boost Volume", WM8983_DAC_LIMITER_2,
		       0, 12, 0, lim_boost_tlv),
	SOC_DOUBLE("DAC Inversion Switch", WM8983_DAC_CONTROL, 0, 1, 1, 0),
	SOC_SINGLE("DAC Auto Mute Switch", WM8983_DAC_CONTROL, 2, 1, 0),
	SOC_SINGLE("DAC 128x Oversampling Switch", WM8983_DAC_CONTROL, 3, 1, 0),

	SOC_DOUBLE_R_TLV("Headphone Playback Volume", WM8983_LOUT1_HP_VOLUME_CTRL,
			 WM8983_ROUT1_HP_VOLUME_CTRL, 0, 63, 0, out_tlv),
	SOC_DOUBLE_R("Headphone Playback ZC Switch", WM8983_LOUT1_HP_VOLUME_CTRL,
		     WM8983_ROUT1_HP_VOLUME_CTRL, 7, 1, 0),
	SOC_DOUBLE_R("Headphone Switch", WM8983_LOUT1_HP_VOLUME_CTRL,
		     WM8983_ROUT1_HP_VOLUME_CTRL, 6, 1, 1),

	SOC_DOUBLE_R_TLV("Speaker Playback Volume", WM8983_LOUT2_SPK_VOLUME_CTRL,
			 WM8983_ROUT2_SPK_VOLUME_CTRL, 0, 63, 0, out_tlv),
	SOC_DOUBLE_R("Speaker Playback ZC Switch", WM8983_LOUT2_SPK_VOLUME_CTRL,
		     WM8983_ROUT2_SPK_VOLUME_CTRL, 7, 1, 0),
	SOC_DOUBLE_R("Speaker Switch", WM8983_LOUT2_SPK_VOLUME_CTRL,
		     WM8983_ROUT2_SPK_VOLUME_CTRL, 6, 1, 1),

	SOC_SINGLE("OUT3 Switch", WM8983_OUT3_MIXER_CTRL,
		   6, 1, 1),

	SOC_SINGLE("OUT4 Switch", WM8983_OUT4_MONO_MIX_CTRL,
		   6, 1, 1),

	SOC_SINGLE("High Pass Filter Switch", WM8983_ADC_CONTROL, 8, 1, 0),
	SOC_ENUM("High Pass Filter Mode", filter_mode),
	SOC_SINGLE("High Pass Filter Cutoff", WM8983_ADC_CONTROL, 4, 7, 0),

	SOC_DOUBLE_R_TLV("Aux Bypass Volume",
			 WM8983_LEFT_MIXER_CTRL, WM8983_RIGHT_MIXER_CTRL, 6, 7, 0,
			 aux_tlv),

	SOC_DOUBLE_R_TLV("Input PGA Bypass Volume",
			 WM8983_LEFT_MIXER_CTRL, WM8983_RIGHT_MIXER_CTRL, 2, 7, 0,
			 bypass_tlv),

	SOC_ENUM_EXT("Equalizer Function", eqmode, eqmode_get, eqmode_put),
	SOC_ENUM("EQ1 Cutoff", eq1_cutoff),
	SOC_SINGLE_TLV("EQ1 Volume", WM8983_EQ1_LOW_SHELF,  0, 24, 1, eq_tlv),
	SOC_ENUM("EQ2 Bandwidth", eq2_bw),
	SOC_ENUM("EQ2 Cutoff", eq2_cutoff),
	SOC_SINGLE_TLV("EQ2 Volume", WM8983_EQ2_PEAK_1, 0, 24, 1, eq_tlv),
	SOC_ENUM("EQ3 Bandwidth", eq3_bw),
	SOC_ENUM("EQ3 Cutoff", eq3_cutoff),
	SOC_SINGLE_TLV("EQ3 Volume", WM8983_EQ3_PEAK_2, 0, 24, 1, eq_tlv),
	SOC_ENUM("EQ4 Bandwidth", eq4_bw),
	SOC_ENUM("EQ4 Cutoff", eq4_cutoff),
	SOC_SINGLE_TLV("EQ4 Volume", WM8983_EQ4_PEAK_3, 0, 24, 1, eq_tlv),
	SOC_ENUM("EQ5 Cutoff", eq5_cutoff),
	SOC_SINGLE_TLV("EQ5 Volume", WM8983_EQ5_HIGH_SHELF, 0, 24, 1, eq_tlv),

	SOC_ENUM("3D Depth", depth_3d),
};

static const struct snd_kcontrol_new left_out_mixer[] = {
	SOC_DAPM_SINGLE("Line Switch", WM8983_LEFT_MIXER_CTRL, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Switch", WM8983_LEFT_MIXER_CTRL, 5, 1, 0),
	SOC_DAPM_SINGLE("PCM Switch", WM8983_LEFT_MIXER_CTRL, 0, 1, 0),
};

static const struct snd_kcontrol_new right_out_mixer[] = {
	SOC_DAPM_SINGLE("Line Switch", WM8983_RIGHT_MIXER_CTRL, 1, 1, 0),
	SOC_DAPM_SINGLE("Aux Switch", WM8983_RIGHT_MIXER_CTRL, 5, 1, 0),
	SOC_DAPM_SINGLE("PCM Switch", WM8983_RIGHT_MIXER_CTRL, 0, 1, 0),
};

static const struct snd_kcontrol_new left_input_mixer[] = {
	SOC_DAPM_SINGLE("L2 Switch", WM8983_INPUT_CTRL, 2, 1, 0),
	SOC_DAPM_SINGLE("MicN Switch", WM8983_INPUT_CTRL, 1, 1, 0),
	SOC_DAPM_SINGLE("MicP Switch", WM8983_INPUT_CTRL, 0, 1, 0),
};

static const struct snd_kcontrol_new right_input_mixer[] = {
	SOC_DAPM_SINGLE("R2 Switch", WM8983_INPUT_CTRL, 6, 1, 0),
	SOC_DAPM_SINGLE("MicN Switch", WM8983_INPUT_CTRL, 5, 1, 0),
	SOC_DAPM_SINGLE("MicP Switch", WM8983_INPUT_CTRL, 4, 1, 0),
};

static const struct snd_kcontrol_new left_boost_mixer[] = {
	SOC_DAPM_SINGLE_TLV("L2 Volume", WM8983_LEFT_ADC_BOOST_CTRL,
			    4, 7, 0, boost_tlv),
	SOC_DAPM_SINGLE_TLV("AUXL Volume", WM8983_LEFT_ADC_BOOST_CTRL,
			    0, 7, 0, boost_tlv)
};

static const struct snd_kcontrol_new out3_mixer[] = {
	SOC_DAPM_SINGLE("LMIX2OUT3 Switch", WM8983_OUT3_MIXER_CTRL,
			1, 1, 0),
	SOC_DAPM_SINGLE("LDAC2OUT3 Switch", WM8983_OUT3_MIXER_CTRL,
			0, 1, 0),
};

static const struct snd_kcontrol_new out4_mixer[] = {
	SOC_DAPM_SINGLE("LMIX2OUT4 Switch", WM8983_OUT4_MONO_MIX_CTRL,
			4, 1, 0),
	SOC_DAPM_SINGLE("RMIX2OUT4 Switch", WM8983_OUT4_MONO_MIX_CTRL,
			1, 1, 0),
	SOC_DAPM_SINGLE("LDAC2OUT4 Switch", WM8983_OUT4_MONO_MIX_CTRL,
			3, 1, 0),
	SOC_DAPM_SINGLE("RDAC2OUT4 Switch", WM8983_OUT4_MONO_MIX_CTRL,
			0, 1, 0),
};

static const struct snd_kcontrol_new right_boost_mixer[] = {
	SOC_DAPM_SINGLE_TLV("R2 Volume", WM8983_RIGHT_ADC_BOOST_CTRL,
			    4, 7, 0, boost_tlv),
	SOC_DAPM_SINGLE_TLV("AUXR Volume", WM8983_RIGHT_ADC_BOOST_CTRL,
			    0, 7, 0, boost_tlv)
};

static const struct snd_soc_dapm_widget wm8983_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8983_POWER_MANAGEMENT_3,
			 0, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8983_POWER_MANAGEMENT_3,
			 1, 0),
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8983_POWER_MANAGEMENT_2,
			 0, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8983_POWER_MANAGEMENT_2,
			 1, 0),

	SND_SOC_DAPM_MIXER("Left Output Mixer", WM8983_POWER_MANAGEMENT_3,
			   2, 0, left_out_mixer, ARRAY_SIZE(left_out_mixer)),
	SND_SOC_DAPM_MIXER("Right Output Mixer", WM8983_POWER_MANAGEMENT_3,
			   3, 0, right_out_mixer, ARRAY_SIZE(right_out_mixer)),

	SND_SOC_DAPM_MIXER("Left Input Mixer", WM8983_POWER_MANAGEMENT_2,
			   2, 0, left_input_mixer, ARRAY_SIZE(left_input_mixer)),
	SND_SOC_DAPM_MIXER("Right Input Mixer", WM8983_POWER_MANAGEMENT_2,
			   3, 0, right_input_mixer, ARRAY_SIZE(right_input_mixer)),

	SND_SOC_DAPM_MIXER("Left Boost Mixer", WM8983_POWER_MANAGEMENT_2,
			   4, 0, left_boost_mixer, ARRAY_SIZE(left_boost_mixer)),
	SND_SOC_DAPM_MIXER("Right Boost Mixer", WM8983_POWER_MANAGEMENT_2,
			   5, 0, right_boost_mixer, ARRAY_SIZE(right_boost_mixer)),

	SND_SOC_DAPM_MIXER("OUT3 Mixer", WM8983_POWER_MANAGEMENT_1,
			   6, 0, out3_mixer, ARRAY_SIZE(out3_mixer)),

	SND_SOC_DAPM_MIXER("OUT4 Mixer", WM8983_POWER_MANAGEMENT_1,
			   7, 0, out4_mixer, ARRAY_SIZE(out4_mixer)),

	SND_SOC_DAPM_PGA("Left Capture PGA", WM8983_LEFT_INP_PGA_GAIN_CTRL,
			 6, 1, NULL, 0),
	SND_SOC_DAPM_PGA("Right Capture PGA", WM8983_RIGHT_INP_PGA_GAIN_CTRL,
			 6, 1, NULL, 0),

	SND_SOC_DAPM_PGA("Left Headphone Out", WM8983_POWER_MANAGEMENT_2,
			 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Headphone Out", WM8983_POWER_MANAGEMENT_2,
			 8, 0, NULL, 0),

	SND_SOC_DAPM_PGA("Left Speaker Out", WM8983_POWER_MANAGEMENT_3,
			 5, 0, NULL, 0),
	SND_SOC_DAPM_PGA("Right Speaker Out", WM8983_POWER_MANAGEMENT_3,
			 6, 0, NULL, 0),

	SND_SOC_DAPM_PGA("OUT3 Out", WM8983_POWER_MANAGEMENT_3,
			 7, 0, NULL, 0),

	SND_SOC_DAPM_PGA("OUT4 Out", WM8983_POWER_MANAGEMENT_3,
			 8, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Mic Bias", WM8983_POWER_MANAGEMENT_1, 4, 0,
			    NULL, 0),

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
	SND_SOC_DAPM_OUTPUT("SPKR"),
	SND_SOC_DAPM_OUTPUT("OUT3"),
	SND_SOC_DAPM_OUTPUT("OUT4")
};

static const struct snd_soc_dapm_route wm8983_audio_map[] = {
	{ "OUT3 Mixer", "LMIX2OUT3 Switch", "Left Output Mixer" },
	{ "OUT3 Mixer", "LDAC2OUT3 Switch", "Left DAC" },

	{ "OUT3 Out", NULL, "OUT3 Mixer" },
	{ "OUT3", NULL, "OUT3 Out" },

	{ "OUT4 Mixer", "LMIX2OUT4 Switch", "Left Output Mixer" },
	{ "OUT4 Mixer", "RMIX2OUT4 Switch", "Right Output Mixer" },
	{ "OUT4 Mixer", "LDAC2OUT4 Switch", "Left DAC" },
	{ "OUT4 Mixer", "RDAC2OUT4 Switch", "Right DAC" },

	{ "OUT4 Out", NULL, "OUT4 Mixer" },
	{ "OUT4", NULL, "OUT4 Out" },

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
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	unsigned int reg;

	reg = snd_soc_component_read(component, WM8983_EQ1_LOW_SHELF);
	if (reg & WM8983_EQ3DMODE)
		ucontrol->value.enumerated.item[0] = 1;
	else
		ucontrol->value.enumerated.item[0] = 0;

	return 0;
}

static int eqmode_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	unsigned int regpwr2, regpwr3;
	unsigned int reg_eq;

	if (ucontrol->value.enumerated.item[0] != 0
	    && ucontrol->value.enumerated.item[0] != 1)
		return -EINVAL;

	reg_eq = snd_soc_component_read(component, WM8983_EQ1_LOW_SHELF);
	switch ((reg_eq & WM8983_EQ3DMODE) >> WM8983_EQ3DMODE_SHIFT) {
	case 0:
		if (!ucontrol->value.enumerated.item[0])
			return 0;
		break;
	case 1:
		if (ucontrol->value.enumerated.item[0])
			return 0;
		break;
	}

	regpwr2 = snd_soc_component_read(component, WM8983_POWER_MANAGEMENT_2);
	regpwr3 = snd_soc_component_read(component, WM8983_POWER_MANAGEMENT_3);
	/* disable the DACs and ADCs */
	snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_2,
			    WM8983_ADCENR_MASK | WM8983_ADCENL_MASK, 0);
	snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_3,
			    WM8983_DACENR_MASK | WM8983_DACENL_MASK, 0);
	/* set the desired eqmode */
	snd_soc_component_update_bits(component, WM8983_EQ1_LOW_SHELF,
			    WM8983_EQ3DMODE_MASK,
			    ucontrol->value.enumerated.item[0]
			    << WM8983_EQ3DMODE_SHIFT);
	/* restore DAC/ADC configuration */
	snd_soc_component_write(component, WM8983_POWER_MANAGEMENT_2, regpwr2);
	snd_soc_component_write(component, WM8983_POWER_MANAGEMENT_3, regpwr3);
	return 0;
}

static bool wm8983_writeable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8983_SOFTWARE_RESET ... WM8983_RIGHT_ADC_DIGITAL_VOL:
	case WM8983_EQ1_LOW_SHELF ... WM8983_DAC_LIMITER_2:
	case WM8983_NOTCH_FILTER_1 ... WM8983_NOTCH_FILTER_4:
	case WM8983_ALC_CONTROL_1 ... WM8983_PLL_K_3:
	case WM8983_3D_CONTROL ... WM8983_OUT4_MONO_MIX_CTRL:
	case WM8983_BIAS_CTRL:
		return true;
	default:
		return false;
	}
}

static int wm8983_dac_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;

	return snd_soc_component_update_bits(component, WM8983_DAC_CONTROL,
				   WM8983_SOFTMUTE_MASK,
				   !!mute << WM8983_SOFTMUTE_SHIFT);
}

static int wm8983_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	u16 format, master, bcp, lrp;

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

	snd_soc_component_update_bits(component, WM8983_AUDIO_INTERFACE,
			    WM8983_FMT_MASK, format << WM8983_FMT_SHIFT);

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

	snd_soc_component_update_bits(component, WM8983_CLOCK_GEN_CONTROL,
			    WM8983_MS_MASK, master << WM8983_MS_SHIFT);

	/* FIXME: We don't currently support DSP A/B modes */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
	case SND_SOC_DAIFMT_DSP_B:
		dev_err(dai->dev, "DSP A/B modes are not supported\n");
		return -EINVAL;
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

	snd_soc_component_update_bits(component, WM8983_AUDIO_INTERFACE,
			    WM8983_LRCP_MASK, lrp << WM8983_LRCP_SHIFT);
	snd_soc_component_update_bits(component, WM8983_AUDIO_INTERFACE,
			    WM8983_BCP_MASK, bcp << WM8983_BCP_SHIFT);
	return 0;
}

static int wm8983_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	int i;
	struct snd_soc_component *component = dai->component;
	struct wm8983_priv *wm8983 = snd_soc_component_get_drvdata(component);
	u16 blen, srate_idx;
	u32 tmp;
	int srate_best;
	int ret;

	ret = snd_soc_params_to_bclk(params);
	if (ret < 0) {
		dev_err(component->dev, "Failed to convert params to bclk: %d\n", ret);
		return ret;
	}

	wm8983->bclk = ret;

	switch (params_width(params)) {
	case 16:
		blen = 0x0;
		break;
	case 20:
		blen = 0x1;
		break;
	case 24:
		blen = 0x2;
		break;
	case 32:
		blen = 0x3;
		break;
	default:
		dev_err(dai->dev, "Unsupported word length %u\n",
			params_width(params));
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, WM8983_AUDIO_INTERFACE,
			    WM8983_WL_MASK, blen << WM8983_WL_SHIFT);

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
	snd_soc_component_update_bits(component, WM8983_ADDITIONAL_CONTROL,
			    WM8983_SR_MASK, srate_idx << WM8983_SR_SHIFT);

	dev_dbg(dai->dev, "Target BCLK = %uHz\n", wm8983->bclk);
	dev_dbg(dai->dev, "SYSCLK = %uHz\n", wm8983->sysclk);

	for (i = 0; i < ARRAY_SIZE(fs_ratios); ++i) {
		if (wm8983->sysclk / params_rate(params)
		    == fs_ratios[i].ratio)
			break;
	}

	if (i == ARRAY_SIZE(fs_ratios)) {
		dev_err(dai->dev, "Unable to configure MCLK ratio %u/%u\n",
			wm8983->sysclk, params_rate(params));
		return -EINVAL;
	}

	dev_dbg(dai->dev, "MCLK ratio = %dfs\n", fs_ratios[i].ratio);
	snd_soc_component_update_bits(component, WM8983_CLOCK_GEN_CONTROL,
			    WM8983_MCLKDIV_MASK, i << WM8983_MCLKDIV_SHIFT);

	/* select the appropriate bclk divider */
	tmp = (wm8983->sysclk / fs_ratios[i].div) * 10;
	for (i = 0; i < ARRAY_SIZE(bclk_divs); ++i) {
		if (wm8983->bclk == tmp / bclk_divs[i])
			break;
	}

	if (i == ARRAY_SIZE(bclk_divs)) {
		dev_err(dai->dev, "No matching BCLK divider found\n");
		return -EINVAL;
	}

	dev_dbg(dai->dev, "BCLK div = %d\n", i);
	snd_soc_component_update_bits(component, WM8983_CLOCK_GEN_CONTROL,
			    WM8983_BCLKDIV_MASK, i << WM8983_BCLKDIV_SHIFT);

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
		printk(KERN_ERR "%s: WM8983 N value is not within"
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

static int wm8983_set_pll(struct snd_soc_dai *dai, int pll_id,
			  int source, unsigned int freq_in,
			  unsigned int freq_out)
{
	int ret;
	struct snd_soc_component *component;
	struct pll_div pll_div;

	component = dai->component;
	if (!freq_in || !freq_out) {
		/* disable the PLL */
		snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
				    WM8983_PLLEN_MASK, 0);
		return 0;
	} else {
		ret = pll_factors(&pll_div, freq_out * 4 * 2, freq_in);
		if (ret)
			return ret;

		/* disable the PLL before re-programming it */
		snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
				    WM8983_PLLEN_MASK, 0);

		/* set PLLN and PRESCALE */
		snd_soc_component_write(component, WM8983_PLL_N,
			(pll_div.div2 << WM8983_PLL_PRESCALE_SHIFT)
			| pll_div.n);
		/* set PLLK */
		snd_soc_component_write(component, WM8983_PLL_K_3, pll_div.k & 0x1ff);
		snd_soc_component_write(component, WM8983_PLL_K_2, (pll_div.k >> 9) & 0x1ff);
		snd_soc_component_write(component, WM8983_PLL_K_1, (pll_div.k >> 18));
		/* enable the PLL */
		snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
					WM8983_PLLEN_MASK, WM8983_PLLEN);
	}

	return 0;
}

static int wm8983_set_sysclk(struct snd_soc_dai *dai,
			     int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct wm8983_priv *wm8983 = snd_soc_component_get_drvdata(component);

	switch (clk_id) {
	case WM8983_CLKSRC_MCLK:
		snd_soc_component_update_bits(component, WM8983_CLOCK_GEN_CONTROL,
				    WM8983_CLKSEL_MASK, 0);
		break;
	case WM8983_CLKSRC_PLL:
		snd_soc_component_update_bits(component, WM8983_CLOCK_GEN_CONTROL,
				    WM8983_CLKSEL_MASK, WM8983_CLKSEL);
		break;
	default:
		dev_err(dai->dev, "Unknown clock source: %d\n", clk_id);
		return -EINVAL;
	}

	wm8983->sysclk = freq;
	return 0;
}

static int wm8983_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8983_priv *wm8983 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
		/* VMID at 100k */
		snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
				    WM8983_VMIDSEL_MASK,
				    1 << WM8983_VMIDSEL_SHIFT);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regcache_sync(wm8983->regmap);
			if (ret < 0) {
				dev_err(component->dev, "Failed to sync cache: %d\n", ret);
				return ret;
			}
			/* enable anti-pop features */
			snd_soc_component_update_bits(component, WM8983_OUT4_TO_ADC,
					    WM8983_POBCTRL_MASK | WM8983_DELEN_MASK,
					    WM8983_POBCTRL | WM8983_DELEN);
			/* enable thermal shutdown */
			snd_soc_component_update_bits(component, WM8983_OUTPUT_CTRL,
					    WM8983_TSDEN_MASK, WM8983_TSDEN);
			/* enable BIASEN */
			snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
					    WM8983_BIASEN_MASK, WM8983_BIASEN);
			/* VMID at 100k */
			snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
					    WM8983_VMIDSEL_MASK,
					    1 << WM8983_VMIDSEL_SHIFT);
			msleep(250);
			/* disable anti-pop features */
			snd_soc_component_update_bits(component, WM8983_OUT4_TO_ADC,
					    WM8983_POBCTRL_MASK |
					    WM8983_DELEN_MASK, 0);
		}

		/* VMID at 500k */
		snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
				    WM8983_VMIDSEL_MASK,
				    2 << WM8983_VMIDSEL_SHIFT);
		break;
	case SND_SOC_BIAS_OFF:
		/* disable thermal shutdown */
		snd_soc_component_update_bits(component, WM8983_OUTPUT_CTRL,
				    WM8983_TSDEN_MASK, 0);
		/* disable VMIDSEL and BIASEN */
		snd_soc_component_update_bits(component, WM8983_POWER_MANAGEMENT_1,
				    WM8983_VMIDSEL_MASK | WM8983_BIASEN_MASK,
				    0);
		/* wait for VMID to discharge */
		msleep(100);
		snd_soc_component_write(component, WM8983_POWER_MANAGEMENT_1, 0);
		snd_soc_component_write(component, WM8983_POWER_MANAGEMENT_2, 0);
		snd_soc_component_write(component, WM8983_POWER_MANAGEMENT_3, 0);
		break;
	}

	return 0;
}

static int wm8983_probe(struct snd_soc_component *component)
{
	int ret;
	int i;

	ret = snd_soc_component_write(component, WM8983_SOFTWARE_RESET, 0);
	if (ret < 0) {
		dev_err(component->dev, "Failed to issue reset: %d\n", ret);
		return ret;
	}

	/* set the vol/gain update bits */
	for (i = 0; i < ARRAY_SIZE(vol_update_regs); ++i)
		snd_soc_component_update_bits(component, vol_update_regs[i],
				    0x100, 0x100);

	/* mute all outputs and set PGAs to minimum gain */
	for (i = WM8983_LOUT1_HP_VOLUME_CTRL;
	     i <= WM8983_OUT4_MONO_MIX_CTRL; ++i)
		snd_soc_component_update_bits(component, i, 0x40, 0x40);

	/* enable soft mute */
	snd_soc_component_update_bits(component, WM8983_DAC_CONTROL,
			    WM8983_SOFTMUTE_MASK,
			    WM8983_SOFTMUTE);

	/* enable BIASCUT */
	snd_soc_component_update_bits(component, WM8983_BIAS_CTRL,
			    WM8983_BIASCUT, WM8983_BIASCUT);
	return 0;
}

static const struct snd_soc_dai_ops wm8983_dai_ops = {
	.mute_stream = wm8983_dac_mute,
	.hw_params = wm8983_hw_params,
	.set_fmt = wm8983_set_fmt,
	.set_sysclk = wm8983_set_sysclk,
	.set_pll = wm8983_set_pll,
	.no_capture_mute = 1,
};

#define WM8983_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver wm8983_dai = {
	.name = "wm8983-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = WM8983_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = WM8983_FORMATS,
	},
	.ops = &wm8983_dai_ops,
	.symmetric_rate = 1
};

static const struct snd_soc_component_driver soc_component_dev_wm8983 = {
	.probe			= wm8983_probe,
	.set_bias_level		= wm8983_set_bias_level,
	.controls		= wm8983_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8983_snd_controls),
	.dapm_widgets		= wm8983_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8983_dapm_widgets),
	.dapm_routes		= wm8983_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(wm8983_audio_map),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static const struct regmap_config wm8983_regmap = {
	.reg_bits = 7,
	.val_bits = 9,

	.reg_defaults = wm8983_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8983_defaults),
	.cache_type = REGCACHE_MAPLE,
	.max_register = WM8983_MAX_REGISTER,

	.writeable_reg = wm8983_writeable,
};

#if defined(CONFIG_SPI_MASTER)
static int wm8983_spi_probe(struct spi_device *spi)
{
	struct wm8983_priv *wm8983;
	int ret;

	wm8983 = devm_kzalloc(&spi->dev, sizeof *wm8983, GFP_KERNEL);
	if (!wm8983)
		return -ENOMEM;

	wm8983->regmap = devm_regmap_init_spi(spi, &wm8983_regmap);
	if (IS_ERR(wm8983->regmap)) {
		ret = PTR_ERR(wm8983->regmap);
		dev_err(&spi->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	spi_set_drvdata(spi, wm8983);

	ret = devm_snd_soc_register_component(&spi->dev,
				&soc_component_dev_wm8983, &wm8983_dai, 1);
	return ret;
}

static struct spi_driver wm8983_spi_driver = {
	.driver = {
		.name = "wm8983",
	},
	.probe = wm8983_spi_probe,
};
#endif

#if IS_ENABLED(CONFIG_I2C)
static int wm8983_i2c_probe(struct i2c_client *i2c)
{
	struct wm8983_priv *wm8983;
	int ret;

	wm8983 = devm_kzalloc(&i2c->dev, sizeof *wm8983, GFP_KERNEL);
	if (!wm8983)
		return -ENOMEM;

	wm8983->regmap = devm_regmap_init_i2c(i2c, &wm8983_regmap);
	if (IS_ERR(wm8983->regmap)) {
		ret = PTR_ERR(wm8983->regmap);
		dev_err(&i2c->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, wm8983);

	ret = devm_snd_soc_register_component(&i2c->dev,
				&soc_component_dev_wm8983, &wm8983_dai, 1);

	return ret;
}

static const struct i2c_device_id wm8983_i2c_id[] = {
	{ "wm8983", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8983_i2c_id);

static struct i2c_driver wm8983_i2c_driver = {
	.driver = {
		.name = "wm8983",
	},
	.probe = wm8983_i2c_probe,
	.id_table = wm8983_i2c_id
};
#endif

static int __init wm8983_modinit(void)
{
	int ret = 0;

#if IS_ENABLED(CONFIG_I2C)
	ret = i2c_add_driver(&wm8983_i2c_driver);
	if (ret) {
		printk(KERN_ERR "Failed to register wm8983 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8983_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register wm8983 SPI driver: %d\n",
		       ret);
	}
#endif
	return ret;
}
module_init(wm8983_modinit);

static void __exit wm8983_exit(void)
{
#if IS_ENABLED(CONFIG_I2C)
	i2c_del_driver(&wm8983_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8983_spi_driver);
#endif
}
module_exit(wm8983_exit);

MODULE_DESCRIPTION("ASoC WM8983 driver");
MODULE_AUTHOR("Dimitris Papastamos <dp@opensource.wolfsonmicro.com>");
MODULE_LICENSE("GPL");
