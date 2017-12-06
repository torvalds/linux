/*
 * alc5623.c  --  alc562[123] ALSA Soc Audio driver
 *
 * Copyright 2008 Realtek Microelectronics
 * Author: flove <flove@realtek.com> Ethan <eku@marvell.com>
 *
 * Copyright 2010 Arnaud Patard <arnaud.patard@rtp-net.org>
 *
 *
 * Based on WM8753.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/alc5623.h>

#include "alc5623.h"

static int caps_charge = 2000;
module_param(caps_charge, int, 0);
MODULE_PARM_DESC(caps_charge, "ALC5623 cap charge time (msecs)");

/* codec private data */
struct alc5623_priv {
	struct regmap *regmap;
	u8 id;
	unsigned int sysclk;
	unsigned int add_ctrl;
	unsigned int jack_det_ctrl;
};

static inline int alc5623_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, ALC5623_RESET, 0);
}

static int amp_mixer_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	/* to power-on/off class-d amp generators/speaker */
	/* need to write to 'index-46h' register :        */
	/* so write index num (here 0x46) to reg 0x6a     */
	/* and then 0xffff/0 to reg 0x6c                  */
	snd_soc_write(codec, ALC5623_HID_CTRL_INDEX, 0x46);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_write(codec, ALC5623_HID_CTRL_DATA, 0xFFFF);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_write(codec, ALC5623_HID_CTRL_DATA, 0);
		break;
	}

	return 0;
}

/*
 * ALC5623 Controls
 */

static const DECLARE_TLV_DB_SCALE(vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(hp_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_rec_tlv, -1650, 150, 0);
static const DECLARE_TLV_DB_RANGE(boost_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(3000, 0, 0)
);
static const DECLARE_TLV_DB_SCALE(dig_tlv, 0, 600, 0);

static const struct snd_kcontrol_new alc5621_vol_snd_controls[] = {
	SOC_DOUBLE_TLV("Speaker Playback Volume",
			ALC5623_SPK_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Speaker Playback Switch",
			ALC5623_SPK_OUT_VOL, 15, 7, 1, 1),
	SOC_DOUBLE_TLV("Headphone Playback Volume",
			ALC5623_HP_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Headphone Playback Switch",
			ALC5623_HP_OUT_VOL, 15, 7, 1, 1),
};

static const struct snd_kcontrol_new alc5622_vol_snd_controls[] = {
	SOC_DOUBLE_TLV("Speaker Playback Volume",
			ALC5623_SPK_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Speaker Playback Switch",
			ALC5623_SPK_OUT_VOL, 15, 7, 1, 1),
	SOC_DOUBLE_TLV("Line Playback Volume",
			ALC5623_HP_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Line Playback Switch",
			ALC5623_HP_OUT_VOL, 15, 7, 1, 1),
};

static const struct snd_kcontrol_new alc5623_vol_snd_controls[] = {
	SOC_DOUBLE_TLV("Line Playback Volume",
			ALC5623_SPK_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Line Playback Switch",
			ALC5623_SPK_OUT_VOL, 15, 7, 1, 1),
	SOC_DOUBLE_TLV("Headphone Playback Volume",
			ALC5623_HP_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Headphone Playback Switch",
			ALC5623_HP_OUT_VOL, 15, 7, 1, 1),
};

static const struct snd_kcontrol_new alc5623_snd_controls[] = {
	SOC_DOUBLE_TLV("Auxout Playback Volume",
			ALC5623_MONO_AUX_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Auxout Playback Switch",
			ALC5623_MONO_AUX_OUT_VOL, 15, 7, 1, 1),
	SOC_DOUBLE_TLV("PCM Playback Volume",
			ALC5623_STEREO_DAC_VOL, 8, 0, 31, 1, vol_tlv),
	SOC_DOUBLE_TLV("AuxI Capture Volume",
			ALC5623_AUXIN_VOL, 8, 0, 31, 1, vol_tlv),
	SOC_DOUBLE_TLV("LineIn Capture Volume",
			ALC5623_LINE_IN_VOL, 8, 0, 31, 1, vol_tlv),
	SOC_SINGLE_TLV("Mic1 Capture Volume",
			ALC5623_MIC_VOL, 8, 31, 1, vol_tlv),
	SOC_SINGLE_TLV("Mic2 Capture Volume",
			ALC5623_MIC_VOL, 0, 31, 1, vol_tlv),
	SOC_DOUBLE_TLV("Rec Capture Volume",
			ALC5623_ADC_REC_GAIN, 7, 0, 31, 0, adc_rec_tlv),
	SOC_SINGLE_TLV("Mic 1 Boost Volume",
			ALC5623_MIC_CTRL, 10, 2, 0, boost_tlv),
	SOC_SINGLE_TLV("Mic 2 Boost Volume",
			ALC5623_MIC_CTRL, 8, 2, 0, boost_tlv),
	SOC_SINGLE_TLV("Digital Boost Volume",
			ALC5623_ADD_CTRL_REG, 4, 3, 0, dig_tlv),
};

/*
 * DAPM Controls
 */
static const struct snd_kcontrol_new alc5623_hp_mixer_controls[] = {
SOC_DAPM_SINGLE("LI2HP Playback Switch", ALC5623_LINE_IN_VOL, 15, 1, 1),
SOC_DAPM_SINGLE("AUXI2HP Playback Switch", ALC5623_AUXIN_VOL, 15, 1, 1),
SOC_DAPM_SINGLE("MIC12HP Playback Switch", ALC5623_MIC_ROUTING_CTRL, 15, 1, 1),
SOC_DAPM_SINGLE("MIC22HP Playback Switch", ALC5623_MIC_ROUTING_CTRL, 7, 1, 1),
SOC_DAPM_SINGLE("DAC2HP Playback Switch", ALC5623_STEREO_DAC_VOL, 15, 1, 1),
};

static const struct snd_kcontrol_new alc5623_hpl_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC2HP_L Playback Switch", ALC5623_ADC_REC_GAIN, 15, 1, 1),
};

