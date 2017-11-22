/*
 * cs42l52.c -- CS42L52 ALSA SoC audio driver
 *
 * Copyright 2012 CirrusLogic, Inc.
 *
 * Author: Georgi Vlaev <joe@nucleusys.com>
 * Author: Brian Austin <brian.austin@cirrus.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/cs42l52.h>
#include "cs42l52.h"

struct sp_config {
	u8 spc, format, spfs;
	u32 srate;
};

struct  cs42l52_private {
	struct regmap *regmap;
	struct snd_soc_codec *codec;
	struct device *dev;
	struct sp_config config;
	struct cs42l52_platform_data pdata;
	u32 sysclk;
	u8 mclksel;
	u32 mclk;
	u8 flags;
	struct input_dev *beep;
	struct work_struct beep_work;
	int beep_rate;
};

static const struct reg_default cs42l52_reg_defaults[] = {
	{ CS42L52_PWRCTL1, 0x9F },	/* r02 PWRCTL 1 */
	{ CS42L52_PWRCTL2, 0x07 },	/* r03 PWRCTL 2 */
	{ CS42L52_PWRCTL3, 0xFF },	/* r04 PWRCTL 3 */
	{ CS42L52_CLK_CTL, 0xA0 },	/* r05 Clocking Ctl */
	{ CS42L52_IFACE_CTL1, 0x00 },	/* r06 Interface Ctl 1 */
	{ CS42L52_ADC_PGA_A, 0x80 },	/* r08 Input A Select */
	{ CS42L52_ADC_PGA_B, 0x80 },	/* r09 Input B Select */
	{ CS42L52_ANALOG_HPF_CTL, 0xA5 },	/* r0A Analog HPF Ctl */
	{ CS42L52_ADC_HPF_FREQ, 0x00 },	/* r0B ADC HPF Corner Freq */
	{ CS42L52_ADC_MISC_CTL, 0x00 },	/* r0C Misc. ADC Ctl */
	{ CS42L52_PB_CTL1, 0x60 },	/* r0D Playback Ctl 1 */
	{ CS42L52_MISC_CTL, 0x02 },	/* r0E Misc. Ctl */
	{ CS42L52_PB_CTL2, 0x00 },	/* r0F Playback Ctl 2 */
	{ CS42L52_MICA_CTL, 0x00 },	/* r10 MICA Amp Ctl */
	{ CS42L52_MICB_CTL, 0x00 },	/* r11 MICB Amp Ctl */
	{ CS42L52_PGAA_CTL, 0x00 },	/* r12 PGAA Vol, Misc. */
	{ CS42L52_PGAB_CTL, 0x00 },	/* r13 PGAB Vol, Misc. */
	{ CS42L52_PASSTHRUA_VOL, 0x00 },	/* r14 Bypass A Vol */
	{ CS42L52_PASSTHRUB_VOL, 0x00 },	/* r15 Bypass B Vol */
	{ CS42L52_ADCA_VOL, 0x00 },	/* r16 ADCA Volume */
	{ CS42L52_ADCB_VOL, 0x00 },	/* r17 ADCB Volume */
	{ CS42L52_ADCA_MIXER_VOL, 0x80 },	/* r18 ADCA Mixer Volume */
	{ CS42L52_ADCB_MIXER_VOL, 0x80 },	/* r19 ADCB Mixer Volume */
	{ CS42L52_PCMA_MIXER_VOL, 0x00 },	/* r1A PCMA Mixer Volume */
	{ CS42L52_PCMB_MIXER_VOL, 0x00 },	/* r1B PCMB Mixer Volume */
	{ CS42L52_BEEP_FREQ, 0x00 },	/* r1C Beep Freq on Time */
	{ CS42L52_BEEP_VOL, 0x00 },	/* r1D Beep Volume off Time */
	{ CS42L52_BEEP_TONE_CTL, 0x00 },	/* r1E Beep Tone Cfg. */
	{ CS42L52_TONE_CTL, 0x00 },	/* r1F Tone Ctl */
	{ CS42L52_MASTERA_VOL, 0x00 },	/* r20 Master A Volume */
	{ CS42L52_MASTERB_VOL, 0x00 },	/* r21 Master B Volume */
	{ CS42L52_HPA_VOL, 0x00 },	/* r22 Headphone A Volume */
	{ CS42L52_HPB_VOL, 0x00 },	/* r23 Headphone B Volume */
	{ CS42L52_SPKA_VOL, 0x00 },	/* r24 Speaker A Volume */
	{ CS42L52_SPKB_VOL, 0x00 },	/* r25 Speaker B Volume */
	{ CS42L52_ADC_PCM_MIXER, 0x00 },	/* r26 Channel Mixer and Swap */
	{ CS42L52_LIMITER_CTL1, 0x00 },	/* r27 Limit Ctl 1 Thresholds */
	{ CS42L52_LIMITER_CTL2, 0x7F },	/* r28 Limit Ctl 2 Release Rate */
	{ CS42L52_LIMITER_AT_RATE, 0xC0 },	/* r29 Limiter Attack Rate */
	{ CS42L52_ALC_CTL, 0x00 },	/* r2A ALC Ctl 1 Attack Rate */
	{ CS42L52_ALC_RATE, 0x3F },	/* r2B ALC Release Rate */
	{ CS42L52_ALC_THRESHOLD, 0x3f },	/* r2C ALC Thresholds */
	{ CS42L52_NOISE_GATE_CTL, 0x00 },	/* r2D Noise Gate Ctl */
	{ CS42L52_CLK_STATUS, 0x00 },	/* r2E Overflow and Clock Status */
	{ CS42L52_BATT_COMPEN, 0x00 },	/* r2F battery Compensation */
	{ CS42L52_BATT_LEVEL, 0x00 },	/* r30 VP Battery Level */
	{ CS42L52_SPK_STATUS, 0x00 },	/* r31 Speaker Status */
	{ CS42L52_TEM_CTL, 0x3B },	/* r32 Temp Ctl */
	{ CS42L52_THE_FOLDBACK, 0x00 },	/* r33 Foldback */
};

static bool cs42l52_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L52_CHIP ... CS42L52_CHARGE_PUMP:
		return true;
	default:
		return false;
	}
}

static bool cs42l52_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case CS42L52_IFACE_CTL2:
	case CS42L52_CLK_STATUS:
	case CS42L52_BATT_LEVEL:
	case CS42L52_SPK_STATUS:
	case CS42L52_CHARGE_PUMP:
		return true;
	default:
		return false;
	}
}

