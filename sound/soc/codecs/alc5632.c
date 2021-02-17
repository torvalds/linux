// SPDX-License-Identifier: GPL-2.0-only
/*
* alc5632.c  --  ALC5632 ALSA SoC Audio Codec
*
* Copyright (C) 2011 The AC100 Kernel Team <ac100@lists.lauchpad.net>
*
* Authors:  Leon Romanovsky <leon@leon.nu>
*           Andrey Danin <danindrey@mail.ru>
*           Ilya Petrov <ilya.muromec@gmail.com>
*           Marc Dietrich <marvin24@gmx.de>
*
* Based on alc5623.c by Arnaud Patard
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>
#include <sound/soc.h>
#include <sound/initval.h>

#include "alc5632.h"

/*
 * ALC5632 register cache
 */
static const struct reg_default alc5632_reg_defaults[] = {
	{   2, 0x8080 },	/* R2   - Speaker Output Volume */
	{   4, 0x8080 },	/* R4   - Headphone Output Volume */
	{   6, 0x8080 },	/* R6   - AUXOUT Volume */
	{   8, 0xC800 },	/* R8   - Phone Input */
	{  10, 0xE808 },	/* R10  - LINE_IN Volume */
	{  12, 0x1010 },	/* R12  - STEREO DAC Input Volume */
	{  14, 0x0808 },	/* R14  - MIC Input Volume */
	{  16, 0xEE0F },	/* R16  - Stereo DAC and MIC Routing Control */
	{  18, 0xCBCB },	/* R18  - ADC Record Gain */
	{  20, 0x7F7F },	/* R20  - ADC Record Mixer Control */
	{  24, 0xE010 },	/* R24  - Voice DAC Volume */
	{  28, 0x8008 },	/* R28  - Output Mixer Control */
	{  34, 0x0000 },	/* R34  - Microphone Control */
	{  36, 0x00C0 },    /* R36  - Codec Digital MIC/Digital Boost
						   Control */
	{  46, 0x0000 },	/* R46  - Stereo DAC/Voice DAC/Stereo ADC
						   Function Select */
	{  52, 0x8000 },	/* R52  - Main Serial Data Port Control
						   (Stereo I2S) */
	{  54, 0x0000 },	/* R54  - Extend Serial Data Port Control
						   (VoDAC_I2S/PCM) */
	{  58, 0x0000 },	/* R58  - Power Management Addition 1 */
	{  60, 0x0000 },	/* R60  - Power Management Addition 2 */
	{  62, 0x8000 },	/* R62  - Power Management Addition 3 */
	{  64, 0x0C0A },	/* R64  - General Purpose Control Register 1 */
	{  66, 0x0000 },	/* R66  - General Purpose Control Register 2 */
	{  68, 0x0000 },	/* R68  - PLL1 Control */
	{  70, 0x0000 },	/* R70  - PLL2 Control */
	{  76, 0xBE3E },	/* R76  - GPIO Pin Configuration */
	{  78, 0xBE3E },	/* R78  - GPIO Pin Polarity */
	{  80, 0x0000 },	/* R80  - GPIO Pin Sticky */
	{  82, 0x0000 },	/* R82  - GPIO Pin Wake Up */
	{  86, 0x0000 },	/* R86  - Pin Sharing */
	{  90, 0x0009 },	/* R90  - Soft Volume Control Setting */
	{  92, 0x0000 },	/* R92  - GPIO_Output Pin Control */
	{  94, 0x3000 },	/* R94  - MISC Control */
	{  96, 0x3075 },	/* R96  - Stereo DAC Clock Control_1 */
	{  98, 0x1010 },	/* R98  - Stereo DAC Clock Control_2 */
	{ 100, 0x3110 },	/* R100 - VoDAC_PCM Clock Control_1 */
	{ 104, 0x0553 },	/* R104 - Pseudo Stereo and Spatial Effect
						   Block Control */
	{ 106, 0x0000 },	/* R106 - Private Register Address */
};

/* codec private data */
struct alc5632_priv {
	struct regmap *regmap;
	u8 id;
	unsigned int sysclk;
};

static bool alc5632_volatile_register(struct device *dev,
							unsigned int reg)
{
	switch (reg) {
	case ALC5632_RESET:
	case ALC5632_PWR_DOWN_CTRL_STATUS:
	case ALC5632_GPIO_PIN_STATUS:
	case ALC5632_OVER_CURR_STATUS:
	case ALC5632_HID_CTRL_DATA:
	case ALC5632_EQ_CTRL:
	case ALC5632_VENDOR_ID1:
	case ALC5632_VENDOR_ID2:
		return true;

	default:
		break;
	}

	return false;
}

static inline int alc5632_reset(struct regmap *map)
{
	return regmap_write(map, ALC5632_RESET, 0x59B4);
}

static int amp_mixer_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	/* to power-on/off class-d amp generators/speaker */
	/* need to write to 'index-46h' register :        */
	/* so write index num (here 0x46) to reg 0x6a     */
	/* and then 0xffff/0 to reg 0x6c                  */
	snd_soc_component_write(component, ALC5632_HID_CTRL_INDEX, 0x46);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		snd_soc_component_write(component, ALC5632_HID_CTRL_DATA, 0xFFFF);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_write(component, ALC5632_HID_CTRL_DATA, 0);
		break;
	}

	return 0;
}

/*
 * ALC5632 Controls
 */

/* -34.5db min scale, 1.5db steps, no mute */
static const DECLARE_TLV_DB_SCALE(vol_tlv, -3450, 150, 0);
/* -46.5db min scale, 1.5db steps, no mute */
static const DECLARE_TLV_DB_SCALE(hp_tlv, -4650, 150, 0);
/* -16.5db min scale, 1.5db steps, no mute */
static const DECLARE_TLV_DB_SCALE(adc_rec_tlv, -1650, 150, 0);
static const DECLARE_TLV_DB_RANGE(boost_tlv,
	0, 1, TLV_DB_SCALE_ITEM(0, 2000, 0),
	1, 3, TLV_DB_SCALE_ITEM(2000, 1000, 0)
);
/* 0db min scale, 6 db steps, no mute */
static const DECLARE_TLV_DB_SCALE(dig_tlv, 0, 600, 0);
/* 0db min scalem 0.75db steps, no mute */
static const DECLARE_TLV_DB_SCALE(vdac_tlv, -3525, 75, 0);