static const struct snd_kcontrol_new alc5623_hpr_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC2HP_R Playback Switch", ALC5623_ADC_REC_GAIN, 14, 1, 1),
};

static const struct snd_kcontrol_new alc5623_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC2MONO_L Playback Switch", ALC5623_ADC_REC_GAIN, 13, 1, 1),
SOC_DAPM_SINGLE("ADC2MONO_R Playback Switch", ALC5623_ADC_REC_GAIN, 12, 1, 1),
SOC_DAPM_SINGLE("LI2MONO Playback Switch", ALC5623_LINE_IN_VOL, 13, 1, 1),
SOC_DAPM_SINGLE("AUXI2MONO Playback Switch", ALC5623_AUXIN_VOL, 13, 1, 1),
SOC_DAPM_SINGLE("MIC12MONO Playback Switch", ALC5623_MIC_ROUTING_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("MIC22MONO Playback Switch", ALC5623_MIC_ROUTING_CTRL, 5, 1, 1),
SOC_DAPM_SINGLE("DAC2MONO Playback Switch", ALC5623_STEREO_DAC_VOL, 13, 1, 1),
};

static const struct snd_kcontrol_new alc5623_speaker_mixer_controls[] = {
SOC_DAPM_SINGLE("LI2SPK Playback Switch", ALC5623_LINE_IN_VOL, 14, 1, 1),
SOC_DAPM_SINGLE("AUXI2SPK Playback Switch", ALC5623_AUXIN_VOL, 14, 1, 1),
SOC_DAPM_SINGLE("MIC12SPK Playback Switch", ALC5623_MIC_ROUTING_CTRL, 14, 1, 1),
SOC_DAPM_SINGLE("MIC22SPK Playback Switch", ALC5623_MIC_ROUTING_CTRL, 6, 1, 1),
SOC_DAPM_SINGLE("DAC2SPK Playback Switch", ALC5623_STEREO_DAC_VOL, 14, 1, 1),
};

/* Left Record Mixer */
static const struct snd_kcontrol_new alc5623_captureL_mixer_controls[] = {
SOC_DAPM_SINGLE("Mic1 Capture Switch", ALC5623_ADC_REC_MIXER, 14, 1, 1),
SOC_DAPM_SINGLE("Mic2 Capture Switch", ALC5623_ADC_REC_MIXER, 13, 1, 1),
SOC_DAPM_SINGLE("LineInL Capture Switch", ALC5623_ADC_REC_MIXER, 12, 1, 1),
SOC_DAPM_SINGLE("Left AuxI Capture Switch", ALC5623_ADC_REC_MIXER, 11, 1, 1),
SOC_DAPM_SINGLE("HPMixerL Capture Switch", ALC5623_ADC_REC_MIXER, 10, 1, 1),
SOC_DAPM_SINGLE("SPKMixer Capture Switch", ALC5623_ADC_REC_MIXER, 9, 1, 1),
SOC_DAPM_SINGLE("MonoMixer Capture Switch", ALC5623_ADC_REC_MIXER, 8, 1, 1),
};

/* Right Record Mixer */
static const struct snd_kcontrol_new alc5623_captureR_mixer_controls[] = {
SOC_DAPM_SINGLE("Mic1 Capture Switch", ALC5623_ADC_REC_MIXER, 6, 1, 1),
SOC_DAPM_SINGLE("Mic2 Capture Switch", ALC5623_ADC_REC_MIXER, 5, 1, 1),
SOC_DAPM_SINGLE("LineInR Capture Switch", ALC5623_ADC_REC_MIXER, 4, 1, 1),
SOC_DAPM_SINGLE("Right AuxI Capture Switch", ALC5623_ADC_REC_MIXER, 3, 1, 1),
SOC_DAPM_SINGLE("HPMixerR Capture Switch", ALC5623_ADC_REC_MIXER, 2, 1, 1),
SOC_DAPM_SINGLE("SPKMixer Capture Switch", ALC5623_ADC_REC_MIXER, 1, 1, 1),
SOC_DAPM_SINGLE("MonoMixer Capture Switch", ALC5623_ADC_REC_MIXER, 0, 1, 1),
};

static const char *alc5623_spk_n_sour_sel[] = {
		"RN/-R", "RP/+R", "LN/-R", "Vmid" };
static const char *alc5623_hpl_out_input_sel[] = {
		"Vmid", "HP Left Mix"};
static const char *alc5623_hpr_out_input_sel[] = {
		"Vmid", "HP Right Mix"};
static const char *alc5623_spkout_input_sel[] = {
		"Vmid", "HPOut Mix", "Speaker Mix", "Mono Mix"};
static const char *alc5623_aux_out_input_sel[] = {
		"Vmid", "HPOut Mix", "Speaker Mix", "Mono Mix"};

/* auxout output mux */
static SOC_ENUM_SINGLE_DECL(alc5623_aux_out_input_enum,
			    ALC5623_OUTPUT_MIXER_CTRL, 6,
			    alc5623_aux_out_input_sel);
static const struct snd_kcontrol_new alc5623_auxout_mux_controls =
SOC_DAPM_ENUM("Route", alc5623_aux_out_input_enum);

/* speaker output mux */
static SOC_ENUM_SINGLE_DECL(alc5623_spkout_input_enum,
			    ALC5623_OUTPUT_MIXER_CTRL, 10,
			    alc5623_spkout_input_sel);