static DECLARE_TLV_DB_SCALE(hl_tlv, -10200, 50, 0);

static DECLARE_TLV_DB_SCALE(hpd_tlv, -9600, 50, 1);

static DECLARE_TLV_DB_SCALE(ipd_tlv, -9600, 100, 0);

static DECLARE_TLV_DB_SCALE(mic_tlv, 1600, 100, 0);

static DECLARE_TLV_DB_SCALE(pga_tlv, -600, 50, 0);

static DECLARE_TLV_DB_SCALE(mix_tlv, -50, 50, 0);

static DECLARE_TLV_DB_SCALE(beep_tlv, -56, 200, 0);

static const DECLARE_TLV_DB_RANGE(limiter_tlv,
	0, 2, TLV_DB_SCALE_ITEM(-3000, 600, 0),
	3, 7, TLV_DB_SCALE_ITEM(-1200, 300, 0)
);

static const char * const cs42l52_adca_text[] = {
	"Input1A", "Input2A", "Input3A", "Input4A", "PGA Input Left"};

static const char * const cs42l52_adcb_text[] = {
	"Input1B", "Input2B", "Input3B", "Input4B", "PGA Input Right"};

static SOC_ENUM_SINGLE_DECL(adca_enum,
			    CS42L52_ADC_PGA_A, 5, cs42l52_adca_text);

static SOC_ENUM_SINGLE_DECL(adcb_enum,
			    CS42L52_ADC_PGA_B, 5, cs42l52_adcb_text);

static const struct snd_kcontrol_new adca_mux =
	SOC_DAPM_ENUM("Left ADC Input Capture Mux", adca_enum);

static const struct snd_kcontrol_new adcb_mux =
	SOC_DAPM_ENUM("Right ADC Input Capture Mux", adcb_enum);

static const char * const mic_bias_level_text[] = {
	"0.5 +VA", "0.6 +VA", "0.7 +VA",
	"0.8 +VA", "0.83 +VA", "0.91 +VA"
};

static SOC_ENUM_SINGLE_DECL(mic_bias_level_enum,
			    CS42L52_IFACE_CTL2, 0, mic_bias_level_text);

static const char * const cs42l52_mic_text[] = { "MIC1", "MIC2" };

static SOC_ENUM_SINGLE_DECL(mica_enum,
			    CS42L52_MICA_CTL, 5, cs42l52_mic_text);

static SOC_ENUM_SINGLE_DECL(micb_enum,
			    CS42L52_MICB_CTL, 5, cs42l52_mic_text);

static const char * const digital_output_mux_text[] = {"ADC", "DSP"};

static SOC_ENUM_SINGLE_DECL(digital_output_mux_enum,
			    CS42L52_ADC_MISC_CTL, 6,
			    digital_output_mux_text);

static const struct snd_kcontrol_new digital_output_mux =
	SOC_DAPM_ENUM("Digital Output Mux", digital_output_mux_enum);

static const char * const hp_gain_num_text[] = {
	"0.3959", "0.4571", "0.5111", "0.6047",
	"0.7099", "0.8399", "1.000", "1.1430"
};

static SOC_ENUM_SINGLE_DECL(hp_gain_enum,
			    CS42L52_PB_CTL1, 5,
			    hp_gain_num_text);

static const char * const beep_pitch_text[] = {
	"C4", "C5", "D5", "E5", "F5", "G5", "A5", "B5",
	"C6", "D6", "E6", "F6", "G6", "A6", "B6", "C7"
};

static SOC_ENUM_SINGLE_DECL(beep_pitch_enum,
			    CS42L52_BEEP_FREQ, 4,
			    beep_pitch_text);

static const char * const beep_ontime_text[] = {
	"86 ms", "430 ms", "780 ms", "1.20 s", "1.50 s",
	"1.80 s", "2.20 s", "2.50 s", "2.80 s", "3.20 s",
	"3.50 s", "3.80 s", "4.20 s", "4.50 s", "4.80 s", "5.20 s"
};

static SOC_ENUM_SINGLE_DECL(beep_ontime_enum,
			    CS42L52_BEEP_FREQ, 0,
			    beep_ontime_text);

static const char * const beep_offtime_text[] = {
	"1.23 s", "2.58 s", "3.90 s", "5.20 s",
	"6.60 s", "8.05 s", "9.35 s", "10.80 s"
};

static SOC_ENUM_SINGLE_DECL(beep_offtime_enum,
			    CS42L52_BEEP_VOL, 5,
			    beep_offtime_text);

static const char * const beep_config_text[] = {
	"Off", "Single", "Multiple", "Continuous"
};

static SOC_ENUM_SINGLE_DECL(beep_config_enum,
			    CS42L52_BEEP_TONE_CTL, 6,
			    beep_config_text);

static const char * const beep_bass_text[] = {
	"50 Hz", "100 Hz", "200 Hz", "250 Hz"
};

static SOC_ENUM_SINGLE_DECL(beep_bass_enum,
			    CS42L52_BEEP_TONE_CTL, 1,
			    beep_bass_text);

static const char * const beep_treble_text[] = {
	"5 kHz", "7 kHz", "10 kHz", " 15 kHz"
};

static SOC_ENUM_SINGLE_DECL(beep_treble_enum,
			    CS42L52_BEEP_TONE_CTL, 3,
			    beep_treble_text);

static const char * const ng_threshold_text[] = {
	"-34dB", "-37dB", "-40dB", "-43dB",
	"-46dB", "-52dB", "-58dB", "-64dB"
};

static SOC_ENUM_SINGLE_DECL(ng_threshold_enum,
			    CS42L52_NOISE_GATE_CTL, 2,
			    ng_threshold_text);

static const char * const cs42l52_ng_delay_text[] = {
	"50ms", "100ms", "150ms", "200ms"};

static SOC_ENUM_SINGLE_DECL(ng_delay_enum,
			    CS42L52_NOISE_GATE_CTL, 0,
			    cs42l52_ng_delay_text);

static const char * const cs42l52_ng_type_text[] = {
	"Apply Specific", "Apply All"
};

static SOC_ENUM_SINGLE_DECL(ng_type_enum,
			    CS42L52_NOISE_GATE_CTL, 6,
			    cs42l52_ng_type_text);

static const char * const left_swap_text[] = {
	"Left", "LR 2", "Right"};

static const char * const right_swap_text[] = {
	"Right", "LR 2", "Left"};

static const unsigned int swap_values[] = { 0, 1, 3 };

static const struct soc_enum adca_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L52_ADC_PCM_MIXER, 2, 3,
			      ARRAY_SIZE(left_swap_text),
			      left_swap_text,
			      swap_values);

