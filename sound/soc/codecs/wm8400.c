/*
 * wm8400.c  --  WM8400 ALSA Soc Audio driver
 *
 * Copyright 2008-11 Wolfson Microelectronics PLC.
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/wm8400-audio.h>
#include <linux/mfd/wm8400-private.h>
#include <linux/mfd/core.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "wm8400.h"

static struct regulator_bulk_data power[] = {
	{
		.supply = "I2S1VDD",
	},
	{
		.supply = "I2S2VDD",
	},
	{
		.supply = "DCVDD",
	},
	{
		.supply = "AVDD",
	},
	{
		.supply = "FLLVDD",
	},
	{
		.supply = "HPVDD",
	},
	{
		.supply = "SPKVDD",
	},
};

/* codec private data */
struct wm8400_priv {
	struct snd_soc_codec *codec;
	struct wm8400 *wm8400;
	u16 fake_register;
	unsigned int sysclk;
	unsigned int pcmclk;
	struct work_struct work;
	int fll_in, fll_out;
};

static void wm8400_codec_reset(struct snd_soc_codec *codec)
{
	struct wm8400_priv *wm8400 = snd_soc_codec_get_drvdata(codec);

	wm8400_reset_codec_reg_cache(wm8400->wm8400);
}

static const DECLARE_TLV_DB_SCALE(rec_mix_tlv, -1500, 600, 0);

static const DECLARE_TLV_DB_SCALE(in_pga_tlv, -1650, 3000, 0);

static const DECLARE_TLV_DB_SCALE(out_mix_tlv, -2100, 0, 0);

static const DECLARE_TLV_DB_SCALE(out_pga_tlv, -7300, 600, 0);

static const DECLARE_TLV_DB_SCALE(out_omix_tlv, -600, 0, 0);

static const DECLARE_TLV_DB_SCALE(out_dac_tlv, -7163, 0, 0);

static const DECLARE_TLV_DB_SCALE(in_adc_tlv, -7163, 1763, 0);

static const DECLARE_TLV_DB_SCALE(out_sidetone_tlv, -3600, 0, 0);

static int wm8400_outpga_put_volsw_vu(struct snd_kcontrol *kcontrol,
        struct snd_ctl_elem_value *ucontrol)
{
        struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
        int ret;
        u16 val;

        ret = snd_soc_put_volsw(kcontrol, ucontrol);
        if (ret < 0)
                return ret;

        /* now hit the volume update bits (always bit 8) */
        val = snd_soc_read(codec, reg);
        return snd_soc_write(codec, reg, val | 0x0100);
}

#define WM8400_OUTPGA_SINGLE_R_TLV(xname, reg, shift, max, invert, tlv_array) \
	SOC_SINGLE_EXT_TLV(xname, reg, shift, max, invert, \
		snd_soc_get_volsw, wm8400_outpga_put_volsw_vu, tlv_array)


static const char *wm8400_digital_sidetone[] =
	{"None", "Left ADC", "Right ADC", "Reserved"};

static const struct soc_enum wm8400_left_digital_sidetone_enum =
SOC_ENUM_SINGLE(WM8400_DIGITAL_SIDE_TONE,
		WM8400_ADC_TO_DACL_SHIFT, 2, wm8400_digital_sidetone);

static const struct soc_enum wm8400_right_digital_sidetone_enum =
SOC_ENUM_SINGLE(WM8400_DIGITAL_SIDE_TONE,
		WM8400_ADC_TO_DACR_SHIFT, 2, wm8400_digital_sidetone);

static const char *wm8400_adcmode[] =
	{"Hi-fi mode", "Voice mode 1", "Voice mode 2", "Voice mode 3"};

static const struct soc_enum wm8400_right_adcmode_enum =
SOC_ENUM_SINGLE(WM8400_ADC_CTRL, WM8400_ADC_HPF_CUT_SHIFT, 3, wm8400_adcmode);