static const struct snd_kcontrol_new alc5623_spkout_mux_controls =
SOC_DAPM_ENUM("Route", alc5623_spkout_input_enum);

/* headphone left output mux */
static SOC_ENUM_SINGLE_DECL(alc5623_hpl_out_input_enum,
			    ALC5623_OUTPUT_MIXER_CTRL, 9,
			    alc5623_hpl_out_input_sel);
static const struct snd_kcontrol_new alc5623_hpl_out_mux_controls =
SOC_DAPM_ENUM("Route", alc5623_hpl_out_input_enum);

/* headphone right output mux */
static SOC_ENUM_SINGLE_DECL(alc5623_hpr_out_input_enum,
			    ALC5623_OUTPUT_MIXER_CTRL, 8,
			    alc5623_hpr_out_input_sel);
static const struct snd_kcontrol_new alc5623_hpr_out_mux_controls =
SOC_DAPM_ENUM("Route", alc5623_hpr_out_input_enum);

/* speaker output N select */
static SOC_ENUM_SINGLE_DECL(alc5623_spk_n_sour_enum,
			    ALC5623_OUTPUT_MIXER_CTRL, 14,
			    alc5623_spk_n_sour_sel);
static const struct snd_kcontrol_new alc5623_spkoutn_mux_controls =
SOC_DAPM_ENUM("Route", alc5623_spk_n_sour_enum);

static const struct snd_soc_dapm_widget alc5623_dapm_widgets[] = {
/* Muxes */
SND_SOC_DAPM_MUX("AuxOut Mux", SND_SOC_NOPM, 0, 0,
	&alc5623_auxout_mux_controls),
SND_SOC_DAPM_MUX("SpeakerOut Mux", SND_SOC_NOPM, 0, 0,
	&alc5623_spkout_mux_controls),
SND_SOC_DAPM_MUX("Left Headphone Mux", SND_SOC_NOPM, 0, 0,
	&alc5623_hpl_out_mux_controls),
SND_SOC_DAPM_MUX("Right Headphone Mux", SND_SOC_NOPM, 0, 0,
	&alc5623_hpr_out_mux_controls),
SND_SOC_DAPM_MUX("SpeakerOut N Mux", SND_SOC_NOPM, 0, 0,
	&alc5623_spkoutn_mux_controls),

/* output mixers */
SND_SOC_DAPM_MIXER("HP Mix", SND_SOC_NOPM, 0, 0,
	&alc5623_hp_mixer_controls[0],
	ARRAY_SIZE(alc5623_hp_mixer_controls)),
SND_SOC_DAPM_MIXER("HPR Mix", ALC5623_PWR_MANAG_ADD2, 4, 0,
	&alc5623_hpr_mixer_controls[0],
	ARRAY_SIZE(alc5623_hpr_mixer_controls)),
SND_SOC_DAPM_MIXER("HPL Mix", ALC5623_PWR_MANAG_ADD2, 5, 0,
	&alc5623_hpl_mixer_controls[0],
	ARRAY_SIZE(alc5623_hpl_mixer_controls)),
SND_SOC_DAPM_MIXER("HPOut Mix", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Mono Mix", ALC5623_PWR_MANAG_ADD2, 2, 0,
	&alc5623_mono_mixer_controls[0],
	ARRAY_SIZE(alc5623_mono_mixer_controls)),
SND_SOC_DAPM_MIXER("Speaker Mix", ALC5623_PWR_MANAG_ADD2, 3, 0,
	&alc5623_speaker_mixer_controls[0],
	ARRAY_SIZE(alc5623_speaker_mixer_controls)),

/* input mixers */
SND_SOC_DAPM_MIXER("Left Capture Mix", ALC5623_PWR_MANAG_ADD2, 1, 0,
	&alc5623_captureL_mixer_controls[0],
	ARRAY_SIZE(alc5623_captureL_mixer_controls)),
SND_SOC_DAPM_MIXER("Right Capture Mix", ALC5623_PWR_MANAG_ADD2, 0, 0,
	&alc5623_captureR_mixer_controls[0],
	ARRAY_SIZE(alc5623_captureR_mixer_controls)),

SND_SOC_DAPM_DAC("Left DAC", "Left HiFi Playback",
	ALC5623_PWR_MANAG_ADD2, 9, 0),
SND_SOC_DAPM_DAC("Right DAC", "Right HiFi Playback",
	ALC5623_PWR_MANAG_ADD2, 8, 0),
SND_SOC_DAPM_MIXER("I2S Mix", ALC5623_PWR_MANAG_ADD1, 15, 0, NULL, 0),
SND_SOC_DAPM_MIXER("AuxI Mix", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Line Mix", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_ADC("Left ADC", "Left HiFi Capture",
	ALC5623_PWR_MANAG_ADD2, 7, 0),
SND_SOC_DAPM_ADC("Right ADC", "Right HiFi Capture",
	ALC5623_PWR_MANAG_ADD2, 6, 0),
SND_SOC_DAPM_PGA("Left Headphone", ALC5623_PWR_MANAG_ADD3, 10, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Headphone", ALC5623_PWR_MANAG_ADD3, 9, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpeakerOut", ALC5623_PWR_MANAG_ADD3, 12, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left AuxOut", ALC5623_PWR_MANAG_ADD3, 14, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right AuxOut", ALC5623_PWR_MANAG_ADD3, 13, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left LineIn", ALC5623_PWR_MANAG_ADD3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right LineIn", ALC5623_PWR_MANAG_ADD3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left AuxI", ALC5623_PWR_MANAG_ADD3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right AuxI", ALC5623_PWR_MANAG_ADD3, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC1 PGA", ALC5623_PWR_MANAG_ADD3, 3, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC2 PGA", ALC5623_PWR_MANAG_ADD3, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC1 Pre Amp", ALC5623_PWR_MANAG_ADD3, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC2 Pre Amp", ALC5623_PWR_MANAG_ADD3, 0, 0, NULL, 0),
SND_SOC_DAPM_MICBIAS("Mic Bias1", ALC5623_PWR_MANAG_ADD1, 11, 0),

SND_SOC_DAPM_OUTPUT("AUXOUTL"),
SND_SOC_DAPM_OUTPUT("AUXOUTR"),
SND_SOC_DAPM_OUTPUT("HPL"),
SND_SOC_DAPM_OUTPUT("HPR"),
SND_SOC_DAPM_OUTPUT("SPKOUT"),
SND_SOC_DAPM_OUTPUT("SPKOUTN"),
SND_SOC_DAPM_INPUT("LINEINL"),
SND_SOC_DAPM_INPUT("LINEINR"),
SND_SOC_DAPM_INPUT("AUXINL"),
SND_SOC_DAPM_INPUT("AUXINR"),
SND_SOC_DAPM_INPUT("MIC1"),
SND_SOC_DAPM_INPUT("MIC2"),
SND_SOC_DAPM_VMID("Vmid"),
};