static const struct snd_kcontrol_new adca_mixer =
	SOC_DAPM_ENUM("Route", adca_swap_enum);

static const struct soc_enum pcma_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L52_ADC_PCM_MIXER, 6, 3,
			      ARRAY_SIZE(left_swap_text),
			      left_swap_text,
			      swap_values);

static const struct snd_kcontrol_new pcma_mixer =
	SOC_DAPM_ENUM("Route", pcma_swap_enum);

static const struct soc_enum adcb_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L52_ADC_PCM_MIXER, 0, 3,
			      ARRAY_SIZE(right_swap_text),
			      right_swap_text,
			      swap_values);

static const struct snd_kcontrol_new adcb_mixer =
	SOC_DAPM_ENUM("Route", adcb_swap_enum);

static const struct soc_enum pcmb_swap_enum =
	SOC_VALUE_ENUM_SINGLE(CS42L52_ADC_PCM_MIXER, 4, 3,
			      ARRAY_SIZE(right_swap_text),
			      right_swap_text,
			      swap_values);

static const struct snd_kcontrol_new pcmb_mixer =
	SOC_DAPM_ENUM("Route", pcmb_swap_enum);


static const struct snd_kcontrol_new passthrul_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L52_MISC_CTL, 6, 1, 0);

static const struct snd_kcontrol_new passthrur_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L52_MISC_CTL, 7, 1, 0);

static const struct snd_kcontrol_new spkl_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L52_PWRCTL3, 0, 1, 1);

static const struct snd_kcontrol_new spkr_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L52_PWRCTL3, 2, 1, 1);

static const struct snd_kcontrol_new hpl_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L52_PWRCTL3, 4, 1, 1);

static const struct snd_kcontrol_new hpr_ctl =
	SOC_DAPM_SINGLE("Switch", CS42L52_PWRCTL3, 6, 1, 1);

static const struct snd_kcontrol_new cs42l52_snd_controls[] = {

	SOC_DOUBLE_R_SX_TLV("Master Volume", CS42L52_MASTERA_VOL,
			      CS42L52_MASTERB_VOL, 0, 0x34, 0xE4, hl_tlv),

	SOC_DOUBLE_R_SX_TLV("Headphone Volume", CS42L52_HPA_VOL,
			      CS42L52_HPB_VOL, 0, 0x34, 0xC0, hpd_tlv),

	SOC_ENUM("Headphone Analog Gain", hp_gain_enum),

	SOC_DOUBLE_R_SX_TLV("Speaker Volume", CS42L52_SPKA_VOL,
			      CS42L52_SPKB_VOL, 0, 0x40, 0xC0, hl_tlv),

	SOC_DOUBLE_R_SX_TLV("Bypass Volume", CS42L52_PASSTHRUA_VOL,
			      CS42L52_PASSTHRUB_VOL, 0, 0x88, 0x90, pga_tlv),

	SOC_DOUBLE("Bypass Mute", CS42L52_MISC_CTL, 4, 5, 1, 0),

	SOC_DOUBLE_R_TLV("MIC Gain Volume", CS42L52_MICA_CTL,
			      CS42L52_MICB_CTL, 0, 0x10, 0, mic_tlv),

	SOC_ENUM("MIC Bias Level", mic_bias_level_enum),

	SOC_DOUBLE_R_SX_TLV("ADC Volume", CS42L52_ADCA_VOL,
			      CS42L52_ADCB_VOL, 0, 0xA0, 0x78, ipd_tlv),
	SOC_DOUBLE_R_SX_TLV("ADC Mixer Volume",
			     CS42L52_ADCA_MIXER_VOL, CS42L52_ADCB_MIXER_VOL,
				0, 0x19, 0x7F, ipd_tlv),

	SOC_DOUBLE("ADC Switch", CS42L52_ADC_MISC_CTL, 0, 1, 1, 0),

	SOC_DOUBLE_R("ADC Mixer Switch", CS42L52_ADCA_MIXER_VOL,
		     CS42L52_ADCB_MIXER_VOL, 7, 1, 1),

	SOC_DOUBLE_R_SX_TLV("PGA Volume", CS42L52_PGAA_CTL,
			    CS42L52_PGAB_CTL, 0, 0x28, 0x24, pga_tlv),

	SOC_DOUBLE_R_SX_TLV("PCM Mixer Volume",
			    CS42L52_PCMA_MIXER_VOL, CS42L52_PCMB_MIXER_VOL,
				0, 0x19, 0x7f, mix_tlv),
	SOC_DOUBLE_R("PCM Mixer Switch",
		     CS42L52_PCMA_MIXER_VOL, CS42L52_PCMB_MIXER_VOL, 7, 1, 1),

	SOC_ENUM("Beep Config", beep_config_enum),
	SOC_ENUM("Beep Pitch", beep_pitch_enum),
	SOC_ENUM("Beep on Time", beep_ontime_enum),
	SOC_ENUM("Beep off Time", beep_offtime_enum),
	SOC_SINGLE_SX_TLV("Beep Volume", CS42L52_BEEP_VOL,
			0, 0x07, 0x1f, beep_tlv),
	SOC_SINGLE("Beep Mixer Switch", CS42L52_BEEP_TONE_CTL, 5, 1, 1),
	SOC_ENUM("Beep Treble Corner Freq", beep_treble_enum),
	SOC_ENUM("Beep Bass Corner Freq", beep_bass_enum),

	SOC_SINGLE("Tone Control Switch", CS42L52_BEEP_TONE_CTL, 0, 1, 1),
	SOC_SINGLE_TLV("Treble Gain Volume",
			    CS42L52_TONE_CTL, 4, 15, 1, hl_tlv),
	SOC_SINGLE_TLV("Bass Gain Volume",
			    CS42L52_TONE_CTL, 0, 15, 1, hl_tlv),

	/* Limiter */
	SOC_SINGLE_TLV("Limiter Max Threshold Volume",
		       CS42L52_LIMITER_CTL1, 5, 7, 0, limiter_tlv),
	SOC_SINGLE_TLV("Limiter Cushion Threshold Volume",
		       CS42L52_LIMITER_CTL1, 2, 7, 0, limiter_tlv),
	SOC_SINGLE_TLV("Limiter Release Rate Volume",
		       CS42L52_LIMITER_CTL2, 0, 63, 0, limiter_tlv),
	SOC_SINGLE_TLV("Limiter Attack Rate Volume",
		       CS42L52_LIMITER_AT_RATE, 0, 63, 0, limiter_tlv),

	SOC_SINGLE("Limiter SR Switch", CS42L52_LIMITER_CTL1, 1, 1, 0),
	SOC_SINGLE("Limiter ZC Switch", CS42L52_LIMITER_CTL1, 0, 1, 0),
	SOC_SINGLE("Limiter Switch", CS42L52_LIMITER_CTL2, 7, 1, 0),

	/* ALC */
	SOC_SINGLE_TLV("ALC Attack Rate Volume", CS42L52_ALC_CTL,
		       0, 63, 0, limiter_tlv),
	SOC_SINGLE_TLV("ALC Release Rate Volume", CS42L52_ALC_RATE,
		       0, 63, 0, limiter_tlv),
	SOC_SINGLE_TLV("ALC Max Threshold Volume", CS42L52_ALC_THRESHOLD,
		       5, 7, 0, limiter_tlv),
	SOC_SINGLE_TLV("ALC Min Threshold Volume", CS42L52_ALC_THRESHOLD,
		       2, 7, 0, limiter_tlv),

	SOC_DOUBLE_R("ALC SR Capture Switch", CS42L52_PGAA_CTL,
		     CS42L52_PGAB_CTL, 7, 1, 1),
	SOC_DOUBLE_R("ALC ZC Capture Switch", CS42L52_PGAA_CTL,
		     CS42L52_PGAB_CTL, 6, 1, 1),
	SOC_DOUBLE("ALC Capture Switch", CS42L52_ALC_CTL, 6, 7, 1, 0),

	/* Noise gate */
	SOC_ENUM("NG Type Switch", ng_type_enum),
	SOC_SINGLE("NG Enable Switch", CS42L52_NOISE_GATE_CTL, 6, 1, 0),
	SOC_SINGLE("NG Boost Switch", CS42L52_NOISE_GATE_CTL, 5, 1, 1),
	SOC_ENUM("NG Threshold", ng_threshold_enum),
	SOC_ENUM("NG Delay", ng_delay_enum),

	SOC_DOUBLE("HPF Switch", CS42L52_ANALOG_HPF_CTL, 5, 7, 1, 0),

	SOC_DOUBLE("Analog SR Switch", CS42L52_ANALOG_HPF_CTL, 1, 3, 1, 1),
	SOC_DOUBLE("Analog ZC Switch", CS42L52_ANALOG_HPF_CTL, 0, 2, 1, 1),
	SOC_SINGLE("Digital SR Switch", CS42L52_MISC_CTL, 1, 1, 0),
	SOC_SINGLE("Digital ZC Switch", CS42L52_MISC_CTL, 0, 1, 0),
	SOC_SINGLE("Deemphasis Switch", CS42L52_MISC_CTL, 2, 1, 0),

	SOC_SINGLE("Batt Compensation Switch", CS42L52_BATT_COMPEN, 7, 1, 0),
	SOC_SINGLE("Batt VP Monitor Switch", CS42L52_BATT_COMPEN, 6, 1, 0),
	SOC_SINGLE("Batt VP ref", CS42L52_BATT_COMPEN, 0, 0x0f, 0),

	SOC_SINGLE("PGA AIN1L Switch", CS42L52_ADC_PGA_A, 0, 1, 0),
	SOC_SINGLE("PGA AIN1R Switch", CS42L52_ADC_PGA_B, 0, 1, 0),
	SOC_SINGLE("PGA AIN2L Switch", CS42L52_ADC_PGA_A, 1, 1, 0),
	SOC_SINGLE("PGA AIN2R Switch", CS42L52_ADC_PGA_B, 1, 1, 0),

	SOC_SINGLE("PGA AIN3L Switch", CS42L52_ADC_PGA_A, 2, 1, 0),
	SOC_SINGLE("PGA AIN3R Switch", CS42L52_ADC_PGA_B, 2, 1, 0),

	SOC_SINGLE("PGA AIN4L Switch", CS42L52_ADC_PGA_A, 3, 1, 0),
	SOC_SINGLE("PGA AIN4R Switch", CS42L52_ADC_PGA_B, 3, 1, 0),

	SOC_SINGLE("PGA MICA Switch", CS42L52_ADC_PGA_A, 4, 1, 0),
	SOC_SINGLE("PGA MICB Switch", CS42L52_ADC_PGA_B, 4, 1, 0),

};