static const struct snd_kcontrol_new alc5632_vol_snd_controls[] = {
	/* left starts at bit 8, right at bit 0 */
	/* 31 steps (5 bit), -46.5db scale */
	SOC_DOUBLE_TLV("Speaker Playback Volume",
			ALC5632_SPK_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	/* bit 15 mutes left, bit 7 right */
	SOC_DOUBLE("Speaker Playback Switch",
			ALC5632_SPK_OUT_VOL, 15, 7, 1, 1),
	SOC_DOUBLE_TLV("Headphone Playback Volume",
			ALC5632_HP_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Headphone Playback Switch",
			ALC5632_HP_OUT_VOL, 15, 7, 1, 1),
};

static const struct snd_kcontrol_new alc5632_snd_controls[] = {
	SOC_DOUBLE_TLV("Auxout Playback Volume",
			ALC5632_AUX_OUT_VOL, 8, 0, 31, 1, hp_tlv),
	SOC_DOUBLE("Auxout Playback Switch",
			ALC5632_AUX_OUT_VOL, 15, 7, 1, 1),
	SOC_SINGLE_TLV("Voice DAC Playback Volume",
			ALC5632_VOICE_DAC_VOL, 0, 63, 0, vdac_tlv),
	SOC_SINGLE("Voice DAC Playback Switch",
			ALC5632_VOICE_DAC_VOL, 12, 1, 1),
	SOC_SINGLE_TLV("Phone Playback Volume",
			ALC5632_PHONE_IN_VOL, 8, 31, 1, vol_tlv),
	SOC_DOUBLE_TLV("LineIn Playback Volume",
			ALC5632_LINE_IN_VOL, 8, 0, 31, 1, vol_tlv),
	SOC_DOUBLE_TLV("Master Playback Volume",
			ALC5632_STEREO_DAC_IN_VOL, 8, 0, 63, 1, vdac_tlv),
	SOC_DOUBLE("Master Playback Switch",
			ALC5632_STEREO_DAC_IN_VOL, 15, 7, 1, 1),
	SOC_SINGLE_TLV("Mic1 Playback Volume",
			ALC5632_MIC_VOL, 8, 31, 1, vol_tlv),
	SOC_SINGLE_TLV("Mic2 Playback Volume",
			ALC5632_MIC_VOL, 0, 31, 1, vol_tlv),
	SOC_DOUBLE_TLV("Rec Capture Volume",
			ALC5632_ADC_REC_GAIN, 8, 0, 31, 0, adc_rec_tlv),
	SOC_SINGLE_TLV("Mic 1 Boost Volume",
			ALC5632_MIC_CTRL, 10, 3, 0, boost_tlv),
	SOC_SINGLE_TLV("Mic 2 Boost Volume",
			ALC5632_MIC_CTRL, 8, 3, 0, boost_tlv),
	SOC_SINGLE_TLV("DMIC Boost Capture Volume",
			ALC5632_DIGI_BOOST_CTRL, 0, 7, 0, dig_tlv),
	SOC_SINGLE("DMIC En Capture Switch",
			ALC5632_DIGI_BOOST_CTRL, 15, 1, 0),
	SOC_SINGLE("DMIC PreFilter Capture Switch",
			ALC5632_DIGI_BOOST_CTRL, 12, 1, 0),
};

/*
 * DAPM Controls
 */
static const struct snd_kcontrol_new alc5632_hp_mixer_controls[] = {
SOC_DAPM_SINGLE("LI2HP Playback Switch", ALC5632_LINE_IN_VOL, 15, 1, 1),
SOC_DAPM_SINGLE("PHONE2HP Playback Switch", ALC5632_PHONE_IN_VOL, 15, 1, 1),
SOC_DAPM_SINGLE("MIC12HP Playback Switch", ALC5632_MIC_ROUTING_CTRL, 15, 1, 1),
SOC_DAPM_SINGLE("MIC22HP Playback Switch", ALC5632_MIC_ROUTING_CTRL, 11, 1, 1),
SOC_DAPM_SINGLE("VOICE2HP Playback Switch", ALC5632_VOICE_DAC_VOL, 15, 1, 1),
};

static const struct snd_kcontrol_new alc5632_hpl_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC2HP_L Playback Switch", ALC5632_ADC_REC_GAIN, 15, 1, 1),
SOC_DAPM_SINGLE("DACL2HP Playback Switch", ALC5632_MIC_ROUTING_CTRL, 3, 1, 1),
};

static const struct snd_kcontrol_new alc5632_hpr_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC2HP_R Playback Switch", ALC5632_ADC_REC_GAIN, 7, 1, 1),
SOC_DAPM_SINGLE("DACR2HP Playback Switch", ALC5632_MIC_ROUTING_CTRL, 2, 1, 1),
};

static const struct snd_kcontrol_new alc5632_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC2MONO_L Playback Switch", ALC5632_ADC_REC_GAIN, 14, 1, 1),
SOC_DAPM_SINGLE("ADC2MONO_R Playback Switch", ALC5632_ADC_REC_GAIN, 6, 1, 1),
SOC_DAPM_SINGLE("LI2MONO Playback Switch", ALC5632_LINE_IN_VOL, 13, 1, 1),
SOC_DAPM_SINGLE("MIC12MONO Playback Switch",
					ALC5632_MIC_ROUTING_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("MIC22MONO Playback Switch",
					ALC5632_MIC_ROUTING_CTRL, 9, 1, 1),
SOC_DAPM_SINGLE("DAC2MONO Playback Switch", ALC5632_MIC_ROUTING_CTRL, 0, 1, 1),
SOC_DAPM_SINGLE("VOICE2MONO Playback Switch", ALC5632_VOICE_DAC_VOL, 13, 1, 1),
};

static const struct snd_kcontrol_new alc5632_speaker_mixer_controls[] = {
SOC_DAPM_SINGLE("LI2SPK Playback Switch", ALC5632_LINE_IN_VOL, 14, 1, 1),
SOC_DAPM_SINGLE("PHONE2SPK Playback Switch", ALC5632_PHONE_IN_VOL, 14, 1, 1),
SOC_DAPM_SINGLE("MIC12SPK Playback Switch",
					ALC5632_MIC_ROUTING_CTRL, 14, 1, 1),
SOC_DAPM_SINGLE("MIC22SPK Playback Switch",
					ALC5632_MIC_ROUTING_CTRL, 10, 1, 1),
SOC_DAPM_SINGLE("DAC2SPK Playback Switch", ALC5632_MIC_ROUTING_CTRL, 1, 1, 1),
SOC_DAPM_SINGLE("VOICE2SPK Playback Switch", ALC5632_VOICE_DAC_VOL, 14, 1, 1),
};