static const struct snd_kcontrol_new wm8400_snd_controls[] = {
/* INMIXL */
SOC_SINGLE("LIN12 PGA Boost", WM8400_INPUT_MIXER3, WM8400_L12MNBST_SHIFT,
	   1, 0),
SOC_SINGLE("LIN34 PGA Boost", WM8400_INPUT_MIXER3, WM8400_L34MNBST_SHIFT,
	   1, 0),
/* INMIXR */
SOC_SINGLE("RIN12 PGA Boost", WM8400_INPUT_MIXER3, WM8400_R12MNBST_SHIFT,
	   1, 0),
SOC_SINGLE("RIN34 PGA Boost", WM8400_INPUT_MIXER3, WM8400_R34MNBST_SHIFT,
	   1, 0),

/* LOMIX */
SOC_SINGLE_TLV("LOMIX LIN3 Bypass Volume", WM8400_OUTPUT_MIXER3,
	WM8400_LLI3LOVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX RIN12 PGA Bypass Volume", WM8400_OUTPUT_MIXER3,
	WM8400_LR12LOVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX LIN12 PGA Bypass Volume", WM8400_OUTPUT_MIXER3,
	WM8400_LL12LOVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX RIN3 Bypass Volume", WM8400_OUTPUT_MIXER5,
	WM8400_LRI3LOVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX AINRMUX Bypass Volume", WM8400_OUTPUT_MIXER5,
	WM8400_LRBLOVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX AINLMUX Bypass Volume", WM8400_OUTPUT_MIXER5,
	WM8400_LRBLOVOL_SHIFT, 7, 0, out_mix_tlv),

/* ROMIX */
SOC_SINGLE_TLV("ROMIX RIN3 Bypass Volume", WM8400_OUTPUT_MIXER4,
	WM8400_RRI3ROVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX LIN12 PGA Bypass Volume", WM8400_OUTPUT_MIXER4,
	WM8400_RL12ROVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX RIN12 PGA Bypass Volume", WM8400_OUTPUT_MIXER4,
	WM8400_RR12ROVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX LIN3 Bypass Volume", WM8400_OUTPUT_MIXER6,
	WM8400_RLI3ROVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX AINLMUX Bypass Volume", WM8400_OUTPUT_MIXER6,
	WM8400_RLBROVOL_SHIFT, 7, 0, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX AINRMUX Bypass Volume", WM8400_OUTPUT_MIXER6,
	WM8400_RRBROVOL_SHIFT, 7, 0, out_mix_tlv),

/* LOUT */
WM8400_OUTPGA_SINGLE_R_TLV("LOUT Volume", WM8400_LEFT_OUTPUT_VOLUME,
	WM8400_LOUTVOL_SHIFT, WM8400_LOUTVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("LOUT ZC", WM8400_LEFT_OUTPUT_VOLUME, WM8400_LOZC_SHIFT, 1, 0),

/* ROUT */
WM8400_OUTPGA_SINGLE_R_TLV("ROUT Volume", WM8400_RIGHT_OUTPUT_VOLUME,
	WM8400_ROUTVOL_SHIFT, WM8400_ROUTVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("ROUT ZC", WM8400_RIGHT_OUTPUT_VOLUME, WM8400_ROZC_SHIFT, 1, 0),

/* LOPGA */
WM8400_OUTPGA_SINGLE_R_TLV("LOPGA Volume", WM8400_LEFT_OPGA_VOLUME,
	WM8400_LOPGAVOL_SHIFT, WM8400_LOPGAVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("LOPGA ZC Switch", WM8400_LEFT_OPGA_VOLUME,
	WM8400_LOPGAZC_SHIFT, 1, 0),

/* ROPGA */
WM8400_OUTPGA_SINGLE_R_TLV("ROPGA Volume", WM8400_RIGHT_OPGA_VOLUME,
	WM8400_ROPGAVOL_SHIFT, WM8400_ROPGAVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("ROPGA ZC Switch", WM8400_RIGHT_OPGA_VOLUME,
	WM8400_ROPGAZC_SHIFT, 1, 0),

SOC_SINGLE("LON Mute Switch", WM8400_LINE_OUTPUTS_VOLUME,
	WM8400_LONMUTE_SHIFT, 1, 0),
SOC_SINGLE("LOP Mute Switch", WM8400_LINE_OUTPUTS_VOLUME,
	WM8400_LOPMUTE_SHIFT, 1, 0),
SOC_SINGLE("LOP Attenuation Switch", WM8400_LINE_OUTPUTS_VOLUME,
	WM8400_LOATTN_SHIFT, 1, 0),
SOC_SINGLE("RON Mute Switch", WM8400_LINE_OUTPUTS_VOLUME,
	WM8400_RONMUTE_SHIFT, 1, 0),
SOC_SINGLE("ROP Mute Switch", WM8400_LINE_OUTPUTS_VOLUME,
	WM8400_ROPMUTE_SHIFT, 1, 0),
SOC_SINGLE("ROP Attenuation Switch", WM8400_LINE_OUTPUTS_VOLUME,
	WM8400_ROATTN_SHIFT, 1, 0),

SOC_SINGLE("OUT3 Mute Switch", WM8400_OUT3_4_VOLUME,
	WM8400_OUT3MUTE_SHIFT, 1, 0),
SOC_SINGLE("OUT3 Attenuation Switch", WM8400_OUT3_4_VOLUME,
	WM8400_OUT3ATTN_SHIFT, 1, 0),

SOC_SINGLE("OUT4 Mute Switch", WM8400_OUT3_4_VOLUME,
	WM8400_OUT4MUTE_SHIFT, 1, 0),
SOC_SINGLE("OUT4 Attenuation Switch", WM8400_OUT3_4_VOLUME,
	WM8400_OUT4ATTN_SHIFT, 1, 0),

SOC_SINGLE("Speaker Mode Switch", WM8400_CLASSD1,
	WM8400_CDMODE_SHIFT, 1, 0),

SOC_SINGLE("Speaker Output Attenuation Volume", WM8400_SPEAKER_VOLUME,
	WM8400_SPKATTN_SHIFT, WM8400_SPKATTN_MASK, 0),
SOC_SINGLE("Speaker DC Boost Volume", WM8400_CLASSD3,
	WM8400_DCGAIN_SHIFT, 6, 0),
SOC_SINGLE("Speaker AC Boost Volume", WM8400_CLASSD3,
	WM8400_ACGAIN_SHIFT, 6, 0),

WM8400_OUTPGA_SINGLE_R_TLV("Left DAC Digital Volume",
	WM8400_LEFT_DAC_DIGITAL_VOLUME, WM8400_DACL_VOL_SHIFT,
	127, 0, out_dac_tlv),

WM8400_OUTPGA_SINGLE_R_TLV("Right DAC Digital Volume",
	WM8400_RIGHT_DAC_DIGITAL_VOLUME, WM8400_DACR_VOL_SHIFT,
	127, 0, out_dac_tlv),

SOC_ENUM("Left Digital Sidetone", wm8400_left_digital_sidetone_enum),
SOC_ENUM("Right Digital Sidetone", wm8400_right_digital_sidetone_enum),

SOC_SINGLE_TLV("Left Digital Sidetone Volume", WM8400_DIGITAL_SIDE_TONE,
	WM8400_ADCL_DAC_SVOL_SHIFT, 15, 0, out_sidetone_tlv),
SOC_SINGLE_TLV("Right Digital Sidetone Volume", WM8400_DIGITAL_SIDE_TONE,
	WM8400_ADCR_DAC_SVOL_SHIFT, 15, 0, out_sidetone_tlv),

SOC_SINGLE("ADC Digital High Pass Filter Switch", WM8400_ADC_CTRL,
	WM8400_ADC_HPF_ENA_SHIFT, 1, 0),

SOC_ENUM("ADC HPF Mode", wm8400_right_adcmode_enum),

WM8400_OUTPGA_SINGLE_R_TLV("Left ADC Digital Volume",
	WM8400_LEFT_ADC_DIGITAL_VOLUME,
	WM8400_ADCL_VOL_SHIFT,
	WM8400_ADCL_VOL_MASK,
	0,
	in_adc_tlv),

WM8400_OUTPGA_SINGLE_R_TLV("Right ADC Digital Volume",
	WM8400_RIGHT_ADC_DIGITAL_VOLUME,
	WM8400_ADCR_VOL_SHIFT,
	WM8400_ADCR_VOL_MASK,
	0,
	in_adc_tlv),

WM8400_OUTPGA_SINGLE_R_TLV("LIN12 Volume",
	WM8400_LEFT_LINE_INPUT_1_2_VOLUME,
	WM8400_LIN12VOL_SHIFT,
	WM8400_LIN12VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("LIN12 ZC Switch", WM8400_LEFT_LINE_INPUT_1_2_VOLUME,
	WM8400_LI12ZC_SHIFT, 1, 0),

SOC_SINGLE("LIN12 Mute Switch", WM8400_LEFT_LINE_INPUT_1_2_VOLUME,
	WM8400_LI12MUTE_SHIFT, 1, 0),

WM8400_OUTPGA_SINGLE_R_TLV("LIN34 Volume",
	WM8400_LEFT_LINE_INPUT_3_4_VOLUME,
	WM8400_LIN34VOL_SHIFT,
	WM8400_LIN34VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("LIN34 ZC Switch", WM8400_LEFT_LINE_INPUT_3_4_VOLUME,
	WM8400_LI34ZC_SHIFT, 1, 0),

SOC_SINGLE("LIN34 Mute Switch", WM8400_LEFT_LINE_INPUT_3_4_VOLUME,
	WM8400_LI34MUTE_SHIFT, 1, 0),

WM8400_OUTPGA_SINGLE_R_TLV("RIN12 Volume",
	WM8400_RIGHT_LINE_INPUT_1_2_VOLUME,
	WM8400_RIN12VOL_SHIFT,
	WM8400_RIN12VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("RIN12 ZC Switch", WM8400_RIGHT_LINE_INPUT_1_2_VOLUME,
	WM8400_RI12ZC_SHIFT, 1, 0),

SOC_SINGLE("RIN12 Mute Switch", WM8400_RIGHT_LINE_INPUT_1_2_VOLUME,
	WM8400_RI12MUTE_SHIFT, 1, 0),

WM8400_OUTPGA_SINGLE_R_TLV("RIN34 Volume",
	WM8400_RIGHT_LINE_INPUT_3_4_VOLUME,
	WM8400_RIN34VOL_SHIFT,
	WM8400_RIN34VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("RIN34 ZC Switch", WM8400_RIGHT_LINE_INPUT_3_4_VOLUME,
	WM8400_RI34ZC_SHIFT, 1, 0),

SOC_SINGLE("RIN34 Mute Switch", WM8400_RIGHT_LINE_INPUT_3_4_VOLUME,
	WM8400_RI34MUTE_SHIFT, 1, 0),

};

/*
 * _DAPM_ Controls
 */

static int outmixer_event (struct snd_soc_dapm_widget *w,
	struct snd_kcontrol * kcontrol, int event)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	u32 reg_shift = mc->shift;
	int ret = 0;
	u16 reg;

	switch (reg_shift) {
	case WM8400_SPEAKER_MIXER | (WM8400_LDSPK << 8) :
		reg = snd_soc_read(w->codec, WM8400_OUTPUT_MIXER1);
		if (reg & WM8400_LDLO) {
			printk(KERN_WARNING
			"Cannot set as Output Mixer 1 LDLO Set\n");
			ret = -1;
		}
		break;
	case WM8400_SPEAKER_MIXER | (WM8400_RDSPK << 8):
		reg = snd_soc_read(w->codec, WM8400_OUTPUT_MIXER2);
		if (reg & WM8400_RDRO) {
			printk(KERN_WARNING
			"Cannot set as Output Mixer 2 RDRO Set\n");
			ret = -1;
		}
		break;
	case WM8400_OUTPUT_MIXER1 | (WM8400_LDLO << 8):
		reg = snd_soc_read(w->codec, WM8400_SPEAKER_MIXER);
		if (reg & WM8400_LDSPK) {
			printk(KERN_WARNING
			"Cannot set as Speaker Mixer LDSPK Set\n");
			ret = -1;
		}
		break;
	case WM8400_OUTPUT_MIXER2 | (WM8400_RDRO << 8):
		reg = snd_soc_read(w->codec, WM8400_SPEAKER_MIXER);
		if (reg & WM8400_RDSPK) {
			printk(KERN_WARNING
			"Cannot set as Speaker Mixer RDSPK Set\n");
			ret = -1;
		}
		break;
	}

	return ret;
}

/* INMIX dB values */
static const unsigned int in_mix_tlv[] = {
	TLV_DB_RANGE_HEAD(1),
	0,7, TLV_DB_SCALE_ITEM(-1200, 600, 0),
};

/* Left In PGA Connections */
static const struct snd_kcontrol_new wm8400_dapm_lin12_pga_controls[] = {
SOC_DAPM_SINGLE("LIN1 Switch", WM8400_INPUT_MIXER2, WM8400_LMN1_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LIN2 Switch", WM8400_INPUT_MIXER2, WM8400_LMP2_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new wm8400_dapm_lin34_pga_controls[] = {
SOC_DAPM_SINGLE("LIN3 Switch", WM8400_INPUT_MIXER2, WM8400_LMN3_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LIN4 Switch", WM8400_INPUT_MIXER2, WM8400_LMP4_SHIFT, 1, 0),
};

/* Right In PGA Connections */
static const struct snd_kcontrol_new wm8400_dapm_rin12_pga_controls[] = {
SOC_DAPM_SINGLE("RIN1 Switch", WM8400_INPUT_MIXER2, WM8400_RMN1_SHIFT, 1, 0),
SOC_DAPM_SINGLE("RIN2 Switch", WM8400_INPUT_MIXER2, WM8400_RMP2_SHIFT, 1, 0),
};

static const struct snd_kcontrol_new wm8400_dapm_rin34_pga_controls[] = {
SOC_DAPM_SINGLE("RIN3 Switch", WM8400_INPUT_MIXER2, WM8400_RMN3_SHIFT, 1, 0),
SOC_DAPM_SINGLE("RIN4 Switch", WM8400_INPUT_MIXER2, WM8400_RMP4_SHIFT, 1, 0),
};

/* INMIXL */
static const struct snd_kcontrol_new wm8400_dapm_inmixl_controls[] = {
SOC_DAPM_SINGLE_TLV("Record Left Volume", WM8400_INPUT_MIXER3,
	WM8400_LDBVOL_SHIFT, WM8400_LDBVOL_MASK, 0, in_mix_tlv),
SOC_DAPM_SINGLE_TLV("LIN2 Volume", WM8400_INPUT_MIXER5, WM8400_LI2BVOL_SHIFT,
	7, 0, in_mix_tlv),
SOC_DAPM_SINGLE("LINPGA12 Switch", WM8400_INPUT_MIXER3, WM8400_L12MNB_SHIFT,
		1, 0),
SOC_DAPM_SINGLE("LINPGA34 Switch", WM8400_INPUT_MIXER3, WM8400_L34MNB_SHIFT,
		1, 0),
};

/* INMIXR */
static const struct snd_kcontrol_new wm8400_dapm_inmixr_controls[] = {
SOC_DAPM_SINGLE_TLV("Record Right Volume", WM8400_INPUT_MIXER4,
	WM8400_RDBVOL_SHIFT, WM8400_RDBVOL_MASK, 0, in_mix_tlv),
SOC_DAPM_SINGLE_TLV("RIN2 Volume", WM8400_INPUT_MIXER6, WM8400_RI2BVOL_SHIFT,
	7, 0, in_mix_tlv),
SOC_DAPM_SINGLE("RINPGA12 Switch", WM8400_INPUT_MIXER3, WM8400_L12MNB_SHIFT,
	1, 0),
SOC_DAPM_SINGLE("RINPGA34 Switch", WM8400_INPUT_MIXER3, WM8400_L34MNB_SHIFT,
	1, 0),
};

/* AINLMUX */
static const char *wm8400_ainlmux[] =
	{"INMIXL Mix", "RXVOICE Mix", "DIFFINL Mix"};

static const struct soc_enum wm8400_ainlmux_enum =
SOC_ENUM_SINGLE( WM8400_INPUT_MIXER1, WM8400_AINLMODE_SHIFT,
	ARRAY_SIZE(wm8400_ainlmux), wm8400_ainlmux);

static const struct snd_kcontrol_new wm8400_dapm_ainlmux_controls =
SOC_DAPM_ENUM("Route", wm8400_ainlmux_enum);

/* DIFFINL */

/* AINRMUX */
static const char *wm8400_ainrmux[] =
	{"INMIXR Mix", "RXVOICE Mix", "DIFFINR Mix"};

static const struct soc_enum wm8400_ainrmux_enum =
SOC_ENUM_SINGLE( WM8400_INPUT_MIXER1, WM8400_AINRMODE_SHIFT,
	ARRAY_SIZE(wm8400_ainrmux), wm8400_ainrmux);

static const struct snd_kcontrol_new wm8400_dapm_ainrmux_controls =
SOC_DAPM_ENUM("Route", wm8400_ainrmux_enum);

/* RXVOICE */
static const struct snd_kcontrol_new wm8400_dapm_rxvoice_controls[] = {
SOC_DAPM_SINGLE_TLV("LIN4/RXN", WM8400_INPUT_MIXER5, WM8400_LR4BVOL_SHIFT,
			WM8400_LR4BVOL_MASK, 0, in_mix_tlv),
SOC_DAPM_SINGLE_TLV("RIN4/RXP", WM8400_INPUT_MIXER6, WM8400_RL4BVOL_SHIFT,
			WM8400_RL4BVOL_MASK, 0, in_mix_tlv),
};

/* LOMIX */
static const struct snd_kcontrol_new wm8400_dapm_lomix_controls[] = {
SOC_DAPM_SINGLE("LOMIX Right ADC Bypass Switch", WM8400_OUTPUT_MIXER1,
	WM8400_LRBLO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOMIX Left ADC Bypass Switch", WM8400_OUTPUT_MIXER1,
	WM8400_LLBLO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOMIX RIN3 Bypass Switch", WM8400_OUTPUT_MIXER1,
	WM8400_LRI3LO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOMIX LIN3 Bypass Switch", WM8400_OUTPUT_MIXER1,
	WM8400_LLI3LO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOMIX RIN12 PGA Bypass Switch", WM8400_OUTPUT_MIXER1,
	WM8400_LR12LO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOMIX LIN12 PGA Bypass Switch", WM8400_OUTPUT_MIXER1,
	WM8400_LL12LO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOMIX Left DAC Switch", WM8400_OUTPUT_MIXER1,
	WM8400_LDLO_SHIFT, 1, 0),
};

/* ROMIX */
static const struct snd_kcontrol_new wm8400_dapm_romix_controls[] = {
SOC_DAPM_SINGLE("ROMIX Left ADC Bypass Switch", WM8400_OUTPUT_MIXER2,
	WM8400_RLBRO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROMIX Right ADC Bypass Switch", WM8400_OUTPUT_MIXER2,
	WM8400_RRBRO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROMIX LIN3 Bypass Switch", WM8400_OUTPUT_MIXER2,
	WM8400_RLI3RO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROMIX RIN3 Bypass Switch", WM8400_OUTPUT_MIXER2,
	WM8400_RRI3RO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROMIX LIN12 PGA Bypass Switch", WM8400_OUTPUT_MIXER2,
	WM8400_RL12RO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROMIX RIN12 PGA Bypass Switch", WM8400_OUTPUT_MIXER2,
	WM8400_RR12RO_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROMIX Right DAC Switch", WM8400_OUTPUT_MIXER2,
	WM8400_RDRO_SHIFT, 1, 0),
};

/* LONMIX */
static const struct snd_kcontrol_new wm8400_dapm_lonmix_controls[] = {
SOC_DAPM_SINGLE("LONMIX Left Mixer PGA Switch", WM8400_LINE_MIXER1,
	WM8400_LLOPGALON_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LONMIX Right Mixer PGA Switch", WM8400_LINE_MIXER1,
	WM8400_LROPGALON_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LONMIX Inverted LOP Switch", WM8400_LINE_MIXER1,
	WM8400_LOPLON_SHIFT, 1, 0),
};

/* LOPMIX */
static const struct snd_kcontrol_new wm8400_dapm_lopmix_controls[] = {
SOC_DAPM_SINGLE("LOPMIX Right Mic Bypass Switch", WM8400_LINE_MIXER1,
	WM8400_LR12LOP_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOPMIX Left Mic Bypass Switch", WM8400_LINE_MIXER1,
	WM8400_LL12LOP_SHIFT, 1, 0),
SOC_DAPM_SINGLE("LOPMIX Left Mixer PGA Switch", WM8400_LINE_MIXER1,
	WM8400_LLOPGALOP_SHIFT, 1, 0),
};

/* RONMIX */
static const struct snd_kcontrol_new wm8400_dapm_ronmix_controls[] = {
SOC_DAPM_SINGLE("RONMIX Right Mixer PGA Switch", WM8400_LINE_MIXER2,
	WM8400_RROPGARON_SHIFT, 1, 0),
SOC_DAPM_SINGLE("RONMIX Left Mixer PGA Switch", WM8400_LINE_MIXER2,
	WM8400_RLOPGARON_SHIFT, 1, 0),
SOC_DAPM_SINGLE("RONMIX Inverted ROP Switch", WM8400_LINE_MIXER2,
	WM8400_ROPRON_SHIFT, 1, 0),
};

/* ROPMIX */
static const struct snd_kcontrol_new wm8400_dapm_ropmix_controls[] = {
SOC_DAPM_SINGLE("ROPMIX Left Mic Bypass Switch", WM8400_LINE_MIXER2,
	WM8400_RL12ROP_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROPMIX Right Mic Bypass Switch", WM8400_LINE_MIXER2,
	WM8400_RR12ROP_SHIFT, 1, 0),
SOC_DAPM_SINGLE("ROPMIX Right Mixer PGA Switch", WM8400_LINE_MIXER2,
	WM8400_RROPGAROP_SHIFT, 1, 0),
};

/* OUT3MIX */
static const struct snd_kcontrol_new wm8400_dapm_out3mix_controls[] = {
SOC_DAPM_SINGLE("OUT3MIX LIN4/RXP Bypass Switch", WM8400_OUT3_4_MIXER,
	WM8400_LI4O3_SHIFT, 1, 0),
SOC_DAPM_SINGLE("OUT3MIX Left Out PGA Switch", WM8400_OUT3_4_MIXER,
	WM8400_LPGAO3_SHIFT, 1, 0),
};

/* OUT4MIX */
static const struct snd_kcontrol_new wm8400_dapm_out4mix_controls[] = {
SOC_DAPM_SINGLE("OUT4MIX Right Out PGA Switch", WM8400_OUT3_4_MIXER,
	WM8400_RPGAO4_SHIFT, 1, 0),
SOC_DAPM_SINGLE("OUT4MIX RIN4/RXP Bypass Switch", WM8400_OUT3_4_MIXER,
	WM8400_RI4O4_SHIFT, 1, 0),
};

/* SPKMIX */
static const struct snd_kcontrol_new wm8400_dapm_spkmix_controls[] = {
SOC_DAPM_SINGLE("SPKMIX LIN2 Bypass Switch", WM8400_SPEAKER_MIXER,
	WM8400_LI2SPK_SHIFT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX LADC Bypass Switch", WM8400_SPEAKER_MIXER,
	WM8400_LB2SPK_SHIFT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Left Mixer PGA Switch", WM8400_SPEAKER_MIXER,
	WM8400_LOPGASPK_SHIFT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Left DAC Switch", WM8400_SPEAKER_MIXER,
	WM8400_LDSPK_SHIFT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Right DAC Switch", WM8400_SPEAKER_MIXER,
	WM8400_RDSPK_SHIFT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Right Mixer PGA Switch", WM8400_SPEAKER_MIXER,
	WM8400_ROPGASPK_SHIFT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX RADC Bypass Switch", WM8400_SPEAKER_MIXER,
	WM8400_RL12ROP_SHIFT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX RIN2 Bypass Switch", WM8400_SPEAKER_MIXER,
	WM8400_RI2SPK_SHIFT, 1, 0),
};

static const struct snd_soc_dapm_widget wm8400_dapm_widgets[] = {
/* Input Side */
/* Input Lines */
SND_SOC_DAPM_INPUT("LIN1"),
SND_SOC_DAPM_INPUT("LIN2"),
SND_SOC_DAPM_INPUT("LIN3"),
SND_SOC_DAPM_INPUT("LIN4/RXN"),
SND_SOC_DAPM_INPUT("RIN3"),
SND_SOC_DAPM_INPUT("RIN4/RXP"),
SND_SOC_DAPM_INPUT("RIN1"),
SND_SOC_DAPM_INPUT("RIN2"),
SND_SOC_DAPM_INPUT("Internal ADC Source"),

/* DACs */
SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8400_POWER_MANAGEMENT_2,
	WM8400_ADCL_ENA_SHIFT, 0),
SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8400_POWER_MANAGEMENT_2,
	WM8400_ADCR_ENA_SHIFT, 0),

/* Input PGAs */
SND_SOC_DAPM_MIXER("LIN12 PGA", WM8400_POWER_MANAGEMENT_2,
		   WM8400_LIN12_ENA_SHIFT,
		   0, &wm8400_dapm_lin12_pga_controls[0],
		   ARRAY_SIZE(wm8400_dapm_lin12_pga_controls)),
SND_SOC_DAPM_MIXER("LIN34 PGA", WM8400_POWER_MANAGEMENT_2,
		   WM8400_LIN34_ENA_SHIFT,
		   0, &wm8400_dapm_lin34_pga_controls[0],
		   ARRAY_SIZE(wm8400_dapm_lin34_pga_controls)),
SND_SOC_DAPM_MIXER("RIN12 PGA", WM8400_POWER_MANAGEMENT_2,
		   WM8400_RIN12_ENA_SHIFT,
		   0, &wm8400_dapm_rin12_pga_controls[0],
		   ARRAY_SIZE(wm8400_dapm_rin12_pga_controls)),
SND_SOC_DAPM_MIXER("RIN34 PGA", WM8400_POWER_MANAGEMENT_2,
		   WM8400_RIN34_ENA_SHIFT,
		   0, &wm8400_dapm_rin34_pga_controls[0],
		   ARRAY_SIZE(wm8400_dapm_rin34_pga_controls)),

SND_SOC_DAPM_SUPPLY("INL", WM8400_POWER_MANAGEMENT_2, WM8400_AINL_ENA_SHIFT,
		    0, NULL, 0),
SND_SOC_DAPM_SUPPLY("INR", WM8400_POWER_MANAGEMENT_2, WM8400_AINR_ENA_SHIFT,
		    0, NULL, 0),

/* INMIXL */
SND_SOC_DAPM_MIXER("INMIXL", SND_SOC_NOPM, 0, 0,
	&wm8400_dapm_inmixl_controls[0],
	ARRAY_SIZE(wm8400_dapm_inmixl_controls)),

/* AINLMUX */
SND_SOC_DAPM_MUX("AILNMUX", SND_SOC_NOPM, 0, 0, &wm8400_dapm_ainlmux_controls),

/* INMIXR */
SND_SOC_DAPM_MIXER("INMIXR", SND_SOC_NOPM, 0, 0,
	&wm8400_dapm_inmixr_controls[0],
	ARRAY_SIZE(wm8400_dapm_inmixr_controls)),

/* AINRMUX */
SND_SOC_DAPM_MUX("AIRNMUX", SND_SOC_NOPM, 0, 0, &wm8400_dapm_ainrmux_controls),

/* Output Side */
/* DACs */
SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8400_POWER_MANAGEMENT_3,
	WM8400_DACL_ENA_SHIFT, 0),
SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8400_POWER_MANAGEMENT_3,
	WM8400_DACR_ENA_SHIFT, 0),

/* LOMIX */
SND_SOC_DAPM_MIXER_E("LOMIX", WM8400_POWER_MANAGEMENT_3,
		     WM8400_LOMIX_ENA_SHIFT,
		     0, &wm8400_dapm_lomix_controls[0],
		     ARRAY_SIZE(wm8400_dapm_lomix_controls),
		     outmixer_event, SND_SOC_DAPM_PRE_REG),

/* LONMIX */
SND_SOC_DAPM_MIXER("LONMIX", WM8400_POWER_MANAGEMENT_3, WM8400_LON_ENA_SHIFT,
		   0, &wm8400_dapm_lonmix_controls[0],
		   ARRAY_SIZE(wm8400_dapm_lonmix_controls)),

/* LOPMIX */
SND_SOC_DAPM_MIXER("LOPMIX", WM8400_POWER_MANAGEMENT_3, WM8400_LOP_ENA_SHIFT,
		   0, &wm8400_dapm_lopmix_controls[0],
		   ARRAY_SIZE(wm8400_dapm_lopmix_controls)),

/* OUT3MIX */
SND_SOC_DAPM_MIXER("OUT3MIX", WM8400_POWER_MANAGEMENT_1, WM8400_OUT3_ENA_SHIFT,
		   0, &wm8400_dapm_out3mix_controls[0],
		   ARRAY_SIZE(wm8400_dapm_out3mix_controls)),

/* SPKMIX */
SND_SOC_DAPM_MIXER_E("SPKMIX", WM8400_POWER_MANAGEMENT_1, WM8400_SPK_ENA_SHIFT,
		     0, &wm8400_dapm_spkmix_controls[0],
		     ARRAY_SIZE(wm8400_dapm_spkmix_controls), outmixer_event,
		     SND_SOC_DAPM_PRE_REG),

/* OUT4MIX */
SND_SOC_DAPM_MIXER("OUT4MIX", WM8400_POWER_MANAGEMENT_1, WM8400_OUT4_ENA_SHIFT,
	0, &wm8400_dapm_out4mix_controls[0],
	ARRAY_SIZE(wm8400_dapm_out4mix_controls)),

/* ROPMIX */
SND_SOC_DAPM_MIXER("ROPMIX", WM8400_POWER_MANAGEMENT_3, WM8400_ROP_ENA_SHIFT,
		   0, &wm8400_dapm_ropmix_controls[0],
		   ARRAY_SIZE(wm8400_dapm_ropmix_controls)),

/* RONMIX */
SND_SOC_DAPM_MIXER("RONMIX", WM8400_POWER_MANAGEMENT_3, WM8400_RON_ENA_SHIFT,
		   0, &wm8400_dapm_ronmix_controls[0],
		   ARRAY_SIZE(wm8400_dapm_ronmix_controls)),

/* ROMIX */
SND_SOC_DAPM_MIXER_E("ROMIX", WM8400_POWER_MANAGEMENT_3,
		     WM8400_ROMIX_ENA_SHIFT,
		     0, &wm8400_dapm_romix_controls[0],
		     ARRAY_SIZE(wm8400_dapm_romix_controls),
		     outmixer_event, SND_SOC_DAPM_PRE_REG),

/* LOUT PGA */
SND_SOC_DAPM_PGA("LOUT PGA", WM8400_POWER_MANAGEMENT_1, WM8400_LOUT_ENA_SHIFT,
		 0, NULL, 0),

/* ROUT PGA */
SND_SOC_DAPM_PGA("ROUT PGA", WM8400_POWER_MANAGEMENT_1, WM8400_ROUT_ENA_SHIFT,
		 0, NULL, 0),

/* LOPGA */
SND_SOC_DAPM_PGA("LOPGA", WM8400_POWER_MANAGEMENT_3, WM8400_LOPGA_ENA_SHIFT, 0,
	NULL, 0),

/* ROPGA */
SND_SOC_DAPM_PGA("ROPGA", WM8400_POWER_MANAGEMENT_3, WM8400_ROPGA_ENA_SHIFT, 0,
	NULL, 0),

/* MICBIAS */
SND_SOC_DAPM_SUPPLY("MICBIAS", WM8400_POWER_MANAGEMENT_1,
		    WM8400_MIC1BIAS_ENA_SHIFT, 0, NULL, 0),

SND_SOC_DAPM_OUTPUT("LON"),
SND_SOC_DAPM_OUTPUT("LOP"),
SND_SOC_DAPM_OUTPUT("OUT3"),
SND_SOC_DAPM_OUTPUT("LOUT"),
SND_SOC_DAPM_OUTPUT("SPKN"),
SND_SOC_DAPM_OUTPUT("SPKP"),
SND_SOC_DAPM_OUTPUT("ROUT"),
SND_SOC_DAPM_OUTPUT("OUT4"),
SND_SOC_DAPM_OUTPUT("ROP"),
SND_SOC_DAPM_OUTPUT("RON"),

SND_SOC_DAPM_OUTPUT("Internal DAC Sink"),
};

static const struct snd_soc_dapm_route wm8400_dapm_routes[] = {
	/* Make DACs turn on when playing even if not mixed into any outputs */
	{"Internal DAC Sink", NULL, "Left DAC"},
	{"Internal DAC Sink", NULL, "Right DAC"},

	/* Make ADCs turn on when recording
	 * even if not mixed from any inputs */
	{"Left ADC", NULL, "Internal ADC Source"},
	{"Right ADC", NULL, "Internal ADC Source"},

	/* Input Side */
	/* LIN12 PGA */
	{"LIN12 PGA", "LIN1 Switch", "LIN1"},
	{"LIN12 PGA", "LIN2 Switch", "LIN2"},
	/* LIN34 PGA */
	{"LIN34 PGA", "LIN3 Switch", "LIN3"},
	{"LIN34 PGA", "LIN4 Switch", "LIN4/RXN"},
	/* INMIXL */
	{"INMIXL", NULL, "INL"},
	{"INMIXL", "Record Left Volume", "LOMIX"},
	{"INMIXL", "LIN2 Volume", "LIN2"},
	{"INMIXL", "LINPGA12 Switch", "LIN12 PGA"},
	{"INMIXL", "LINPGA34 Switch", "LIN34 PGA"},
	/* AILNMUX */
	{"AILNMUX", NULL, "INL"},
	{"AILNMUX", "INMIXL Mix", "INMIXL"},
	{"AILNMUX", "DIFFINL Mix", "LIN12 PGA"},
	{"AILNMUX", "DIFFINL Mix", "LIN34 PGA"},
	{"AILNMUX", "RXVOICE Mix", "LIN4/RXN"},
	{"AILNMUX", "RXVOICE Mix", "RIN4/RXP"},
	/* ADC */
	{"Left ADC", NULL, "AILNMUX"},

	/* RIN12 PGA */
	{"RIN12 PGA", "RIN1 Switch", "RIN1"},
	{"RIN12 PGA", "RIN2 Switch", "RIN2"},
	/* RIN34 PGA */
	{"RIN34 PGA", "RIN3 Switch", "RIN3"},
	{"RIN34 PGA", "RIN4 Switch", "RIN4/RXP"},
	/* INMIXR */
	{"INMIXR", NULL, "INR"},
	{"INMIXR", "Record Right Volume", "ROMIX"},
	{"INMIXR", "RIN2 Volume", "RIN2"},
	{"INMIXR", "RINPGA12 Switch", "RIN12 PGA"},
	{"INMIXR", "RINPGA34 Switch", "RIN34 PGA"},
	/* AIRNMUX */
	{"AIRNMUX", NULL, "INR"},
	{"AIRNMUX", "INMIXR Mix", "INMIXR"},
	{"AIRNMUX", "DIFFINR Mix", "RIN12 PGA"},
	{"AIRNMUX", "DIFFINR Mix", "RIN34 PGA"},
	{"AIRNMUX", "RXVOICE Mix", "LIN4/RXN"},
	{"AIRNMUX", "RXVOICE Mix", "RIN4/RXP"},
	/* ADC */
	{"Right ADC", NULL, "AIRNMUX"},

	/* LOMIX */
	{"LOMIX", "LOMIX RIN3 Bypass Switch", "RIN3"},
	{"LOMIX", "LOMIX LIN3 Bypass Switch", "LIN3"},
	{"LOMIX", "LOMIX LIN12 PGA Bypass Switch", "LIN12 PGA"},
	{"LOMIX", "LOMIX RIN12 PGA Bypass Switch", "RIN12 PGA"},
	{"LOMIX", "LOMIX Right ADC Bypass Switch", "AIRNMUX"},
	{"LOMIX", "LOMIX Left ADC Bypass Switch", "AILNMUX"},
	{"LOMIX", "LOMIX Left DAC Switch", "Left DAC"},

	/* ROMIX */
	{"ROMIX", "ROMIX RIN3 Bypass Switch", "RIN3"},
	{"ROMIX", "ROMIX LIN3 Bypass Switch", "LIN3"},
	{"ROMIX", "ROMIX LIN12 PGA Bypass Switch", "LIN12 PGA"},
	{"ROMIX", "ROMIX RIN12 PGA Bypass Switch", "RIN12 PGA"},
	{"ROMIX", "ROMIX Right ADC Bypass Switch", "AIRNMUX"},
	{"ROMIX", "ROMIX Left ADC Bypass Switch", "AILNMUX"},
	{"ROMIX", "ROMIX Right DAC Switch", "Right DAC"},

	/* SPKMIX */
	{"SPKMIX", "SPKMIX LIN2 Bypass Switch", "LIN2"},
	{"SPKMIX", "SPKMIX RIN2 Bypass Switch", "RIN2"},
	{"SPKMIX", "SPKMIX LADC Bypass Switch", "AILNMUX"},
	{"SPKMIX", "SPKMIX RADC Bypass Switch", "AIRNMUX"},
	{"SPKMIX", "SPKMIX Left Mixer PGA Switch", "LOPGA"},
	{"SPKMIX", "SPKMIX Right Mixer PGA Switch", "ROPGA"},
	{"SPKMIX", "SPKMIX Right DAC Switch", "Right DAC"},
	{"SPKMIX", "SPKMIX Left DAC Switch", "Right DAC"},

	/* LONMIX */
	{"LONMIX", "LONMIX Left Mixer PGA Switch", "LOPGA"},
	{"LONMIX", "LONMIX Right Mixer PGA Switch", "ROPGA"},
	{"LONMIX", "LONMIX Inverted LOP Switch", "LOPMIX"},

	/* LOPMIX */
	{"LOPMIX", "LOPMIX Right Mic Bypass Switch", "RIN12 PGA"},
	{"LOPMIX", "LOPMIX Left Mic Bypass Switch", "LIN12 PGA"},
	{"LOPMIX", "LOPMIX Left Mixer PGA Switch", "LOPGA"},

	/* OUT3MIX */
	{"OUT3MIX", "OUT3MIX LIN4/RXP Bypass Switch", "LIN4/RXN"},
	{"OUT3MIX", "OUT3MIX Left Out PGA Switch", "LOPGA"},

	/* OUT4MIX */
	{"OUT4MIX", "OUT4MIX Right Out PGA Switch", "ROPGA"},
	{"OUT4MIX", "OUT4MIX RIN4/RXP Bypass Switch", "RIN4/RXP"},

	/* RONMIX */
	{"RONMIX", "RONMIX Right Mixer PGA Switch", "ROPGA"},
	{"RONMIX", "RONMIX Left Mixer PGA Switch", "LOPGA"},
	{"RONMIX", "RONMIX Inverted ROP Switch", "ROPMIX"},

	/* ROPMIX */
	{"ROPMIX", "ROPMIX Left Mic Bypass Switch", "LIN12 PGA"},
	{"ROPMIX", "ROPMIX Right Mic Bypass Switch", "RIN12 PGA"},
	{"ROPMIX", "ROPMIX Right Mixer PGA Switch", "ROPGA"},

	/* Out Mixer PGAs */
	{"LOPGA", NULL, "LOMIX"},
	{"ROPGA", NULL, "ROMIX"},

	{"LOUT PGA", NULL, "LOMIX"},
	{"ROUT PGA", NULL, "ROMIX"},

	/* Output Pins */
	{"LON", NULL, "LONMIX"},
	{"LOP", NULL, "LOPMIX"},
	{"OUT3", NULL, "OUT3MIX"},
	{"LOUT", NULL, "LOUT PGA"},
	{"SPKN", NULL, "SPKMIX"},
	{"ROUT", NULL, "ROUT PGA"},
	{"OUT4", NULL, "OUT4MIX"},
	{"ROP", NULL, "ROPMIX"},
	{"RON", NULL, "RONMIX"},
};

/*
 * Clock after FLL and dividers
 */
static int wm8400_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8400_priv *wm8400 = snd_soc_codec_get_drvdata(codec);

	wm8400->sysclk = freq;
	return 0;
}

struct fll_factors {
	u16 n;
	u16 k;
	u16 outdiv;
	u16 fratio;
	u16 freq_ref;
};

#define FIXED_FLL_SIZE ((1 << 16) * 10)

static int fll_factors(struct wm8400_priv *wm8400, struct fll_factors *factors,
		       unsigned int Fref, unsigned int Fout)
{
	u64 Kpart;
	unsigned int K, Nmod, target;

	factors->outdiv = 2;
	while (Fout * factors->outdiv <  90000000 ||
	       Fout * factors->outdiv > 100000000) {
		factors->outdiv *= 2;
		if (factors->outdiv > 32) {
			dev_err(wm8400->wm8400->dev,
				"Unsupported FLL output frequency %uHz\n",
				Fout);
			return -EINVAL;
		}
	}
	target = Fout * factors->outdiv;
	factors->outdiv = factors->outdiv >> 2;

	if (Fref < 48000)
		factors->freq_ref = 1;
	else
		factors->freq_ref = 0;

	if (Fref < 1000000)
		factors->fratio = 9;
	else
		factors->fratio = 0;

	/* Ensure we have a fractional part */
	do {
		if (Fref < 1000000)
			factors->fratio--;
		else
			factors->fratio++;

		if (factors->fratio < 1 || factors->fratio > 8) {
			dev_err(wm8400->wm8400->dev,
				"Unable to calculate FRATIO\n");
			return -EINVAL;
		}

		factors->n = target / (Fref * factors->fratio);
		Nmod = target % (Fref * factors->fratio);
	} while (Nmod == 0);

	/* Calculate fractional part - scale up so we can round. */
	Kpart = FIXED_FLL_SIZE * (long long)Nmod;

	do_div(Kpart, (Fref * factors->fratio));

	K = Kpart & 0xFFFFFFFF;

	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	factors->k = K / 10;

	dev_dbg(wm8400->wm8400->dev,
		"FLL: Fref=%u Fout=%u N=%x K=%x, FRATIO=%x OUTDIV=%x\n",
		Fref, Fout,
		factors->n, factors->k, factors->fratio, factors->outdiv);

	return 0;
}

static int wm8400_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
			      int source, unsigned int freq_in,
			      unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct wm8400_priv *wm8400 = snd_soc_codec_get_drvdata(codec);
	struct fll_factors factors;
	int ret;
	u16 reg;

	if (freq_in == wm8400->fll_in && freq_out == wm8400->fll_out)
		return 0;

	if (freq_out) {
		ret = fll_factors(wm8400, &factors, freq_in, freq_out);
		if (ret != 0)
			return ret;
	} else {
		/* Bodge GCC 4.4.0 uninitialised variable warning - it
		 * doesn't seem capable of working out that we exit if
		 * freq_out is 0 before any of the uses. */
		memset(&factors, 0, sizeof(factors));
	}

	wm8400->fll_out = freq_out;
	wm8400->fll_in = freq_in;

	/* We *must* disable the FLL before any changes */
	reg = snd_soc_read(codec, WM8400_POWER_MANAGEMENT_2);
	reg &= ~WM8400_FLL_ENA;
	snd_soc_write(codec, WM8400_POWER_MANAGEMENT_2, reg);

	reg = snd_soc_read(codec, WM8400_FLL_CONTROL_1);
	reg &= ~WM8400_FLL_OSC_ENA;
	snd_soc_write(codec, WM8400_FLL_CONTROL_1, reg);

	if (!freq_out)
		return 0;

	reg &= ~(WM8400_FLL_REF_FREQ | WM8400_FLL_FRATIO_MASK);
	reg |= WM8400_FLL_FRAC | factors.fratio;
	reg |= factors.freq_ref << WM8400_FLL_REF_FREQ_SHIFT;
	snd_soc_write(codec, WM8400_FLL_CONTROL_1, reg);

	snd_soc_write(codec, WM8400_FLL_CONTROL_2, factors.k);
	snd_soc_write(codec, WM8400_FLL_CONTROL_3, factors.n);

	reg = snd_soc_read(codec, WM8400_FLL_CONTROL_4);
	reg &= ~WM8400_FLL_OUTDIV_MASK;
	reg |= factors.outdiv;
	snd_soc_write(codec, WM8400_FLL_CONTROL_4, reg);

	return 0;
}

/*
 * Sets ADC and Voice DAC format.
 */
static int wm8400_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 audio1, audio3;

	audio1 = snd_soc_read(codec, WM8400_AUDIO_INTERFACE_1);
	audio3 = snd_soc_read(codec, WM8400_AUDIO_INTERFACE_3);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		audio3 &= ~WM8400_AIF_MSTR1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		audio3 |= WM8400_AIF_MSTR1;
		break;
	default:
		return -EINVAL;
	}

	audio1 &= ~WM8400_AIF_FMT_MASK;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		audio1 |= WM8400_AIF_FMT_I2S;
		audio1 &= ~WM8400_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		audio1 |= WM8400_AIF_FMT_RIGHTJ;
		audio1 &= ~WM8400_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		audio1 |= WM8400_AIF_FMT_LEFTJ;
		audio1 &= ~WM8400_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		audio1 |= WM8400_AIF_FMT_DSP;
		audio1 &= ~WM8400_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		audio1 |= WM8400_AIF_FMT_DSP | WM8400_AIF_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_write(codec, WM8400_AUDIO_INTERFACE_1, audio1);
	snd_soc_write(codec, WM8400_AUDIO_INTERFACE_3, audio3);
	return 0;
}

static int wm8400_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8400_MCLK_DIV:
		reg = snd_soc_read(codec, WM8400_CLOCKING_2) &
			~WM8400_MCLK_DIV_MASK;
		snd_soc_write(codec, WM8400_CLOCKING_2, reg | div);
		break;
	case WM8400_DACCLK_DIV:
		reg = snd_soc_read(codec, WM8400_CLOCKING_2) &
			~WM8400_DAC_CLKDIV_MASK;
		snd_soc_write(codec, WM8400_CLOCKING_2, reg | div);
		break;
	case WM8400_ADCCLK_DIV:
		reg = snd_soc_read(codec, WM8400_CLOCKING_2) &
			~WM8400_ADC_CLKDIV_MASK;
		snd_soc_write(codec, WM8400_CLOCKING_2, reg | div);
		break;
	case WM8400_BCLK_DIV:
		reg = snd_soc_read(codec, WM8400_CLOCKING_1) &
			~WM8400_BCLK_DIV_MASK;
		snd_soc_write(codec, WM8400_CLOCKING_1, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8400_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 audio1 = snd_soc_read(codec, WM8400_AUDIO_INTERFACE_1);

	audio1 &= ~WM8400_AIF_WL_MASK;
	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		audio1 |= WM8400_AIF_WL_20BITS;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		audio1 |= WM8400_AIF_WL_24BITS;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		audio1 |= WM8400_AIF_WL_32BITS;
		break;
	}

	snd_soc_write(codec, WM8400_AUDIO_INTERFACE_1, audio1);
	return 0;
}