static const struct snd_kcontrol_new cs42l52_mica_controls[] = {
	SOC_ENUM("MICA Select", mica_enum),
};

static const struct snd_kcontrol_new cs42l52_micb_controls[] = {
	SOC_ENUM("MICB Select", micb_enum),
};

static int cs42l52_add_mic_controls(struct snd_soc_codec *codec)
{
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);
	struct cs42l52_platform_data *pdata = &cs42l52->pdata;

	if (!pdata->mica_diff_cfg)
		snd_soc_add_codec_controls(codec, cs42l52_mica_controls,
				     ARRAY_SIZE(cs42l52_mica_controls));

	if (!pdata->micb_diff_cfg)
		snd_soc_add_codec_controls(codec, cs42l52_micb_controls,
				     ARRAY_SIZE(cs42l52_micb_controls));

	return 0;
}

static const struct snd_soc_dapm_widget cs42l52_dapm_widgets[] = {

	SND_SOC_DAPM_INPUT("AIN1L"),
	SND_SOC_DAPM_INPUT("AIN1R"),
	SND_SOC_DAPM_INPUT("AIN2L"),
	SND_SOC_DAPM_INPUT("AIN2R"),
	SND_SOC_DAPM_INPUT("AIN3L"),
	SND_SOC_DAPM_INPUT("AIN3R"),
	SND_SOC_DAPM_INPUT("AIN4L"),
	SND_SOC_DAPM_INPUT("AIN4R"),
	SND_SOC_DAPM_INPUT("MICA"),
	SND_SOC_DAPM_INPUT("MICB"),
	SND_SOC_DAPM_SIGGEN("Beep"),

	SND_SOC_DAPM_AIF_OUT("AIFOUTL", NULL,  0,
			SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIFOUTR", NULL,  0,
			SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_ADC("ADC Left", NULL, CS42L52_PWRCTL1, 1, 1),
	SND_SOC_DAPM_ADC("ADC Right", NULL, CS42L52_PWRCTL1, 2, 1),
	SND_SOC_DAPM_PGA("PGA Left", CS42L52_PWRCTL1, 3, 1, NULL, 0),
	SND_SOC_DAPM_PGA("PGA Right", CS42L52_PWRCTL1, 4, 1, NULL, 0),

	SND_SOC_DAPM_MUX("ADC Left Mux", SND_SOC_NOPM, 0, 0, &adca_mux),
	SND_SOC_DAPM_MUX("ADC Right Mux", SND_SOC_NOPM, 0, 0, &adcb_mux),

	SND_SOC_DAPM_MUX("ADC Left Swap", SND_SOC_NOPM,
			 0, 0, &adca_mixer),
	SND_SOC_DAPM_MUX("ADC Right Swap", SND_SOC_NOPM,
			 0, 0, &adcb_mixer),

	SND_SOC_DAPM_MUX("Output Mux", SND_SOC_NOPM,
			 0, 0, &digital_output_mux),

	SND_SOC_DAPM_PGA("PGA MICA", CS42L52_PWRCTL2, 1, 1, NULL, 0),
	SND_SOC_DAPM_PGA("PGA MICB", CS42L52_PWRCTL2, 2, 1, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Mic Bias", CS42L52_PWRCTL2, 0, 1, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Charge Pump", CS42L52_PWRCTL1, 7, 1, NULL, 0),

	SND_SOC_DAPM_AIF_IN("AIFINL", NULL,  0,
			SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIFINR", NULL,  0,
			SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_DAC("DAC Left", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC Right", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SWITCH("Bypass Left", CS42L52_MISC_CTL,
			    6, 0, &passthrul_ctl),
	SND_SOC_DAPM_SWITCH("Bypass Right", CS42L52_MISC_CTL,
			    7, 0, &passthrur_ctl),

	SND_SOC_DAPM_MUX("PCM Left Swap", SND_SOC_NOPM,
			 0, 0, &pcma_mixer),
	SND_SOC_DAPM_MUX("PCM Right Swap", SND_SOC_NOPM,
			 0, 0, &pcmb_mixer),

	SND_SOC_DAPM_SWITCH("HP Left Amp", SND_SOC_NOPM, 0, 0, &hpl_ctl),
	SND_SOC_DAPM_SWITCH("HP Right Amp", SND_SOC_NOPM, 0, 0, &hpr_ctl),

	SND_SOC_DAPM_SWITCH("SPK Left Amp", SND_SOC_NOPM, 0, 0, &spkl_ctl),
	SND_SOC_DAPM_SWITCH("SPK Right Amp", SND_SOC_NOPM, 0, 0, &spkr_ctl),

	SND_SOC_DAPM_OUTPUT("HPOUTA"),
	SND_SOC_DAPM_OUTPUT("HPOUTB"),
	SND_SOC_DAPM_OUTPUT("SPKOUTA"),
	SND_SOC_DAPM_OUTPUT("SPKOUTB"),

};

static const struct snd_soc_dapm_route cs42l52_audio_map[] = {

	{"Capture", NULL, "AIFOUTL"},
	{"Capture", NULL, "AIFOUTL"},

	{"AIFOUTL", NULL, "Output Mux"},
	{"AIFOUTR", NULL, "Output Mux"},

	{"Output Mux", "ADC", "ADC Left"},
	{"Output Mux", "ADC", "ADC Right"},

	{"ADC Left", NULL, "Charge Pump"},
	{"ADC Right", NULL, "Charge Pump"},

	{"Charge Pump", NULL, "ADC Left Mux"},
	{"Charge Pump", NULL, "ADC Right Mux"},

	{"ADC Left Mux", "Input1A", "AIN1L"},
	{"ADC Right Mux", "Input1B", "AIN1R"},
	{"ADC Left Mux", "Input2A", "AIN2L"},
	{"ADC Right Mux", "Input2B", "AIN2R"},
	{"ADC Left Mux", "Input3A", "AIN3L"},
	{"ADC Right Mux", "Input3B", "AIN3R"},
	{"ADC Left Mux", "Input4A", "AIN4L"},
	{"ADC Right Mux", "Input4B", "AIN4R"},
	{"ADC Left Mux", "PGA Input Left", "PGA Left"},
	{"ADC Right Mux", "PGA Input Right" , "PGA Right"},

	{"PGA Left", "Switch", "AIN1L"},
	{"PGA Right", "Switch", "AIN1R"},
	{"PGA Left", "Switch", "AIN2L"},
	{"PGA Right", "Switch", "AIN2R"},
	{"PGA Left", "Switch", "AIN3L"},
	{"PGA Right", "Switch", "AIN3R"},
	{"PGA Left", "Switch", "AIN4L"},
	{"PGA Right", "Switch", "AIN4R"},

	{"PGA Left", "Switch", "PGA MICA"},
	{"PGA MICA", NULL, "MICA"},

	{"PGA Right", "Switch", "PGA MICB"},
	{"PGA MICB", NULL, "MICB"},

	{"HPOUTA", NULL, "HP Left Amp"},
	{"HPOUTB", NULL, "HP Right Amp"},
	{"HP Left Amp", NULL, "Bypass Left"},
	{"HP Right Amp", NULL, "Bypass Right"},
	{"Bypass Left", "Switch", "PGA Left"},
	{"Bypass Right", "Switch", "PGA Right"},
	{"HP Left Amp", "Switch", "DAC Left"},
	{"HP Right Amp", "Switch", "DAC Right"},

	{"SPKOUTA", NULL, "SPK Left Amp"},
	{"SPKOUTB", NULL, "SPK Right Amp"},

	{"SPK Left Amp", NULL, "Beep"},
	{"SPK Right Amp", NULL, "Beep"},
	{"SPK Left Amp", "Switch", "Playback"},
	{"SPK Right Amp", "Switch", "Playback"},

	{"DAC Left", NULL, "Beep"},
	{"DAC Right", NULL, "Beep"},
	{"DAC Left", NULL, "Playback"},
	{"DAC Right", NULL, "Playback"},

	{"Output Mux", "DSP", "Playback"},
	{"Output Mux", "DSP", "Playback"},

	{"AIFINL", NULL, "Playback"},
	{"AIFINR", NULL, "Playback"},

};

struct cs42l52_clk_para {
	u32 mclk;
	u32 rate;
	u8 speed;
	u8 group;
	u8 videoclk;
	u8 ratio;
	u8 mclkdiv2;
};

static const struct cs42l52_clk_para clk_map_table[] = {
	/*8k*/
	{12288000, 8000, CLK_QS_MODE, CLK_32K, CLK_NO_27M, CLK_R_128, 0},
	{18432000, 8000, CLK_QS_MODE, CLK_32K, CLK_NO_27M, CLK_R_128, 0},
	{12000000, 8000, CLK_QS_MODE, CLK_32K, CLK_NO_27M, CLK_R_125, 0},
	{24000000, 8000, CLK_QS_MODE, CLK_32K, CLK_NO_27M, CLK_R_125, 1},
	{27000000, 8000, CLK_QS_MODE, CLK_32K, CLK_27M_MCLK, CLK_R_125, 0},

	/*11.025k*/
	{11289600, 11025, CLK_QS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{16934400, 11025, CLK_QS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},

	/*16k*/
	{12288000, 16000, CLK_HS_MODE, CLK_32K, CLK_NO_27M, CLK_R_128, 0},
	{18432000, 16000, CLK_HS_MODE, CLK_32K, CLK_NO_27M, CLK_R_128, 0},
	{12000000, 16000, CLK_HS_MODE, CLK_32K, CLK_NO_27M, CLK_R_125, 0},
	{24000000, 16000, CLK_HS_MODE, CLK_32K, CLK_NO_27M, CLK_R_125, 1},
	{27000000, 16000, CLK_HS_MODE, CLK_32K, CLK_27M_MCLK, CLK_R_125, 1},

	/*22.05k*/
	{11289600, 22050, CLK_HS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{16934400, 22050, CLK_HS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},

	/* 32k */
	{12288000, 32000, CLK_SS_MODE, CLK_32K, CLK_NO_27M, CLK_R_128, 0},
	{18432000, 32000, CLK_SS_MODE, CLK_32K, CLK_NO_27M, CLK_R_128, 0},
	{12000000, 32000, CLK_SS_MODE, CLK_32K, CLK_NO_27M, CLK_R_125, 0},
	{24000000, 32000, CLK_SS_MODE, CLK_32K, CLK_NO_27M, CLK_R_125, 1},
	{27000000, 32000, CLK_SS_MODE, CLK_32K, CLK_27M_MCLK, CLK_R_125, 0},

	/* 44.1k */
	{11289600, 44100, CLK_SS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{16934400, 44100, CLK_SS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},

	/* 48k */
	{12288000, 48000, CLK_SS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{18432000, 48000, CLK_SS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{12000000, 48000, CLK_SS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_125, 0},
	{24000000, 48000, CLK_SS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_125, 1},
	{27000000, 48000, CLK_SS_MODE, CLK_NO_32K, CLK_27M_MCLK, CLK_R_125, 1},

	/* 88.2k */
	{11289600, 88200, CLK_DS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{16934400, 88200, CLK_DS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},

	/* 96k */
	{12288000, 96000, CLK_DS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{18432000, 96000, CLK_DS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_128, 0},
	{12000000, 96000, CLK_DS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_125, 0},
	{24000000, 96000, CLK_DS_MODE, CLK_NO_32K, CLK_NO_27M, CLK_R_125, 1},
};

static int cs42l52_get_clk(int mclk, int rate)
{
	int i, ret = -EINVAL;
	u_int mclk1, mclk2 = 0;

	for (i = 0; i < ARRAY_SIZE(clk_map_table); i++) {
		if (clk_map_table[i].rate == rate) {
			mclk1 = clk_map_table[i].mclk;
			if (abs(mclk - mclk1) < abs(mclk - mclk2)) {
				mclk2 = mclk1;
				ret = i;
			}
		}
	}
	return ret;
}

static int cs42l52_set_sysclk(struct snd_soc_dai *codec_dai,
			int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);

	if ((freq >= CS42L52_MIN_CLK) && (freq <= CS42L52_MAX_CLK)) {
		cs42l52->sysclk = freq;
	} else {
		dev_err(codec->dev, "Invalid freq parameter\n");
		return -EINVAL;
	}
	return 0;
}

static int cs42l52_set_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);
	u8 iface = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = CS42L52_IFACE_CTL1_MASTER;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface = CS42L52_IFACE_CTL1_SLAVE;
		break;
	default:
		return -EINVAL;
	}

	 /* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= CS42L52_IFACE_CTL1_ADC_FMT_I2S |
				CS42L52_IFACE_CTL1_DAC_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface |= CS42L52_IFACE_CTL1_DAC_FMT_RIGHT_J;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= CS42L52_IFACE_CTL1_ADC_FMT_LEFT_J |
				CS42L52_IFACE_CTL1_DAC_FMT_LEFT_J;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= CS42L52_IFACE_CTL1_DSP_MODE_EN;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= CS42L52_IFACE_CTL1_INV_SCLK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= CS42L52_IFACE_CTL1_INV_SCLK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	default:
		return -EINVAL;
	}
	cs42l52->config.format = iface;
	snd_soc_write(codec, CS42L52_IFACE_CTL1, cs42l52->config.format);

	return 0;
}

static int cs42l52_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	if (mute)
		snd_soc_update_bits(codec, CS42L52_PB_CTL1,
				    CS42L52_PB_CTL1_MUTE_MASK,
				CS42L52_PB_CTL1_MUTE);
	else
		snd_soc_update_bits(codec, CS42L52_PB_CTL1,
				    CS42L52_PB_CTL1_MUTE_MASK,
				CS42L52_PB_CTL1_UNMUTE);

	return 0;
}

static int cs42l52_pcm_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *params,
				     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);
	u32 clk = 0;
	int index;

	index = cs42l52_get_clk(cs42l52->sysclk, params_rate(params));
	if (index >= 0) {
		cs42l52->sysclk = clk_map_table[index].mclk;

		clk |= (clk_map_table[index].speed << CLK_SPEED_SHIFT) |
		(clk_map_table[index].group << CLK_32K_SR_SHIFT) |
		(clk_map_table[index].videoclk << CLK_27M_MCLK_SHIFT) |
		(clk_map_table[index].ratio << CLK_RATIO_SHIFT) |
		clk_map_table[index].mclkdiv2;

		snd_soc_write(codec, CS42L52_CLK_CTL, clk);
	} else {
		dev_err(codec->dev, "can't get correct mclk\n");
		return -EINVAL;
	}

	return 0;
}

static int cs42l52_set_bias_level(struct snd_soc_codec *codec,
					enum snd_soc_bias_level level)
{
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		snd_soc_update_bits(codec, CS42L52_PWRCTL1,
				    CS42L52_PWRCTL1_PDN_CODEC, 0);
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			regcache_cache_only(cs42l52->regmap, false);
			regcache_sync(cs42l52->regmap);
		}
		snd_soc_write(codec, CS42L52_PWRCTL1, CS42L52_PWRCTL1_PDN_ALL);
		break;
	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, CS42L52_PWRCTL1, CS42L52_PWRCTL1_PDN_ALL);
		regcache_cache_only(cs42l52->regmap, true);
		break;
	}

	return 0;
}

#define CS42L52_RATES (SNDRV_PCM_RATE_8000_96000)

#define CS42L52_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE | \
			SNDRV_PCM_FMTBIT_S18_3LE | SNDRV_PCM_FMTBIT_U18_3LE | \
			SNDRV_PCM_FMTBIT_S20_3LE | SNDRV_PCM_FMTBIT_U20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_U24_LE)

static const struct snd_soc_dai_ops cs42l52_ops = {
	.hw_params	= cs42l52_pcm_hw_params,
	.digital_mute	= cs42l52_digital_mute,
	.set_fmt	= cs42l52_set_fmt,
	.set_sysclk	= cs42l52_set_sysclk,
};

static struct snd_soc_dai_driver cs42l52_dai = {
		.name = "cs42l52",
		.playback = {
			.stream_name = "Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS42L52_RATES,
			.formats = CS42L52_FORMATS,
		},
		.capture = {
			.stream_name = "Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = CS42L52_RATES,
			.formats = CS42L52_FORMATS,
		},
		.ops = &cs42l52_ops,
};

static int beep_rates[] = {
	261, 522, 585, 667, 706, 774, 889, 1000,
	1043, 1200, 1333, 1412, 1600, 1714, 2000, 2182
};

static void cs42l52_beep_work(struct work_struct *work)
{
	struct cs42l52_private *cs42l52 =
		container_of(work, struct cs42l52_private, beep_work);
	struct snd_soc_codec *codec = cs42l52->codec;
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	int i;
	int val = 0;
	int best = 0;

	if (cs42l52->beep_rate) {
		for (i = 0; i < ARRAY_SIZE(beep_rates); i++) {
			if (abs(cs42l52->beep_rate - beep_rates[i]) <
			    abs(cs42l52->beep_rate - beep_rates[best]))
				best = i;
		}

		dev_dbg(codec->dev, "Set beep rate %dHz for requested %dHz\n",
			beep_rates[best], cs42l52->beep_rate);

		val = (best << CS42L52_BEEP_RATE_SHIFT);

		snd_soc_dapm_enable_pin(dapm, "Beep");
	} else {
		dev_dbg(codec->dev, "Disabling beep\n");
		snd_soc_dapm_disable_pin(dapm, "Beep");
	}

	snd_soc_update_bits(codec, CS42L52_BEEP_FREQ,
			    CS42L52_BEEP_RATE_MASK, val);

	snd_soc_dapm_sync(dapm);
}

/* For usability define a way of injecting beep events for the device -
 * many systems will not have a keyboard.
 */
static int cs42l52_beep_event(struct input_dev *dev, unsigned int type,
			     unsigned int code, int hz)
{
	struct snd_soc_codec *codec = input_get_drvdata(dev);
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "Beep event %x %x\n", code, hz);

	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 261;
	case SND_TONE:
		break;
	default:
		return -1;
	}

	/* Kick the beep from a workqueue */
	cs42l52->beep_rate = hz;
	schedule_work(&cs42l52->beep_work);
	return 0;
}

static ssize_t cs42l52_beep_set(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct cs42l52_private *cs42l52 = dev_get_drvdata(dev);
	long int time;
	int ret;

	ret = kstrtol(buf, 10, &time);
	if (ret != 0)
		return ret;

	input_event(cs42l52->beep, EV_SND, SND_TONE, time);

	return count;
}

static DEVICE_ATTR(beep, 0200, NULL, cs42l52_beep_set);

static void cs42l52_init_beep(struct snd_soc_codec *codec)
{
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);
	int ret;

	cs42l52->beep = devm_input_allocate_device(codec->dev);
	if (!cs42l52->beep) {
		dev_err(codec->dev, "Failed to allocate beep device\n");
		return;
	}

	INIT_WORK(&cs42l52->beep_work, cs42l52_beep_work);
	cs42l52->beep_rate = 0;

	cs42l52->beep->name = "CS42L52 Beep Generator";
	cs42l52->beep->phys = dev_name(codec->dev);
	cs42l52->beep->id.bustype = BUS_I2C;

	cs42l52->beep->evbit[0] = BIT_MASK(EV_SND);
	cs42l52->beep->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	cs42l52->beep->event = cs42l52_beep_event;
	cs42l52->beep->dev.parent = codec->dev;
	input_set_drvdata(cs42l52->beep, codec);

	ret = input_register_device(cs42l52->beep);
	if (ret != 0) {
		cs42l52->beep = NULL;
		dev_err(codec->dev, "Failed to register beep device\n");
	}

	ret = device_create_file(codec->dev, &dev_attr_beep);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to create keyclick file: %d\n",
			ret);
	}
}

static void cs42l52_free_beep(struct snd_soc_codec *codec)
{
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);

	device_remove_file(codec->dev, &dev_attr_beep);
	cancel_work_sync(&cs42l52->beep_work);
	cs42l52->beep = NULL;

	snd_soc_update_bits(codec, CS42L52_BEEP_TONE_CTL,
			    CS42L52_BEEP_EN_MASK, 0);
}