/* Left Record Mixer */
static const struct snd_kcontrol_new alc5632_captureL_mixer_controls[] = {
SOC_DAPM_SINGLE("MIC12REC_L Capture Switch", ALC5632_ADC_REC_MIXER, 14, 1, 1),
SOC_DAPM_SINGLE("MIC22REC_L Capture Switch", ALC5632_ADC_REC_MIXER, 13, 1, 1),
SOC_DAPM_SINGLE("LIL2REC Capture Switch", ALC5632_ADC_REC_MIXER, 12, 1, 1),
SOC_DAPM_SINGLE("PH2REC_L Capture Switch", ALC5632_ADC_REC_MIXER, 11, 1, 1),
SOC_DAPM_SINGLE("HPL2REC Capture Switch", ALC5632_ADC_REC_MIXER, 10, 1, 1),
SOC_DAPM_SINGLE("SPK2REC_L Capture Switch", ALC5632_ADC_REC_MIXER, 9, 1, 1),
SOC_DAPM_SINGLE("MONO2REC_L Capture Switch", ALC5632_ADC_REC_MIXER, 8, 1, 1),
};

/* Right Record Mixer */
static const struct snd_kcontrol_new alc5632_captureR_mixer_controls[] = {
SOC_DAPM_SINGLE("MIC12REC_R Capture Switch", ALC5632_ADC_REC_MIXER, 6, 1, 1),
SOC_DAPM_SINGLE("MIC22REC_R Capture Switch", ALC5632_ADC_REC_MIXER, 5, 1, 1),
SOC_DAPM_SINGLE("LIR2REC Capture Switch", ALC5632_ADC_REC_MIXER, 4, 1, 1),
SOC_DAPM_SINGLE("PH2REC_R Capture Switch", ALC5632_ADC_REC_MIXER, 3, 1, 1),
SOC_DAPM_SINGLE("HPR2REC Capture Switch", ALC5632_ADC_REC_MIXER, 2, 1, 1),
SOC_DAPM_SINGLE("SPK2REC_R Capture Switch", ALC5632_ADC_REC_MIXER, 1, 1, 1),
SOC_DAPM_SINGLE("MONO2REC_R Capture Switch", ALC5632_ADC_REC_MIXER, 0, 1, 1),
};

/* Dmic Mixer */
static const struct snd_kcontrol_new alc5632_dmicl_mixer_controls[] = {
SOC_DAPM_SINGLE("DMICL2ADC Capture Switch", ALC5632_DIGI_BOOST_CTRL, 7, 1, 1),
};
static const struct snd_kcontrol_new alc5632_dmicr_mixer_controls[] = {
SOC_DAPM_SINGLE("DMICR2ADC Capture Switch", ALC5632_DIGI_BOOST_CTRL, 6, 1, 1),
};

static const char * const alc5632_spk_n_sour_sel[] = {
		"RN/-R", "RP/+R", "LN/-R", "Mute"};
static const char * const alc5632_hpl_out_input_sel[] = {
		"Vmid", "HP Left Mix"};
static const char * const alc5632_hpr_out_input_sel[] = {
		"Vmid", "HP Right Mix"};
static const char * const alc5632_spkout_input_sel[] = {
		"Vmid", "HPOut Mix", "Speaker Mix", "Mono Mix"};
static const char * const alc5632_aux_out_input_sel[] = {
		"Vmid", "HPOut Mix", "Speaker Mix", "Mono Mix"};
static const char * const alc5632_adcr_func_sel[] = {
		"Stereo ADC", "Voice ADC"};
static const char * const alc5632_i2s_out_sel[] = {
		"ADC LR", "Voice Stereo Digital"};

/* auxout output mux */
static SOC_ENUM_SINGLE_DECL(alc5632_aux_out_input_enum,
			    ALC5632_OUTPUT_MIXER_CTRL, 6,
			    alc5632_aux_out_input_sel);
static const struct snd_kcontrol_new alc5632_auxout_mux_controls =
SOC_DAPM_ENUM("AuxOut Mux", alc5632_aux_out_input_enum);

/* speaker output mux */
static SOC_ENUM_SINGLE_DECL(alc5632_spkout_input_enum,
			    ALC5632_OUTPUT_MIXER_CTRL, 10,
			    alc5632_spkout_input_sel);
static const struct snd_kcontrol_new alc5632_spkout_mux_controls =
SOC_DAPM_ENUM("SpeakerOut Mux", alc5632_spkout_input_enum);

/* headphone left output mux */
static SOC_ENUM_SINGLE_DECL(alc5632_hpl_out_input_enum,
			    ALC5632_OUTPUT_MIXER_CTRL, 9,
			    alc5632_hpl_out_input_sel);
static const struct snd_kcontrol_new alc5632_hpl_out_mux_controls =
SOC_DAPM_ENUM("Left Headphone Mux", alc5632_hpl_out_input_enum);

/* headphone right output mux */
static SOC_ENUM_SINGLE_DECL(alc5632_hpr_out_input_enum,
			    ALC5632_OUTPUT_MIXER_CTRL, 8,
			    alc5632_hpr_out_input_sel);
static const struct snd_kcontrol_new alc5632_hpr_out_mux_controls =
SOC_DAPM_ENUM("Right Headphone Mux", alc5632_hpr_out_input_enum);

/* speaker output N select */
static SOC_ENUM_SINGLE_DECL(alc5632_spk_n_sour_enum,
			    ALC5632_OUTPUT_MIXER_CTRL, 14,
			    alc5632_spk_n_sour_sel);
static const struct snd_kcontrol_new alc5632_spkoutn_mux_controls =
SOC_DAPM_ENUM("SpeakerOut N Mux", alc5632_spk_n_sour_enum);

/* speaker amplifier */
static const char *alc5632_amp_names[] = {"AB Amp", "D Amp"};
static SOC_ENUM_SINGLE_DECL(alc5632_amp_enum,
			    ALC5632_OUTPUT_MIXER_CTRL, 13,
			    alc5632_amp_names);