static int wm8400_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 val = snd_soc_read(codec, WM8400_DAC_CTRL) & ~WM8400_DAC_MUTE;

	if (mute)
		snd_soc_write(codec, WM8400_DAC_CTRL, val | WM8400_DAC_MUTE);
	else
		snd_soc_write(codec, WM8400_DAC_CTRL, val);

	return 0;
}

/* TODO: set bias for best performance at standby */
static int wm8400_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct wm8400_priv *wm8400 = snd_soc_codec_get_drvdata(codec);
	u16 val;
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID=2*50k */
		val = snd_soc_read(codec, WM8400_POWER_MANAGEMENT_1) &
			~WM8400_VMID_MODE_MASK;
		snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1, val | 0x2);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF) {
			ret = regulator_bulk_enable(ARRAY_SIZE(power),
						    &power[0]);
			if (ret != 0) {
				dev_err(wm8400->wm8400->dev,
					"Failed to enable regulators: %d\n",
					ret);
				return ret;
			}

			snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1,
				     WM8400_CODEC_ENA | WM8400_SYSCLK_ENA);

			/* Enable POBCTRL, SOFT_ST, VMIDTOG and BUFDCOPEN */
			snd_soc_write(codec, WM8400_ANTIPOP2, WM8400_SOFTST |
				     WM8400_BUFDCOPEN | WM8400_POBCTRL);

			msleep(50);

			/* Enable VREF & VMID at 2x50k */
			val = snd_soc_read(codec, WM8400_POWER_MANAGEMENT_1);
			val |= 0x2 | WM8400_VREF_ENA;
			snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1, val);

			/* Enable BUFIOEN */
			snd_soc_write(codec, WM8400_ANTIPOP2, WM8400_SOFTST |
				     WM8400_BUFDCOPEN | WM8400_POBCTRL |
				     WM8400_BUFIOEN);

			/* disable POBCTRL, SOFT_ST and BUFDCOPEN */
			snd_soc_write(codec, WM8400_ANTIPOP2, WM8400_BUFIOEN);
		}

		/* VMID=2*300k */
		val = snd_soc_read(codec, WM8400_POWER_MANAGEMENT_1) &
			~WM8400_VMID_MODE_MASK;
		snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1, val | 0x4);
		break;

	case SND_SOC_BIAS_OFF:
		/* Enable POBCTRL and SOFT_ST */
		snd_soc_write(codec, WM8400_ANTIPOP2, WM8400_SOFTST |
			WM8400_POBCTRL | WM8400_BUFIOEN);

		/* Enable POBCTRL, SOFT_ST and BUFDCOPEN */
		snd_soc_write(codec, WM8400_ANTIPOP2, WM8400_SOFTST |
			WM8400_BUFDCOPEN | WM8400_POBCTRL |
			WM8400_BUFIOEN);

		/* mute DAC */
		val = snd_soc_read(codec, WM8400_DAC_CTRL);
		snd_soc_write(codec, WM8400_DAC_CTRL, val | WM8400_DAC_MUTE);

		/* Enable any disabled outputs */
		val = snd_soc_read(codec, WM8400_POWER_MANAGEMENT_1);
		val |= WM8400_SPK_ENA | WM8400_OUT3_ENA |
			WM8400_OUT4_ENA | WM8400_LOUT_ENA |
			WM8400_ROUT_ENA;
		snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1, val);

		/* Disable VMID */
		val &= ~WM8400_VMID_MODE_MASK;
		snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1, val);

		msleep(300);

		/* Enable all output discharge bits */
		snd_soc_write(codec, WM8400_ANTIPOP1, WM8400_DIS_LLINE |
			WM8400_DIS_RLINE | WM8400_DIS_OUT3 |
			WM8400_DIS_OUT4 | WM8400_DIS_LOUT |
			WM8400_DIS_ROUT);

		/* Disable VREF */
		val &= ~WM8400_VREF_ENA;
		snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1, val);

		/* disable POBCTRL, SOFT_ST and BUFDCOPEN */
		snd_soc_write(codec, WM8400_ANTIPOP2, 0x0);

		ret = regulator_bulk_disable(ARRAY_SIZE(power),
					     &power[0]);
		if (ret != 0)
			return ret;

		break;
	}

	codec->dapm.bias_level = level;
	return 0;
}