static int cs42l52_probe(struct snd_soc_codec *codec)
{
	struct cs42l52_private *cs42l52 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(cs42l52->regmap, true);

	cs42l52_add_mic_controls(codec);

	cs42l52_init_beep(codec);

	cs42l52->sysclk = CS42L52_DEFAULT_CLK;
	cs42l52->config.format = CS42L52_DEFAULT_FORMAT;

	return 0;
}

static int cs42l52_remove(struct snd_soc_codec *codec)
{
	cs42l52_free_beep(codec);

	return 0;
}

static const struct snd_soc_codec_driver soc_codec_dev_cs42l52 = {
	.probe = cs42l52_probe,
	.remove = cs42l52_remove,
	.set_bias_level = cs42l52_set_bias_level,
	.suspend_bias_off = true,

	.component_driver = {
		.controls		= cs42l52_snd_controls,
		.num_controls		= ARRAY_SIZE(cs42l52_snd_controls),
		.dapm_widgets		= cs42l52_dapm_widgets,
		.num_dapm_widgets	= ARRAY_SIZE(cs42l52_dapm_widgets),
		.dapm_routes		= cs42l52_audio_map,
		.num_dapm_routes	= ARRAY_SIZE(cs42l52_audio_map),
	},
};

/* Current and threshold powerup sequence Pg37 */
static const struct reg_sequence cs42l52_threshold_patch[] = {

	{ 0x00, 0x99 },
	{ 0x3E, 0xBA },
	{ 0x47, 0x80 },
	{ 0x32, 0xBB },
	{ 0x32, 0x3B },
	{ 0x00, 0x00 },

};