static const struct snd_kcontrol_new alc5632_amp_mux_controls =
	SOC_DAPM_ENUM("AB-D Amp Mux", alc5632_amp_enum);

/* ADC output select */
static SOC_ENUM_SINGLE_DECL(alc5632_adcr_func_enum,
			    ALC5632_DAC_FUNC_SELECT, 5,
			    alc5632_adcr_func_sel);
static const struct snd_kcontrol_new alc5632_adcr_func_controls =
	SOC_DAPM_ENUM("ADCR Mux", alc5632_adcr_func_enum);

/* I2S out select */
static SOC_ENUM_SINGLE_DECL(alc5632_i2s_out_enum,
			    ALC5632_I2S_OUT_CTL, 5,
			    alc5632_i2s_out_sel);
static const struct snd_kcontrol_new alc5632_i2s_out_controls =
	SOC_DAPM_ENUM("I2SOut Mux", alc5632_i2s_out_enum);

static const struct snd_soc_dapm_widget alc5632_dapm_widgets[] = {
/* Muxes */
SND_SOC_DAPM_MUX("AuxOut Mux", SND_SOC_NOPM, 0, 0,
	&alc5632_auxout_mux_controls),
SND_SOC_DAPM_MUX("SpeakerOut Mux", SND_SOC_NOPM, 0, 0,
	&alc5632_spkout_mux_controls),
SND_SOC_DAPM_MUX("Left Headphone Mux", SND_SOC_NOPM, 0, 0,
	&alc5632_hpl_out_mux_controls),
SND_SOC_DAPM_MUX("Right Headphone Mux", SND_SOC_NOPM, 0, 0,
	&alc5632_hpr_out_mux_controls),
SND_SOC_DAPM_MUX("SpeakerOut N Mux", SND_SOC_NOPM, 0, 0,
	&alc5632_spkoutn_mux_controls),
SND_SOC_DAPM_MUX("ADCR Mux", SND_SOC_NOPM, 0, 0,
	&alc5632_adcr_func_controls),
SND_SOC_DAPM_MUX("I2SOut Mux", ALC5632_PWR_MANAG_ADD1, 11, 0,
	&alc5632_i2s_out_controls),

/* output mixers */
SND_SOC_DAPM_MIXER("HP Mix", SND_SOC_NOPM, 0, 0,
	&alc5632_hp_mixer_controls[0],
	ARRAY_SIZE(alc5632_hp_mixer_controls)),
SND_SOC_DAPM_MIXER("HPR Mix", ALC5632_PWR_MANAG_ADD2, 4, 0,
	&alc5632_hpr_mixer_controls[0],
	ARRAY_SIZE(alc5632_hpr_mixer_controls)),
SND_SOC_DAPM_MIXER("HPL Mix", ALC5632_PWR_MANAG_ADD2, 5, 0,
	&alc5632_hpl_mixer_controls[0],
	ARRAY_SIZE(alc5632_hpl_mixer_controls)),
SND_SOC_DAPM_MIXER("HPOut Mix", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Mono Mix", ALC5632_PWR_MANAG_ADD2, 2, 0,
	&alc5632_mono_mixer_controls[0],
	ARRAY_SIZE(alc5632_mono_mixer_controls)),
SND_SOC_DAPM_MIXER("Speaker Mix", ALC5632_PWR_MANAG_ADD2, 3, 0,
	&alc5632_speaker_mixer_controls[0],
	ARRAY_SIZE(alc5632_speaker_mixer_controls)),
SND_SOC_DAPM_MIXER("DMICL Mix", SND_SOC_NOPM, 0, 0,
	&alc5632_dmicl_mixer_controls[0],
	ARRAY_SIZE(alc5632_dmicl_mixer_controls)),
SND_SOC_DAPM_MIXER("DMICR Mix", SND_SOC_NOPM, 0, 0,
	&alc5632_dmicr_mixer_controls[0],
	ARRAY_SIZE(alc5632_dmicr_mixer_controls)),

/* input mixers */
SND_SOC_DAPM_MIXER("Left Capture Mix", ALC5632_PWR_MANAG_ADD2, 1, 0,
	&alc5632_captureL_mixer_controls[0],
	ARRAY_SIZE(alc5632_captureL_mixer_controls)),
SND_SOC_DAPM_MIXER("Right Capture Mix", ALC5632_PWR_MANAG_ADD2, 0, 0,
	&alc5632_captureR_mixer_controls[0],
	ARRAY_SIZE(alc5632_captureR_mixer_controls)),

SND_SOC_DAPM_AIF_IN("AIFRXL", "Left HiFi Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIFRXR", "Right HiFi Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIFTXL", "Left HiFi Capture", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("AIFTXR", "Right HiFi Capture", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("VAIFRX", "Voice Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_OUT("VAIFTX", "Voice Capture", 0, SND_SOC_NOPM, 0, 0),

SND_SOC_DAPM_DAC("Voice DAC", NULL, ALC5632_PWR_MANAG_ADD2, 10, 0),
SND_SOC_DAPM_DAC("Left DAC", NULL, ALC5632_PWR_MANAG_ADD2, 9, 0),
SND_SOC_DAPM_DAC("Right DAC", NULL, ALC5632_PWR_MANAG_ADD2, 8, 0),
SND_SOC_DAPM_ADC("Left ADC", NULL, ALC5632_PWR_MANAG_ADD2, 7, 0),
SND_SOC_DAPM_ADC("Right ADC", NULL, ALC5632_PWR_MANAG_ADD2, 6, 0),

SND_SOC_DAPM_MIXER("DAC Left Channel", ALC5632_PWR_MANAG_ADD1, 15, 0, NULL, 0),
SND_SOC_DAPM_MIXER("DAC Right Channel",
	ALC5632_PWR_MANAG_ADD1, 14, 0, NULL, 0),
SND_SOC_DAPM_MIXER("I2S Mix", ALC5632_PWR_MANAG_ADD1, 11, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Phone Mix", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Line Mix", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Voice Mix", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("ADCLR", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_PGA("Left Headphone", ALC5632_PWR_MANAG_ADD3, 11, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Headphone", ALC5632_PWR_MANAG_ADD3, 10, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left Speaker", ALC5632_PWR_MANAG_ADD3, 13, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Speaker", ALC5632_PWR_MANAG_ADD3, 12, 0, NULL, 0),
SND_SOC_DAPM_PGA("Aux Out", ALC5632_PWR_MANAG_ADD3, 14, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left LineIn", ALC5632_PWR_MANAG_ADD3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right LineIn", ALC5632_PWR_MANAG_ADD3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Phone", ALC5632_PWR_MANAG_ADD3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("Phone ADMix", ALC5632_PWR_MANAG_ADD3, 4, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC1 PGA", ALC5632_PWR_MANAG_ADD3, 3, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC2 PGA", ALC5632_PWR_MANAG_ADD3, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC1 Pre Amp", ALC5632_PWR_MANAG_ADD3, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("MIC2 Pre Amp", ALC5632_PWR_MANAG_ADD3, 0, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS1", ALC5632_PWR_MANAG_ADD1, 3, 0, NULL, 0),
SND_SOC_DAPM_SUPPLY("MICBIAS2", ALC5632_PWR_MANAG_ADD1, 2, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("D Amp", ALC5632_PWR_MANAG_ADD2, 14, 0, NULL, 0,
	amp_mixer_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_PGA("AB Amp", ALC5632_PWR_MANAG_ADD2, 15, 0, NULL, 0),
SND_SOC_DAPM_MUX("AB-D Amp Mux", ALC5632_PWR_MANAG_ADD1, 10, 0,
	&alc5632_amp_mux_controls),

SND_SOC_DAPM_OUTPUT("AUXOUT"),
SND_SOC_DAPM_OUTPUT("HPL"),
SND_SOC_DAPM_OUTPUT("HPR"),
SND_SOC_DAPM_OUTPUT("SPKOUT"),
SND_SOC_DAPM_OUTPUT("SPKOUTN"),

SND_SOC_DAPM_INPUT("LINEINL"),
SND_SOC_DAPM_INPUT("LINEINR"),
SND_SOC_DAPM_INPUT("PHONEP"),
SND_SOC_DAPM_INPUT("PHONEN"),
SND_SOC_DAPM_INPUT("DMICDAT"),
SND_SOC_DAPM_INPUT("MIC1"),
SND_SOC_DAPM_INPUT("MIC2"),
SND_SOC_DAPM_VMID("Vmid"),
};


static const struct snd_soc_dapm_route alc5632_dapm_routes[] = {
	/* Playback streams */
	{"Left DAC", NULL, "AIFRXL"},
	{"Right DAC", NULL, "AIFRXR"},

	/* virtual mixer - mixes left & right channels */
	{"I2S Mix",	NULL,	"Left DAC"},
	{"I2S Mix",	NULL,	"Right DAC"},
	{"Line Mix",	NULL,	"Right LineIn"},
	{"Line Mix",	NULL,	"Left LineIn"},
	{"Phone Mix",	NULL,	"Phone"},
	{"Phone Mix",	NULL,	"Phone ADMix"},
	{"AUXOUT",		NULL,	"Aux Out"},

	/* DAC */
	{"DAC Right Channel",	NULL,	"I2S Mix"},
	{"DAC Left Channel",	NULL,   "I2S Mix"},

	/* HP mixer */
	{"HPL Mix",	"ADC2HP_L Playback Switch",	"Left Capture Mix"},
	{"HPL Mix", NULL,					"HP Mix"},
	{"HPR Mix", "ADC2HP_R Playback Switch",	"Right Capture Mix"},
	{"HPR Mix", NULL,					"HP Mix"},
	{"HP Mix",	"LI2HP Playback Switch",	"Line Mix"},
	{"HP Mix",	"PHONE2HP Playback Switch",	"Phone Mix"},
	{"HP Mix",	"MIC12HP Playback Switch",	"MIC1 PGA"},
	{"HP Mix",	"MIC22HP Playback Switch",	"MIC2 PGA"},
	{"HP Mix", "VOICE2HP Playback Switch",	"Voice Mix"},
	{"HPR Mix", "DACR2HP Playback Switch",	"DAC Right Channel"},
	{"HPL Mix", "DACL2HP Playback Switch",	"DAC Left Channel"},
	{"HPOut Mix", NULL, "HP Mix"},
	{"HPOut Mix", NULL, "HPR Mix"},
	{"HPOut Mix", NULL, "HPL Mix"},

	/* speaker mixer */
	{"Speaker Mix", "LI2SPK Playback Switch",	"Line Mix"},
	{"Speaker Mix", "PHONE2SPK Playback Switch", "Phone Mix"},
	{"Speaker Mix", "MIC12SPK Playback Switch",	"MIC1 PGA"},
	{"Speaker Mix", "MIC22SPK Playback Switch",	"MIC2 PGA"},
	{"Speaker Mix", "DAC2SPK Playback Switch",	"DAC Left Channel"},
	{"Speaker Mix", "VOICE2SPK Playback Switch",	"Voice Mix"},

	/* mono mixer */
	{"Mono Mix", "ADC2MONO_L Playback Switch",	"Left Capture Mix"},
	{"Mono Mix", "ADC2MONO_R Playback Switch",	"Right Capture Mix"},
	{"Mono Mix", "LI2MONO Playback Switch",		"Line Mix"},
	{"Mono Mix", "MIC12MONO Playback Switch",	"MIC1 PGA"},
	{"Mono Mix", "MIC22MONO Playback Switch",	"MIC2 PGA"},
	{"Mono Mix", "DAC2MONO Playback Switch",	"DAC Left Channel"},
	{"Mono Mix", "VOICE2MONO Playback Switch",	"Voice Mix"},

	/* Left record mixer */
	{"Left Capture Mix", "LIL2REC Capture Switch", "LINEINL"},
	{"Left Capture Mix", "PH2REC_L Capture Switch", "PHONEN"},
	{"Left Capture Mix", "MIC12REC_L Capture Switch", "MIC1 Pre Amp"},
	{"Left Capture Mix", "MIC22REC_L Capture Switch", "MIC2 Pre Amp"},
	{"Left Capture Mix", "HPL2REC Capture Switch", "HPL Mix"},
	{"Left Capture Mix", "SPK2REC_L Capture Switch", "Speaker Mix"},
	{"Left Capture Mix", "MONO2REC_L Capture Switch", "Mono Mix"},

	/*Right record mixer */
	{"Right Capture Mix", "LIR2REC Capture Switch", "LINEINR"},
	{"Right Capture Mix", "PH2REC_R Capture Switch", "PHONEP"},
	{"Right Capture Mix", "MIC12REC_R Capture Switch", "MIC1 Pre Amp"},
	{"Right Capture Mix", "MIC22REC_R Capture Switch", "MIC2 Pre Amp"},
	{"Right Capture Mix", "HPR2REC Capture Switch", "HPR Mix"},
	{"Right Capture Mix", "SPK2REC_R Capture Switch", "Speaker Mix"},
	{"Right Capture Mix", "MONO2REC_R Capture Switch", "Mono Mix"},

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
	{"Aux Out", NULL,				"AuxOut Mux"},

	/* input pga */
	{"Left LineIn", NULL,				"LINEINL"},
	{"Right LineIn", NULL,				"LINEINR"},
	{"Phone", NULL,				"PHONEP"},
	{"MIC1 Pre Amp", NULL,				"MIC1"},
	{"MIC2 Pre Amp", NULL,				"MIC2"},
	{"MIC1 PGA", NULL,				"MIC1 Pre Amp"},
	{"MIC2 PGA", NULL,				"MIC2 Pre Amp"},

	/* left ADC */
	{"Left ADC", NULL,				"Left Capture Mix"},
	{"DMICL Mix", "DMICL2ADC Capture Switch", "DMICDAT"},
	{"Left ADC", NULL,				"DMICL Mix"},
	{"ADCLR", NULL,					"Left ADC"},

	/* right ADC */
	{"Right ADC", NULL, "Right Capture Mix"},
	{"DMICR Mix", "DMICR2ADC Capture Switch", "DMICDAT"},
	{"Right ADC", NULL, "DMICR Mix"},
	{"ADCR Mux", "Stereo ADC", "Right ADC"},
	{"ADCR Mux", "Voice ADC", "Right ADC"},
	{"ADCLR", NULL, "ADCR Mux"},
	{"VAIFTX", NULL, "ADCR Mux"},

	/* Digital I2S out */
	{"I2SOut Mux", "ADC LR", "ADCLR"},
	{"I2SOut Mux", "Voice Stereo Digital", "VAIFRX"},
	{"AIFTXL", NULL, "I2SOut Mux"},
	{"AIFTXR", NULL, "I2SOut Mux"},

	/* Voice Mix */
	{"Voice DAC", NULL, "VAIFRX"},
	{"Voice Mix", NULL, "Voice DAC"},

	/* Speaker Output */
	{"SpeakerOut N Mux", "RN/-R",			"Left Speaker"},
	{"SpeakerOut N Mux", "RP/+R",			"Left Speaker"},
	{"SpeakerOut N Mux", "LN/-R",			"Left Speaker"},
	{"SpeakerOut N Mux", "Mute",			"Vmid"},

	{"SpeakerOut N Mux", "RN/-R",			"Right Speaker"},
	{"SpeakerOut N Mux", "RP/+R",			"Right Speaker"},
	{"SpeakerOut N Mux", "LN/-R",			"Right Speaker"},
	{"SpeakerOut N Mux", "Mute",			"Vmid"},

	{"AB Amp", NULL,				"SpeakerOut Mux"},
	{"D Amp", NULL,					"SpeakerOut Mux"},
	{"AB-D Amp Mux", "AB Amp",			"AB Amp"},
	{"AB-D Amp Mux", "D Amp",			"D Amp"},
	{"Left Speaker", NULL,				"AB-D Amp Mux"},
	{"Right Speaker", NULL,				"AB-D Amp Mux"},

	{"SPKOUT", NULL,				"Left Speaker"},
	{"SPKOUT", NULL,				"Right Speaker"},

	{"SPKOUTN", NULL,				"SpeakerOut N Mux"},

};

/* PLL divisors */
struct _pll_div {
	u32 pll_in;
	u32 pll_out;
	u16 regvalue;
};

/* Note : pll code from original alc5632 driver. Not sure of how good it is */
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

/* FOUT = MCLK*(N+2)/((M+2)*(K+2))
   N: bit 15:8 (div 2 .. div 257)
   K: bit  6:4 typical 2
   M: bit  3:0 (div 2 .. div 17)

   same as for 5623 - thanks!
*/

static const struct _pll_div codec_slave_pll_div[] = {

	{  1024000,  16384000,  0x3ea0},
	{  1411200,  22579200,	0x3ea0},
	{  1536000,  24576000,	0x3ea0},
	{  2048000,  16384000,  0x1ea0},
	{  2822400,  22579200,	0x1ea0},
	{  3072000,  24576000,	0x1ea0},

};

static int alc5632_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	int i;
	struct snd_soc_component *component = codec_dai->component;
	int gbl_clk = 0, pll_div = 0;
	u16 reg;

	if (pll_id < ALC5632_PLL_FR_MCLK || pll_id > ALC5632_PLL_FR_VBCLK)
		return -EINVAL;

	/* Disable PLL power */
	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD2,
				ALC5632_PWR_ADD2_PLL1,
				0);
	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD2,
				ALC5632_PWR_ADD2_PLL2,
				0);

	/* pll is not used in slave mode */
	reg = snd_soc_component_read(component, ALC5632_DAI_CONTROL);
	if (reg & ALC5632_DAI_SDP_SLAVE_MODE)
		return 0;

	if (!freq_in || !freq_out)
		return 0;

	switch (pll_id) {
	case ALC5632_PLL_FR_MCLK:
		for (i = 0; i < ARRAY_SIZE(codec_master_pll_div); i++) {
			if (codec_master_pll_div[i].pll_in == freq_in
			   && codec_master_pll_div[i].pll_out == freq_out) {
				/* PLL source from MCLK */
				pll_div  = codec_master_pll_div[i].regvalue;
				break;
			}
		}
		break;
	case ALC5632_PLL_FR_BCLK:
		for (i = 0; i < ARRAY_SIZE(codec_slave_pll_div); i++) {
			if (codec_slave_pll_div[i].pll_in == freq_in
			   && codec_slave_pll_div[i].pll_out == freq_out) {
				/* PLL source from Bitclk */
				gbl_clk = ALC5632_PLL_FR_BCLK;
				pll_div = codec_slave_pll_div[i].regvalue;
				break;
			}
		}
		break;
	case ALC5632_PLL_FR_VBCLK:
		for (i = 0; i < ARRAY_SIZE(codec_slave_pll_div); i++) {
			if (codec_slave_pll_div[i].pll_in == freq_in
			   && codec_slave_pll_div[i].pll_out == freq_out) {
				/* PLL source from voice clock */
				gbl_clk = ALC5632_PLL_FR_VBCLK;
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

	/* choose MCLK/BCLK/VBCLK */
	snd_soc_component_write(component, ALC5632_GPCR2, gbl_clk);
	/* choose PLL1 clock rate */
	snd_soc_component_write(component, ALC5632_PLL1_CTRL, pll_div);
	/* enable PLL1 */
	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD2,
				ALC5632_PWR_ADD2_PLL1,
				ALC5632_PWR_ADD2_PLL1);
	/* enable PLL2 */
	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD2,
				ALC5632_PWR_ADD2_PLL2,
				ALC5632_PWR_ADD2_PLL2);
	/* use PLL1 as main SYSCLK */
	snd_soc_component_update_bits(component, ALC5632_GPCR1,
			ALC5632_GPCR1_CLK_SYS_SRC_SEL_PLL1,
			ALC5632_GPCR1_CLK_SYS_SRC_SEL_PLL1);

	return 0;
}

struct _coeff_div {
	u16 fs;
	u16 regvalue;
};

/* codec hifi mclk (after PLL) clock divider coefficients */
/* values inspired from column BCLK=32Fs of Appendix A table */
static const struct _coeff_div coeff_div[] = {
	{512*1, 0x3075},
};

static int get_coeff(struct snd_soc_component *component, int rate)
{
	struct alc5632_priv *alc5632 = snd_soc_component_get_drvdata(component);
	int i;

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].fs * rate == alc5632->sysclk)
			return i;
	}
	return -EINVAL;
}

/*
 * Clock after PLL and dividers
 */
static int alc5632_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct alc5632_priv *alc5632 = snd_soc_component_get_drvdata(component);

	switch (freq) {
	case  4096000:
	case  8192000:
	case 11289600:
	case 12288000:
	case 16384000:
	case 16934400:
	case 18432000:
	case 22579200:
	case 24576000:
		alc5632->sysclk = freq;
		return 0;
	}
	return -EINVAL;
}

static int alc5632_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = ALC5632_DAI_SDP_MASTER_MODE;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface = ALC5632_DAI_SDP_SLAVE_MODE;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= ALC5632_DAI_I2S_DF_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= ALC5632_DAI_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= ALC5632_DAI_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= ALC5632_DAI_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= ALC5632_DAI_MAIN_I2S_BCLK_POL_CTRL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= ALC5632_DAI_MAIN_I2S_BCLK_POL_CTRL;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_component_write(component, ALC5632_DAI_CONTROL, iface);
}