#define WM8400_RATES SNDRV_PCM_RATE_8000_96000

#define WM8400_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8400_dai_ops = {
	.hw_params = wm8400_hw_params,
	.digital_mute = wm8400_mute,
	.set_fmt = wm8400_set_dai_fmt,
	.set_clkdiv = wm8400_set_dai_clkdiv,
	.set_sysclk = wm8400_set_dai_sysclk,
	.set_pll = wm8400_set_dai_pll,
};

/*
 * The WM8400 supports 2 different and mutually exclusive DAI
 * configurations.
 *
 * 1. ADC/DAC on Primary Interface
 * 2. ADC on Primary Interface/DAC on secondary
 */
static struct snd_soc_dai_driver wm8400_dai = {
/* ADC/DAC on primary */
	.name = "wm8400-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8400_RATES,
		.formats = WM8400_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8400_RATES,
		.formats = WM8400_FORMATS,
	},
	.ops = &wm8400_dai_ops,
};

static int wm8400_suspend(struct snd_soc_codec *codec)
{
	wm8400_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int wm8400_resume(struct snd_soc_codec *codec)
{
	wm8400_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	return 0;
}

static void wm8400_probe_deferred(struct work_struct *work)
{
	struct wm8400_priv *priv = container_of(work, struct wm8400_priv,
						work);
	struct snd_soc_codec *codec = priv->codec;

	/* charge output caps */
	wm8400_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
}

static int wm8400_codec_probe(struct snd_soc_codec *codec)
{
	struct wm8400 *wm8400 = dev_get_platdata(codec->dev);
	struct wm8400_priv *priv;
	int ret;
	u16 reg;

	priv = devm_kzalloc(codec->dev, sizeof(struct wm8400_priv),
			    GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	snd_soc_codec_set_drvdata(codec, priv);
	priv->wm8400 = wm8400;
	codec->control_data = wm8400->regmap;
	priv->codec = codec;

	snd_soc_codec_set_cache_io(codec, 8, 16, SND_SOC_REGMAP);

	ret = devm_regulator_bulk_get(wm8400->dev,
				 ARRAY_SIZE(power), &power[0]);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to get regulators: %d\n", ret);
		return ret;
	}

	INIT_WORK(&priv->work, wm8400_probe_deferred);

	wm8400_codec_reset(codec);

	reg = snd_soc_read(codec, WM8400_POWER_MANAGEMENT_1);
	snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1, reg | WM8400_CODEC_ENA);

	/* Latch volume update bits */
	reg = snd_soc_read(codec, WM8400_LEFT_LINE_INPUT_1_2_VOLUME);
	snd_soc_write(codec, WM8400_LEFT_LINE_INPUT_1_2_VOLUME,
		     reg & WM8400_IPVU);
	reg = snd_soc_read(codec, WM8400_RIGHT_LINE_INPUT_1_2_VOLUME);
	snd_soc_write(codec, WM8400_RIGHT_LINE_INPUT_1_2_VOLUME,
		     reg & WM8400_IPVU);

	snd_soc_write(codec, WM8400_LEFT_OUTPUT_VOLUME, 0x50 | (1<<8));
	snd_soc_write(codec, WM8400_RIGHT_OUTPUT_VOLUME, 0x50 | (1<<8));

	if (!schedule_work(&priv->work))
		return -EINVAL;
	return 0;
}