static const char *alc5623_amp_names[] = {"AB Amp", "D Amp"};
static SOC_ENUM_SINGLE_DECL(alc5623_amp_enum,
			    ALC5623_OUTPUT_MIXER_CTRL, 13,
			    alc5623_amp_names);
static const struct snd_kcontrol_new alc5623_amp_mux_controls =
	SOC_DAPM_ENUM("Route", alc5623_amp_enum);

static const struct snd_soc_dapm_widget alc5623_dapm_amp_widgets[] = {
SND_SOC_DAPM_PGA_E("D Amp", ALC5623_PWR_MANAG_ADD2, 14, 0, NULL, 0,
	amp_mixer_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_PGA("AB Amp", ALC5623_PWR_MANAG_ADD2, 15, 0, NULL, 0),
SND_SOC_DAPM_MUX("AB-D Amp Mux", SND_SOC_NOPM, 0, 0,
	&alc5623_amp_mux_controls),
};

static const struct snd_soc_dapm_route intercon[] = {
	/* virtual mixer - mixes left & right channels */
	{"I2S Mix", NULL,				"Left DAC"},
	{"I2S Mix", NULL,				"Right DAC"},
	{"Line Mix", NULL,				"Right LineIn"},
	{"Line Mix", NULL,				"Left LineIn"},
	{"AuxI Mix", NULL,				"Left AuxI"},
	{"AuxI Mix", NULL,				"Right AuxI"},
	{"AUXOUTL", NULL,				"Left AuxOut"},
	{"AUXOUTR", NULL,				"Right AuxOut"},

	/* HP mixer */
	{"HPL Mix", "ADC2HP_L Playback Switch",		"Left Capture Mix"},
	{"HPL Mix", NULL,				"HP Mix"},
	{"HPR Mix", "ADC2HP_R Playback Switch",		"Right Capture Mix"},
	{"HPR Mix", NULL,				"HP Mix"},
	{"HP Mix", "LI2HP Playback Switch",		"Line Mix"},
	{"HP Mix", "AUXI2HP Playback Switch",		"AuxI Mix"},
	{"HP Mix", "MIC12HP Playback Switch",		"MIC1 PGA"},
	{"HP Mix", "MIC22HP Playback Switch",		"MIC2 PGA"},
	{"HP Mix", "DAC2HP Playback Switch",		"I2S Mix"},

	/* speaker mixer */
	{"Speaker Mix", "LI2SPK Playback Switch",	"Line Mix"},
	{"Speaker Mix", "AUXI2SPK Playback Switch",	"AuxI Mix"},
	{"Speaker Mix", "MIC12SPK Playback Switch",	"MIC1 PGA"},
	{"Speaker Mix", "MIC22SPK Playback Switch",	"MIC2 PGA"},
	{"Speaker Mix", "DAC2SPK Playback Switch",	"I2S Mix"},

	/* mono mixer */
	{"Mono Mix", "ADC2MONO_L Playback Switch",	"Left Capture Mix"},
	{"Mono Mix", "ADC2MONO_R Playback Switch",	"Right Capture Mix"},
	{"Mono Mix", "LI2MONO Playback Switch",		"Line Mix"},
	{"Mono Mix", "AUXI2MONO Playback Switch",	"AuxI Mix"},
	{"Mono Mix", "MIC12MONO Playback Switch",	"MIC1 PGA"},
	{"Mono Mix", "MIC22MONO Playback Switch",	"MIC2 PGA"},
	{"Mono Mix", "DAC2MONO Playback Switch",	"I2S Mix"},

	/* Left record mixer */
	{"Left Capture Mix", "LineInL Capture Switch",	"LINEINL"},
	{"Left Capture Mix", "Left AuxI Capture Switch", "AUXINL"},
	{"Left Capture Mix", "Mic1 Capture Switch",	"MIC1 Pre Amp"},
	{"Left Capture Mix", "Mic2 Capture Switch",	"MIC2 Pre Amp"},
	{"Left Capture Mix", "HPMixerL Capture Switch", "HPL Mix"},
	{"Left Capture Mix", "SPKMixer Capture Switch", "Speaker Mix"},
	{"Left Capture Mix", "MonoMixer Capture Switch", "Mono Mix"},

	/*Right record mixer */
	{"Right Capture Mix", "LineInR Capture Switch",	"LINEINR"},
	{"Right Capture Mix", "Right AuxI Capture Switch",	"AUXINR"},
	{"Right Capture Mix", "Mic1 Capture Switch",	"MIC1 Pre Amp"},
	{"Right Capture Mix", "Mic2 Capture Switch",	"MIC2 Pre Amp"},
	{"Right Capture Mix", "HPMixerR Capture Switch", "HPR Mix"},
	{"Right Capture Mix", "SPKMixer Capture Switch", "Speaker Mix"},
	{"Right Capture Mix", "MonoMixer Capture Switch", "Mono Mix"},

	/* headphone left mux */
	{"Left Headphone Mux", "HP Left Mix",		"HPL Mix"},
	{"Left Headphone Mux", "Vmid",			"Vmid"},

	/* headphone right mux */
	{"Right Headphone Mux", "HP Right Mix",		"HPR Mix"},
	{"Right Headphone Mux", "Vmid",			"Vmid"},

	/* speaker out mux */
	{"SpeakerOut Mux", "Vmid",			"Vmid"},
	{"SpeakerOut Mux", "HPOut Mix",			"HPOut Mix"},
	{"SpeakerOut Mux", "Speaker Mix",		"Speaker Mix"},
	{"SpeakerOut Mux", "Mono Mix",			"Mono Mix"},

	/* Mono/Aux Out mux */
	{"AuxOut Mux", "Vmid",				"Vmid"},
	{"AuxOut Mux", "HPOut Mix",			"HPOut Mix"},
	{"AuxOut Mux", "Speaker Mix",			"Speaker Mix"},
	{"AuxOut Mux", "Mono Mix",			"Mono Mix"},

	/* output pga */
	{"HPL", NULL,					"Left Headphone"},
	{"Left Headphone", NULL,			"Left Headphone Mux"},
	{"HPR", NULL,					"Right Headphone"},
	{"Right Headphone", NULL,			"Right Headphone Mux"},
	{"Left AuxOut", NULL,				"AuxOut Mux"},
	{"Right AuxOut", NULL,				"AuxOut Mux"},

	/* input pga */
	{"Left LineIn", NULL,				"LINEINL"},
	{"Right LineIn", NULL,				"LINEINR"},
	{"Left AuxI", NULL,				"AUXINL"},
	{"Right AuxI", NULL,				"AUXINR"},
	{"MIC1 Pre Amp", NULL,				"MIC1"},
	{"MIC2 Pre Amp", NULL,				"MIC2"},
	{"MIC1 PGA", NULL,				"MIC1 Pre Amp"},
	{"MIC2 PGA", NULL,				"MIC2 Pre Amp"},

	/* left ADC */
	{"Left ADC", NULL,				"Left Capture Mix"},

	/* right ADC */
	{"Right ADC", NULL,				"Right Capture Mix"},

	{"SpeakerOut N Mux", "RN/-R",			"SpeakerOut"},
	{"SpeakerOut N Mux", "RP/+R",			"SpeakerOut"},
	{"SpeakerOut N Mux", "LN/-R",			"SpeakerOut"},
	{"SpeakerOut N Mux", "Vmid",			"Vmid"},

	{"SPKOUT", NULL,				"SpeakerOut"},
	{"SPKOUTN", NULL,				"SpeakerOut N Mux"},
};

static const struct snd_soc_dapm_route intercon_spk[] = {
	{"SpeakerOut", NULL,				"SpeakerOut Mux"},
};

static const struct snd_soc_dapm_route intercon_amp_spk[] = {
	{"AB Amp", NULL,				"SpeakerOut Mux"},
	{"D Amp", NULL,					"SpeakerOut Mux"},
	{"AB-D Amp Mux", "AB Amp",			"AB Amp"},
	{"AB-D Amp Mux", "D Amp",			"D Amp"},
	{"SpeakerOut", NULL,				"AB-D Amp Mux"},
};

/* PLL divisors */
struct _pll_div {
	u32 pll_in;
	u32 pll_out;
	u16 regvalue;
};

/* Note : pll code from original alc5623 driver. Not sure of how good it is */
/* useful only for master mode */
static const struct _pll_div codec_master_pll_div[] = {

	{  2048000,  8192000,	0x0ea0},
	{  3686400,  8192000,	0x4e27},
	{ 12000000,  8192000,	0x456b},
	{ 13000000,  8192000,	0x495f},
	{ 13100000,  8192000,	0x0320},
	{  2048000,  11289600,	0xf637},
	{  3686400,  11289600,	0x2f22},
	{ 12000000,  11289600,	0x3e2f},
	{ 13000000,  11289600,	0x4d5b},
	{ 13100000,  11289600,	0x363b},
	{  2048000,  16384000,	0x1ea0},
	{  3686400,  16384000,	0x9e27},
	{ 12000000,  16384000,	0x452b},
	{ 13000000,  16384000,	0x542f},
	{ 13100000,  16384000,	0x03a0},
	{  2048000,  16934400,	0xe625},
	{  3686400,  16934400,	0x9126},
	{ 12000000,  16934400,	0x4d2c},
	{ 13000000,  16934400,	0x742f},
	{ 13100000,  16934400,	0x3c27},
	{  2048000,  22579200,	0x2aa0},
	{  3686400,  22579200,	0x2f20},
	{ 12000000,  22579200,	0x7e2f},
	{ 13000000,  22579200,	0x742f},
	{ 13100000,  22579200,	0x3c27},
	{  2048000,  24576000,	0x2ea0},
	{  3686400,  24576000,	0xee27},
	{ 12000000,  24576000,	0x2915},
	{ 13000000,  24576000,	0x772e},
	{ 13100000,  24576000,	0x0d20},
};

static const struct _pll_div codec_slave_pll_div[] = {

	{  1024000,  16384000,  0x3ea0},
	{  1411200,  22579200,	0x3ea0},
	{  1536000,  24576000,	0x3ea0},
	{  2048000,  16384000,  0x1ea0},
	{  2822400,  22579200,	0x1ea0},
	{  3072000,  24576000,	0x1ea0},

};

static int alc5623_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	int i;
	struct snd_soc_codec *codec = codec_dai->codec;
	int gbl_clk = 0, pll_div = 0;
	u16 reg;

	if (pll_id < ALC5623_PLL_FR_MCLK || pll_id > ALC5623_PLL_FR_BCK)
		return -ENODEV;

	/* Disable PLL power */
	snd_soc_update_bits(codec, ALC5623_PWR_MANAG_ADD2,
				ALC5623_PWR_ADD2_PLL,
				0);

	/* pll is not used in slave mode */
	reg = snd_soc_read(codec, ALC5623_DAI_CONTROL);
	if (reg & ALC5623_DAI_SDP_SLAVE_MODE)
		return 0;

	if (!freq_in || !freq_out)
		return 0;

	switch (pll_id) {
	case ALC5623_PLL_FR_MCLK:
		for (i = 0; i < ARRAY_SIZE(codec_master_pll_div); i++) {
			if (codec_master_pll_div[i].pll_in == freq_in
			   && codec_master_pll_div[i].pll_out == freq_out) {
				/* PLL source from MCLK */
				pll_div  = codec_master_pll_div[i].regvalue;
				break;
			}
		}
		break;
	case ALC5623_PLL_FR_BCK:
		for (i = 0; i < ARRAY_SIZE(codec_slave_pll_div); i++) {
			if (codec_slave_pll_div[i].pll_in == freq_in
			   && codec_slave_pll_div[i].pll_out == freq_out) {
				/* PLL source from Bitclk */
				gbl_clk = ALC5623_GBL_CLK_PLL_SOUR_SEL_BITCLK;
				pll_div = codec_slave_pll_div[i].regvalue;
				break;
			}
		}
		break;
	default:
		return -EINVAL;
	}

	if (!pll_div)
		return -EINVAL;

	snd_soc_write(codec, ALC5623_GLOBAL_CLK_CTRL_REG, gbl_clk);
	snd_soc_write(codec, ALC5623_PLL_CTRL, pll_div);
	snd_soc_update_bits(codec, ALC5623_PWR_MANAG_ADD2,
				ALC5623_PWR_ADD2_PLL,
				ALC5623_PWR_ADD2_PLL);
	gbl_clk |= ALC5623_GBL_CLK_SYS_SOUR_SEL_PLL;
	snd_soc_write(codec, ALC5623_GLOBAL_CLK_CTRL_REG, gbl_clk);

	return 0;
}