static int alc5632_pcm_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	int coeff, rate;
	u16 iface;

	iface = snd_soc_component_read(component, ALC5632_DAI_CONTROL);
	iface &= ~ALC5632_DAI_I2S_DL_MASK;

	/* bit size */
	switch (params_width(params)) {
	case 16:
		iface |= ALC5632_DAI_I2S_DL_16;
		break;
	case 20:
		iface |= ALC5632_DAI_I2S_DL_20;
		break;
	case 24:
		iface |= ALC5632_DAI_I2S_DL_24;
		break;
	default:
		return -EINVAL;
	}

	/* set iface & srate */
	snd_soc_component_write(component, ALC5632_DAI_CONTROL, iface);
	rate = params_rate(params);
	coeff = get_coeff(component, rate);
	if (coeff < 0)
		return -EINVAL;

	coeff = coeff_div[coeff].regvalue;
	snd_soc_component_write(component, ALC5632_DAC_CLK_CTRL1, coeff);

	return 0;
}

static int alc5632_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	u16 hp_mute = ALC5632_MISC_HP_DEPOP_MUTE_L
						|ALC5632_MISC_HP_DEPOP_MUTE_R;
	u16 mute_reg = snd_soc_component_read(component, ALC5632_MISC_CTRL) & ~hp_mute;

	if (mute)
		mute_reg |= hp_mute;

	return snd_soc_component_write(component, ALC5632_MISC_CTRL, mute_reg);
}