static int  wm8400_codec_remove(struct snd_soc_codec *codec)
{
	u16 reg;

	reg = snd_soc_read(codec, WM8400_POWER_MANAGEMENT_1);
	snd_soc_write(codec, WM8400_POWER_MANAGEMENT_1,
		     reg & (~WM8400_CODEC_ENA));

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_wm8400 = {
	.probe =	wm8400_codec_probe,
	.remove =	wm8400_codec_remove,
	.suspend =	wm8400_suspend,
	.resume =	wm8400_resume,
	.set_bias_level = wm8400_set_bias_level,

	.controls = wm8400_snd_controls,
	.num_controls = ARRAY_SIZE(wm8400_snd_controls),
	.dapm_widgets = wm8400_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(wm8400_dapm_widgets),
	.dapm_routes = wm8400_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(wm8400_dapm_routes),
};

static int wm8400_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev, &soc_codec_dev_wm8400,
			&wm8400_dai, 1);
}

static int wm8400_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver wm8400_codec_driver = {
	.driver = {
		   .name = "wm8400-codec",
		   .owner = THIS_MODULE,
		   },
	.probe = wm8400_probe,
	.remove = wm8400_remove,
};

module_platform_driver(wm8400_codec_driver);

MODULE_DESCRIPTION("ASoC WM8400 driver");
MODULE_AUTHOR("Mark Brown");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:wm8400-codec");