struct _coeff_div {
	u16 fs;
	u16 regvalue;
};

/* codec hifi mclk (after PLL) clock divider coefficients */
/* values inspired from column BCLK=32Fs of Appendix A table */
static const struct _coeff_div coeff_div[] = {
	{256*8, 0x3a69},
	{384*8, 0x3c6b},
	{256*4, 0x2a69},
	{384*4, 0x2c6b},
	{256*2, 0x1a69},
	{384*2, 0x1c6b},
	{256*1, 0x0a69},
	{384*1, 0x0c6b},
};

static int get_coeff(struct snd_soc_codec *codec, int rate)
{
	struct alc5623_priv *alc5623 = snd_soc_codec_get_drvdata(codec);
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].fs * rate == alc5623->sysclk)
			return i;
	}
	return -EINVAL;
}

/*
 * Clock after PLL and dividers
 */
static int alc5623_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct alc5623_priv *alc5623 = snd_soc_codec_get_drvdata(codec);

	switch (freq) {
	case  8192000:
	case 11289600:
	case 12288000:
	case 16384000:
	case 16934400:
	case 18432000:
	case 22579200:
	case 24576000:
		alc5623->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int alc5623_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = ALC5623_DAI_SDP_MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface = ALC5623_DAI_SDP_SLAVE_MODE;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= ALC5623_DAI_I2S_DF_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface |= ALC5623_DAI_I2S_DF_RIGHT;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= ALC5623_DAI_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= ALC5623_DAI_I2S_DF_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= ALC5623_DAI_I2S_DF_PCM | ALC5623_DAI_I2S_PCM_MODE;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= ALC5623_DAI_MAIN_I2S_BCLK_POL_CTRL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= ALC5623_DAI_MAIN_I2S_BCLK_POL_CTRL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_write(codec, ALC5623_DAI_CONTROL, iface);
}