#define ALC5632_ADD2_POWER_EN (ALC5632_PWR_ADD2_VREF)

#define ALC5632_ADD3_POWER_EN (ALC5632_PWR_ADD3_MIC1_BOOST_AD)

#define ALC5632_ADD1_POWER_EN \
		(ALC5632_PWR_ADD1_DAC_REF \
		| ALC5632_PWR_ADD1_SOFTGEN_EN \
		| ALC5632_PWR_ADD1_HP_OUT_AMP \
		| ALC5632_PWR_ADD1_HP_OUT_ENH_AMP \
		| ALC5632_PWR_ADD1_MAIN_BIAS)

static void enable_power_depop(struct snd_soc_component *component)
{
	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD1,
				ALC5632_PWR_ADD1_SOFTGEN_EN,
				ALC5632_PWR_ADD1_SOFTGEN_EN);

	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD3,
				ALC5632_ADD3_POWER_EN,
				ALC5632_ADD3_POWER_EN);

	snd_soc_component_update_bits(component, ALC5632_MISC_CTRL,
				ALC5632_MISC_HP_DEPOP_MODE2_EN,
				ALC5632_MISC_HP_DEPOP_MODE2_EN);

	/* "normal" mode: 0 @ 26 */
	/* set all PR0-7 mixers to 0 */
	snd_soc_component_update_bits(component, ALC5632_PWR_DOWN_CTRL_STATUS,
				ALC5632_PWR_DOWN_CTRL_STATUS_MASK,
				0);

	msleep(500);

	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD2,
				ALC5632_ADD2_POWER_EN,
				ALC5632_ADD2_POWER_EN);

	snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD1,
				ALC5632_ADD1_POWER_EN,
				ALC5632_ADD1_POWER_EN);

	/* disable HP Depop2 */
	snd_soc_component_update_bits(component, ALC5632_MISC_CTRL,
				ALC5632_MISC_HP_DEPOP_MODE2_EN,
				0);

}