static const struct regmap_config cs42l52_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CS42L52_MAX_REGISTER,
	.reg_defaults = cs42l52_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(cs42l52_reg_defaults),
	.readable_reg = cs42l52_readable_register,
	.volatile_reg = cs42l52_volatile_register,
	.cache_type = REGCACHE_RBTREE,
};

static int cs42l52_i2c_probe(struct i2c_client *i2c_client,
			     const struct i2c_device_id *id)
{
	struct cs42l52_private *cs42l52;
	struct cs42l52_platform_data *pdata = dev_get_platdata(&i2c_client->dev);
	int ret;
	unsigned int devid = 0;
	unsigned int reg;
	u32 val32;

	cs42l52 = devm_kzalloc(&i2c_client->dev, sizeof(struct cs42l52_private),
			       GFP_KERNEL);
	if (cs42l52 == NULL)
		return -ENOMEM;
	cs42l52->dev = &i2c_client->dev;

	cs42l52->regmap = devm_regmap_init_i2c(i2c_client, &cs42l52_regmap);
	if (IS_ERR(cs42l52->regmap)) {
		ret = PTR_ERR(cs42l52->regmap);
		dev_err(&i2c_client->dev, "regmap_init() failed: %d\n", ret);
		return ret;
	}
	if (pdata) {
		cs42l52->pdata = *pdata;
	} else {
		pdata = devm_kzalloc(&i2c_client->dev,
				     sizeof(struct cs42l52_platform_data),
				GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		if (i2c_client->dev.of_node) {
			if (of_property_read_bool(i2c_client->dev.of_node,
				"cirrus,mica-differential-cfg"))
				pdata->mica_diff_cfg = true;

			if (of_property_read_bool(i2c_client->dev.of_node,
				"cirrus,micb-differential-cfg"))
				pdata->micb_diff_cfg = true;

			if (of_property_read_u32(i2c_client->dev.of_node,
				"cirrus,micbias-lvl", &val32) >= 0)
				pdata->micbias_lvl = val32;

			if (of_property_read_u32(i2c_client->dev.of_node,
				"cirrus,chgfreq-divisor", &val32) >= 0)
				pdata->chgfreq = val32;

			pdata->reset_gpio =
				of_get_named_gpio(i2c_client->dev.of_node,
						"cirrus,reset-gpio", 0);
		}
		cs42l52->pdata = *pdata;
	}

	if (cs42l52->pdata.reset_gpio) {
		ret = devm_gpio_request_one(&i2c_client->dev,
					    cs42l52->pdata.reset_gpio,
					    GPIOF_OUT_INIT_HIGH,
					    "CS42L52 /RST");
		if (ret < 0) {
			dev_err(&i2c_client->dev, "Failed to request /RST %d: %d\n",
				cs42l52->pdata.reset_gpio, ret);
			return ret;
		}
		gpio_set_value_cansleep(cs42l52->pdata.reset_gpio, 0);
		gpio_set_value_cansleep(cs42l52->pdata.reset_gpio, 1);
	}

	i2c_set_clientdata(i2c_client, cs42l52);

	ret = regmap_register_patch(cs42l52->regmap, cs42l52_threshold_patch,
				    ARRAY_SIZE(cs42l52_threshold_patch));
	if (ret != 0)
		dev_warn(cs42l52->dev, "Failed to apply regmap patch: %d\n",
			 ret);

	ret = regmap_read(cs42l52->regmap, CS42L52_CHIP, &reg);
	devid = reg & CS42L52_CHIP_ID_MASK;
	if (devid != CS42L52_CHIP_ID) {
		ret = -ENODEV;
		dev_err(&i2c_client->dev,
			"CS42L52 Device ID (%X). Expected %X\n",
			devid, CS42L52_CHIP_ID);
		return ret;
	}

	dev_info(&i2c_client->dev, "Cirrus Logic CS42L52, Revision: %02X\n",
		 reg & CS42L52_CHIP_REV_MASK);

	/* Set Platform Data */
	if (cs42l52->pdata.mica_diff_cfg)
		regmap_update_bits(cs42l52->regmap, CS42L52_MICA_CTL,
				   CS42L52_MIC_CTL_TYPE_MASK,
				cs42l52->pdata.mica_diff_cfg <<
				CS42L52_MIC_CTL_TYPE_SHIFT);

	if (cs42l52->pdata.micb_diff_cfg)
		regmap_update_bits(cs42l52->regmap, CS42L52_MICB_CTL,
				   CS42L52_MIC_CTL_TYPE_MASK,
				cs42l52->pdata.micb_diff_cfg <<
				CS42L52_MIC_CTL_TYPE_SHIFT);

	if (cs42l52->pdata.chgfreq)
		regmap_update_bits(cs42l52->regmap, CS42L52_CHARGE_PUMP,
				   CS42L52_CHARGE_PUMP_MASK,
				cs42l52->pdata.chgfreq <<
				CS42L52_CHARGE_PUMP_SHIFT);

	if (cs42l52->pdata.micbias_lvl)
		regmap_update_bits(cs42l52->regmap, CS42L52_IFACE_CTL2,
				   CS42L52_IFACE_CTL2_BIAS_LVL,
				cs42l52->pdata.micbias_lvl);

	ret =  snd_soc_register_codec(&i2c_client->dev,
			&soc_codec_dev_cs42l52, &cs42l52_dai, 1);
	if (ret < 0)
		return ret;
	return 0;
}

static int cs42l52_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct of_device_id cs42l52_of_match[] = {
	{ .compatible = "cirrus,cs42l52", },
	{},
};
MODULE_DEVICE_TABLE(of, cs42l52_of_match);


static const struct i2c_device_id cs42l52_id[] = {
	{ "cs42l52", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cs42l52_id);

static struct i2c_driver cs42l52_i2c_driver = {
	.driver = {
		.name = "cs42l52",
		.of_match_table = cs42l52_of_match,
	},
	.id_table = cs42l52_id,
	.probe =    cs42l52_i2c_probe,
	.remove =   cs42l52_i2c_remove,
};

module_i2c_driver(cs42l52_i2c_driver);

MODULE_DESCRIPTION("ASoC CS42L52 driver");
MODULE_AUTHOR("Georgi Vlaev, Nucleus Systems Ltd, <joe@nucleusys.com>");
MODULE_AUTHOR("Brian Austin, Cirrus Logic Inc, <brian.austin@cirrus.com>");
MODULE_LICENSE("GPL");