static int alc5623_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct alc5623_priv *alc5623 = snd_soc_codec_get_drvdata(codec);
	int coeff, rate;
	u16 iface;

	iface = snd_soc_read(codec, ALC5623_DAI_CONTROL);
	iface &= ~ALC5623_DAI_I2S_DL_MASK;

	/* bit size */
	switch (params_width(params)) {
	case 16:
		iface |= ALC5623_DAI_I2S_DL_16;
		break;
	case 20:
		iface |= ALC5623_DAI_I2S_DL_20;
		break;
	case 24:
		iface |= ALC5623_DAI_I2S_DL_24;
		break;
	case 32:
		iface |= ALC5623_DAI_I2S_DL_32;
		break;
	default:
		return -EINVAL;
	}

	/* set iface & srate */
	snd_soc_write(codec, ALC5623_DAI_CONTROL, iface);
	rate = params_rate(params);
	coeff = get_coeff(codec, rate);
	if (coeff < 0)
		return -EINVAL;

	coeff = coeff_div[coeff].regvalue;
	dev_dbg(codec->dev, "%s: sysclk=%d,rate=%d,coeff=0x%04x\n",
		__func__, alc5623->sysclk, rate, coeff);
	snd_soc_write(codec, ALC5623_STEREO_AD_DA_CLK_CTRL, coeff);

	return 0;
}