static int alc5632_set_bias_level(struct snd_soc_component *component,
				      enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		enable_power_depop(component);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		/* everything off except vref/vmid, */
		snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD1,
				ALC5632_PWR_MANAG_ADD1_MASK,
				ALC5632_PWR_ADD1_MAIN_BIAS);
		snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD2,
				ALC5632_PWR_MANAG_ADD2_MASK,
				ALC5632_PWR_ADD2_VREF);
		/* "normal" mode: 0 @ 26 */
		snd_soc_component_update_bits(component, ALC5632_PWR_DOWN_CTRL_STATUS,
				ALC5632_PWR_DOWN_CTRL_STATUS_MASK,
				0xffff ^ (ALC5632_PWR_VREF_PR3
				| ALC5632_PWR_VREF_PR2));
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off, dac mute, inactive */
		snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD2,
				ALC5632_PWR_MANAG_ADD2_MASK, 0);
		snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD3,
				ALC5632_PWR_MANAG_ADD3_MASK, 0);
		snd_soc_component_update_bits(component, ALC5632_PWR_MANAG_ADD1,
				ALC5632_PWR_MANAG_ADD1_MASK, 0);
		break;
	}
	return 0;
}

#define ALC5632_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE \
			| SNDRV_PCM_FMTBIT_S24_LE \
			| SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops alc5632_dai_ops = {
		.hw_params = alc5632_pcm_hw_params,
		.mute_stream = alc5632_mute,
		.set_fmt = alc5632_set_dai_fmt,
		.set_sysclk = alc5632_set_dai_sysclk,
		.set_pll = alc5632_set_dai_pll,
		.no_capture_mute = 1,
};