static int alc5623_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 hp_mute = ALC5623_MISC_M_DAC_L_INPUT | ALC5623_MISC_M_DAC_R_INPUT;
	u16 mute_reg = snd_soc_read(codec, ALC5623_MISC_CTRL) & ~hp_mute;

	if (mute)
		mute_reg |= hp_mute;

	return snd_soc_write(codec, ALC5623_MISC_CTRL, mute_reg);
}

#define ALC5623_ADD2_POWER_EN (ALC5623_PWR_ADD2_VREF \
	| ALC5623_PWR_ADD2_DAC_REF_CIR)

#define ALC5623_ADD3_POWER_EN (ALC5623_PWR_ADD3_MAIN_BIAS \
	| ALC5623_PWR_ADD3_MIC1_BOOST_AD)

#define ALC5623_ADD1_POWER_EN \
	(ALC5623_PWR_ADD1_SHORT_CURR_DET_EN | ALC5623_PWR_ADD1_SOFTGEN_EN \
	| ALC5623_PWR_ADD1_DEPOP_BUF_HP | ALC5623_PWR_ADD1_HP_OUT_AMP \
	| ALC5623_PWR_ADD1_HP_OUT_ENH_AMP)

#define ALC5623_ADD1_POWER_EN_5622 \
	(ALC5623_PWR_ADD1_SHORT_CURR_DET_EN \
	| ALC5623_PWR_ADD1_HP_OUT_AMP)

static void enable_power_depop(struct snd_soc_codec *codec)
{
	struct alc5623_priv *alc5623 = snd_soc_codec_get_drvdata(codec);

	snd_soc_update_bits(codec, ALC5623_PWR_MANAG_ADD1,
				ALC5623_PWR_ADD1_SOFTGEN_EN,
				ALC5623_PWR_ADD1_SOFTGEN_EN);

	snd_soc_write(codec, ALC5623_PWR_MANAG_ADD3, ALC5623_ADD3_POWER_EN);

	snd_soc_update_bits(codec, ALC5623_MISC_CTRL,
				ALC5623_MISC_HP_DEPOP_MODE2_EN,
				ALC5623_MISC_HP_DEPOP_MODE2_EN);

	msleep(500);

	snd_soc_write(codec, ALC5623_PWR_MANAG_ADD2, ALC5623_ADD2_POWER_EN);

	/* avoid writing '1' into 5622 reserved bits */
	if (alc5623->id == 0x22)
		snd_soc_write(codec, ALC5623_PWR_MANAG_ADD1,
			ALC5623_ADD1_POWER_EN_5622);
	else
		snd_soc_write(codec, ALC5623_PWR_MANAG_ADD1,
			ALC5623_ADD1_POWER_EN);

	/* disable HP Depop2 */
	snd_soc_update_bits(codec, ALC5623_MISC_CTRL,
				ALC5623_MISC_HP_DEPOP_MODE2_EN,
				0);

}

static int alc5623_set_bias_level(struct snd_soc_codec *codec,
				      enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		enable_power_depop(codec);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* everything off except vref/vmid, */
		snd_soc_write(codec, ALC5623_PWR_MANAG_ADD2,
				ALC5623_PWR_ADD2_VREF);
		snd_soc_write(codec, ALC5623_PWR_MANAG_ADD3,
				ALC5623_PWR_ADD3_MAIN_BIAS);
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off, dac mute, inactive */
		snd_soc_write(codec, ALC5623_PWR_MANAG_ADD2, 0);
		snd_soc_write(codec, ALC5623_PWR_MANAG_ADD3, 0);
		snd_soc_write(codec, ALC5623_PWR_MANAG_ADD1, 0);
		break;
	}
	return 0;
}

#define ALC5623_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE \
			| SNDRV_PCM_FMTBIT_S24_LE \
			| SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops alc5623_dai_ops = {
		.hw_params = alc5623_pcm_hw_params,
		.digital_mute = alc5623_mute,
		.set_fmt = alc5623_set_dai_fmt,
		.set_sysclk = alc5623_set_dai_sysclk,
		.set_pll = alc5623_set_dai_pll,
};

static struct snd_soc_dai_driver alc5623_dai = {
	.name = "alc5623-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rate_min =	8000,
		.rate_max =	48000,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = ALC5623_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rate_min =	8000,
		.rate_max =	48000,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = ALC5623_FORMATS,},

	.ops = &alc5623_dai_ops,
};

static int alc5623_suspend(struct snd_soc_codec *codec)
{
	struct alc5623_priv *alc5623 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(alc5623->regmap, true);

	return 0;
}

static int alc5623_resume(struct snd_soc_codec *codec)
{
	struct alc5623_priv *alc5623 = snd_soc_codec_get_drvdata(codec);
	int ret;

	/* Sync reg_cache with the hardware */
	regcache_cache_only(alc5623->regmap, false);
	ret = regcache_sync(alc5623->regmap);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to sync register cache: %d\n",
			ret);
		regcache_cache_only(alc5623->regmap, true);
		return ret;
	}

	return 0;
}

static int alc5623_probe(struct snd_soc_codec *codec)
{
	struct alc5623_priv *alc5623 = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);

	alc5623_reset(codec);

	if (alc5623->add_ctrl) {
		snd_soc_write(codec, ALC5623_ADD_CTRL_REG,
				alc5623->add_ctrl);
	}

	if (alc5623->jack_det_ctrl) {
		snd_soc_write(codec, ALC5623_JACK_DET_CTRL,
				alc5623->jack_det_ctrl);
	}

	switch (alc5623->id) {
	case 0x21:
		snd_soc_add_codec_controls(codec, alc5621_vol_snd_controls,
			ARRAY_SIZE(alc5621_vol_snd_controls));
		break;
	case 0x22:
		snd_soc_add_codec_controls(codec, alc5622_vol_snd_controls,
			ARRAY_SIZE(alc5622_vol_snd_controls));
		break;
	case 0x23:
		snd_soc_add_codec_controls(codec, alc5623_vol_snd_controls,
			ARRAY_SIZE(alc5623_vol_snd_controls));
		break;
	default:
		return -EINVAL;
	}

	snd_soc_add_codec_controls(codec, alc5623_snd_controls,
			ARRAY_SIZE(alc5623_snd_controls));

	snd_soc_dapm_new_controls(dapm, alc5623_dapm_widgets,
					ARRAY_SIZE(alc5623_dapm_widgets));

	/* set up audio path interconnects */
	snd_soc_dapm_add_routes(dapm, intercon, ARRAY_SIZE(intercon));

	switch (alc5623->id) {
	case 0x21:
	case 0x22:
		snd_soc_dapm_new_controls(dapm, alc5623_dapm_amp_widgets,
					ARRAY_SIZE(alc5623_dapm_amp_widgets));
		snd_soc_dapm_add_routes(dapm, intercon_amp_spk,
					ARRAY_SIZE(intercon_amp_spk));
		break;
	case 0x23:
		snd_soc_dapm_add_routes(dapm, intercon_spk,
					ARRAY_SIZE(intercon_spk));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_codec_driver soc_codec_device_alc5623 = {
	.probe = alc5623_probe,
	.suspend = alc5623_suspend,
	.resume = alc5623_resume,
	.set_bias_level = alc5623_set_bias_level,
	.suspend_bias_off = true,
};

static const struct regmap_config alc5623_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.reg_stride = 2,

	.max_register = ALC5623_VENDOR_ID2,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * ALC5623 2 wire address is determined by A1 pin
 * state during powerup.
 *    low  = 0x1a
 *    high = 0x1b
 */
static int alc5623_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct alc5623_platform_data *pdata;
	struct alc5623_priv *alc5623;
	struct device_node *np;
	unsigned int vid1, vid2;
	int ret;
	u32 val32;

	alc5623 = devm_kzalloc(&client->dev, sizeof(struct alc5623_priv),
			       GFP_KERNEL);
	if (alc5623 == NULL)
		return -ENOMEM;

	alc5623->regmap = devm_regmap_init_i2c(client, &alc5623_regmap);
	if (IS_ERR(alc5623->regmap)) {
		ret = PTR_ERR(alc5623->regmap);
		dev_err(&client->dev, "Failed to initialise I/O: %d\n", ret);
		return ret;
	}

	ret = regmap_read(alc5623->regmap, ALC5623_VENDOR_ID1, &vid1);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read vendor ID1: %d\n", ret);
		return ret;
	}

	ret = regmap_read(alc5623->regmap, ALC5623_VENDOR_ID2, &vid2);
	if (ret < 0) {
		dev_err(&client->dev, "failed to read vendor ID2: %d\n", ret);
		return ret;
	}
	vid2 >>= 8;

	if ((vid1 != 0x10ec) || (vid2 != id->driver_data)) {
		dev_err(&client->dev, "unknown or wrong codec\n");
		dev_err(&client->dev, "Expected %x:%lx, got %x:%x\n",
				0x10ec, id->driver_data,
				vid1, vid2);
		return -ENODEV;
	}

	dev_dbg(&client->dev, "Found codec id : alc56%02x\n", vid2);

	pdata = client->dev.platform_data;
	if (pdata) {
		alc5623->add_ctrl = pdata->add_ctrl;
		alc5623->jack_det_ctrl = pdata->jack_det_ctrl;
	} else {
		if (client->dev.of_node) {
			np = client->dev.of_node;
			ret = of_property_read_u32(np, "add-ctrl", &val32);
			if (!ret)
				alc5623->add_ctrl = val32;
			ret = of_property_read_u32(np, "jack-det-ctrl", &val32);
			if (!ret)
				alc5623->jack_det_ctrl = val32;
		}
	}

	alc5623->id = vid2;
	switch (alc5623->id) {
	case 0x21:
		alc5623_dai.name = "alc5621-hifi";
		break;
	case 0x22:
		alc5623_dai.name = "alc5622-hifi";
		break;
	case 0x23:
		alc5623_dai.name = "alc5623-hifi";
		break;
	default:
		return -EINVAL;
	}

	i2c_set_clientdata(client, alc5623);

	ret =  snd_soc_register_codec(&client->dev,
		&soc_codec_device_alc5623, &alc5623_dai, 1);
	if (ret != 0)
		dev_err(&client->dev, "Failed to register codec: %d\n", ret);

	return ret;
}

static int alc5623_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id alc5623_i2c_table[] = {
	{"alc5621", 0x21},
	{"alc5622", 0x22},
	{"alc5623", 0x23},
	{}
};
MODULE_DEVICE_TABLE(i2c, alc5623_i2c_table);

static const struct of_device_id alc5623_of_match[] = {
	{ .compatible = "realtek,alc5623", },
	{ }
};
MODULE_DEVICE_TABLE(of, alc5623_of_match);

/*  i2c codec control layer */
static struct i2c_driver alc5623_i2c_driver = {
	.driver = {
		.name = "alc562x-codec",
		.of_match_table = of_match_ptr(alc5623_of_match),
	},
	.probe = alc5623_i2c_probe,
	.remove =  alc5623_i2c_remove,
	.id_table = alc5623_i2c_table,
};

module_i2c_driver(alc5623_i2c_driver);

MODULE_DESCRIPTION("ASoC alc5621/2/3 driver");
MODULE_AUTHOR("Arnaud Patard <arnaud.patard@rtp-net.org>");
MODULE_LICENSE("GPL");