static struct snd_soc_dai_driver alc5632_dai = {
	.name = "alc5632-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rate_min =	8000,
		.rate_max =	48000,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = ALC5632_FORMATS,},
	.capture = {
		.stream_name = "HiFi Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rate_min =	8000,
		.rate_max =	48000,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = ALC5632_FORMATS,},

	.ops = &alc5632_dai_ops,
	.symmetric_rate = 1,
};

#ifdef CONFIG_PM
static int alc5632_resume(struct snd_soc_component *component)
{
	struct alc5632_priv *alc5632 = snd_soc_component_get_drvdata(component);

	regcache_sync(alc5632->regmap);

	return 0;
}
#else
#define	alc5632_resume	NULL
#endif

static int alc5632_probe(struct snd_soc_component *component)
{
	struct alc5632_priv *alc5632 = snd_soc_component_get_drvdata(component);

	switch (alc5632->id) {
	case 0x5c:
		snd_soc_add_component_controls(component, alc5632_vol_snd_controls,
			ARRAY_SIZE(alc5632_vol_snd_controls));
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_component_driver soc_component_device_alc5632 = {
	.probe			= alc5632_probe,
	.resume			= alc5632_resume,
	.set_bias_level		= alc5632_set_bias_level,
	.controls		= alc5632_snd_controls,
	.num_controls		= ARRAY_SIZE(alc5632_snd_controls),
	.dapm_widgets		= alc5632_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(alc5632_dapm_widgets),
	.dapm_routes		= alc5632_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(alc5632_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config alc5632_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = ALC5632_MAX_REGISTER,
	.reg_defaults = alc5632_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(alc5632_reg_defaults),
	.volatile_reg = alc5632_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * alc5632 2 wire address is determined by A1 pin
 * state during powerup.
 *    low  = 0x1a
 *    high = 0x1b
 */
static int alc5632_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct alc5632_priv *alc5632;
	int ret, ret1, ret2;
	unsigned int vid1, vid2;

	alc5632 = devm_kzalloc(&client->dev,
			 sizeof(struct alc5632_priv), GFP_KERNEL);
	if (alc5632 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(client, alc5632);

	alc5632->regmap = devm_regmap_init_i2c(client, &alc5632_regmap);
	if (IS_ERR(alc5632->regmap)) {
		ret = PTR_ERR(alc5632->regmap);
		dev_err(&client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}

	ret1 = regmap_read(alc5632->regmap, ALC5632_VENDOR_ID1, &vid1);
	ret2 = regmap_read(alc5632->regmap, ALC5632_VENDOR_ID2, &vid2);
	if (ret1 != 0 || ret2 != 0) {
		dev_err(&client->dev,
		"Failed to read chip ID: ret1=%d, ret2=%d\n", ret1, ret2);
		return -EIO;
	}

	vid2 >>= 8;

	if ((vid1 != 0x10EC) || (vid2 != id->driver_data)) {
		dev_err(&client->dev,
		"Device is not a ALC5632: VID1=0x%x, VID2=0x%x\n", vid1, vid2);
		return -EINVAL;
	}

	ret = alc5632_reset(alc5632->regmap);
	if (ret < 0) {
		dev_err(&client->dev, "Failed to issue reset\n");
		return ret;
	}

	alc5632->id = vid2;
	switch (alc5632->id) {
	case 0x5c:
		alc5632_dai.name = "alc5632-hifi";
		break;
	default:
		return -EINVAL;
	}

	ret = devm_snd_soc_register_component(&client->dev,
		&soc_component_device_alc5632, &alc5632_dai, 1);

	if (ret < 0) {
		dev_err(&client->dev, "Failed to register component: %d\n", ret);
		return ret;
	}

	return ret;
}

static const struct i2c_device_id alc5632_i2c_table[] = {
	{"alc5632", 0x5c},
	{}
};
MODULE_DEVICE_TABLE(i2c, alc5632_i2c_table);

#ifdef CONFIG_OF
static const struct of_device_id alc5632_of_match[] = {
	{ .compatible = "realtek,alc5632", },
	{ }
};
MODULE_DEVICE_TABLE(of, alc5632_of_match);
#endif

/* i2c codec control layer */
static struct i2c_driver alc5632_i2c_driver = {
	.driver = {
		.name = "alc5632",
		.of_match_table = of_match_ptr(alc5632_of_match),
	},
	.probe = alc5632_i2c_probe,
	.id_table = alc5632_i2c_table,
};

module_i2c_driver(alc5632_i2c_driver);

MODULE_DESCRIPTION("ASoC ALC5632 driver");
MODULE_AUTHOR("Leon Romanovsky <leon@leon.nu>");
MODULE_LICENSE("GPL");
