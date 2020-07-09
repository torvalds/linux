// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm8990.c  --  WM8990 ALSA Soc Audio driver
 *
 * Copyright 2008 Wolfson Microelectronics PLC.
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
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
#include <sound/tlv.h>
#include <asm/div64.h>

#include "wm8990.h"

/* codec private data */
struct wm8990_priv {
	struct regmap *regmap;
	unsigned int sysclk;
	unsigned int pcmclk;
};

#define wm8990_reset(c) snd_soc_component_write(c, WM8990_RESET, 0)

static const DECLARE_TLV_DB_SCALE(in_pga_tlv, -1650, 3000, 0);

static const DECLARE_TLV_DB_SCALE(out_mix_tlv, 0, -2100, 0);

static const DECLARE_TLV_DB_SCALE(out_pga_tlv, -7300, 600, 0);

static const DECLARE_TLV_DB_SCALE(out_dac_tlv, -7163, 0, 0);

static const DECLARE_TLV_DB_SCALE(in_adc_tlv, -7163, 1763, 0);

static const DECLARE_TLV_DB_SCALE(out_sidetone_tlv, -3600, 0, 0);

static int wm899x_outpga_put_volsw_vu(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int reg = mc->reg;
	int ret;
	u16 val;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	/* now hit the volume update bits (always bit 8) */
	val = snd_soc_component_read(component, reg);
	return snd_soc_component_write(component, reg, val | 0x0100);
}

#define SOC_WM899X_OUTPGA_SINGLE_R_TLV(xname, reg, shift, max, invert,\
	tlv_array) \
	SOC_SINGLE_EXT_TLV(xname, reg, shift, max, invert, \
		snd_soc_get_volsw, wm899x_outpga_put_volsw_vu, tlv_array)


static const char *wm8990_digital_sidetone[] =
	{"None", "Left ADC", "Right ADC", "Reserved"};

static SOC_ENUM_SINGLE_DECL(wm8990_left_digital_sidetone_enum,
			    WM8990_DIGITAL_SIDE_TONE,
			    WM8990_ADC_TO_DACL_SHIFT,
			    wm8990_digital_sidetone);

static SOC_ENUM_SINGLE_DECL(wm8990_right_digital_sidetone_enum,
			    WM8990_DIGITAL_SIDE_TONE,
			    WM8990_ADC_TO_DACR_SHIFT,
			    wm8990_digital_sidetone);

static const char *wm8990_adcmode[] =
	{"Hi-fi mode", "Voice mode 1", "Voice mode 2", "Voice mode 3"};

static SOC_ENUM_SINGLE_DECL(wm8990_right_adcmode_enum,
			    WM8990_ADC_CTRL,
			    WM8990_ADC_HPF_CUT_SHIFT,
			    wm8990_adcmode);

static const struct snd_kcontrol_new wm8990_snd_controls[] = {
/* INMIXL */
SOC_SINGLE("LIN12 PGA Boost", WM8990_INPUT_MIXER3, WM8990_L12MNBST_BIT, 1, 0),
SOC_SINGLE("LIN34 PGA Boost", WM8990_INPUT_MIXER3, WM8990_L34MNBST_BIT, 1, 0),
/* INMIXR */
SOC_SINGLE("RIN12 PGA Boost", WM8990_INPUT_MIXER3, WM8990_R12MNBST_BIT, 1, 0),
SOC_SINGLE("RIN34 PGA Boost", WM8990_INPUT_MIXER3, WM8990_R34MNBST_BIT, 1, 0),

/* LOMIX */
SOC_SINGLE_TLV("LOMIX LIN3 Bypass Volume", WM8990_OUTPUT_MIXER3,
	WM8990_LLI3LOVOL_SHIFT, WM8990_LLI3LOVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX RIN12 PGA Bypass Volume", WM8990_OUTPUT_MIXER3,
	WM8990_LR12LOVOL_SHIFT, WM8990_LR12LOVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX LIN12 PGA Bypass Volume", WM8990_OUTPUT_MIXER3,
	WM8990_LL12LOVOL_SHIFT, WM8990_LL12LOVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX RIN3 Bypass Volume", WM8990_OUTPUT_MIXER5,
	WM8990_LRI3LOVOL_SHIFT, WM8990_LRI3LOVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX AINRMUX Bypass Volume", WM8990_OUTPUT_MIXER5,
	WM8990_LRBLOVOL_SHIFT, WM8990_LRBLOVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("LOMIX AINLMUX Bypass Volume", WM8990_OUTPUT_MIXER5,
	WM8990_LRBLOVOL_SHIFT, WM8990_LRBLOVOL_MASK, 1, out_mix_tlv),

/* ROMIX */
SOC_SINGLE_TLV("ROMIX RIN3 Bypass Volume", WM8990_OUTPUT_MIXER4,
	WM8990_RRI3ROVOL_SHIFT, WM8990_RRI3ROVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX LIN12 PGA Bypass Volume", WM8990_OUTPUT_MIXER4,
	WM8990_RL12ROVOL_SHIFT, WM8990_RL12ROVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX RIN12 PGA Bypass Volume", WM8990_OUTPUT_MIXER4,
	WM8990_RR12ROVOL_SHIFT, WM8990_RR12ROVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX LIN3 Bypass Volume", WM8990_OUTPUT_MIXER6,
	WM8990_RLI3ROVOL_SHIFT, WM8990_RLI3ROVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX AINLMUX Bypass Volume", WM8990_OUTPUT_MIXER6,
	WM8990_RLBROVOL_SHIFT, WM8990_RLBROVOL_MASK, 1, out_mix_tlv),
SOC_SINGLE_TLV("ROMIX AINRMUX Bypass Volume", WM8990_OUTPUT_MIXER6,
	WM8990_RRBROVOL_SHIFT, WM8990_RRBROVOL_MASK, 1, out_mix_tlv),

/* LOUT */
SOC_WM899X_OUTPGA_SINGLE_R_TLV("LOUT Volume", WM8990_LEFT_OUTPUT_VOLUME,
	WM8990_LOUTVOL_SHIFT, WM8990_LOUTVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("LOUT ZC", WM8990_LEFT_OUTPUT_VOLUME, WM8990_LOZC_BIT, 1, 0),

/* ROUT */
SOC_WM899X_OUTPGA_SINGLE_R_TLV("ROUT Volume", WM8990_RIGHT_OUTPUT_VOLUME,
	WM8990_ROUTVOL_SHIFT, WM8990_ROUTVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("ROUT ZC", WM8990_RIGHT_OUTPUT_VOLUME, WM8990_ROZC_BIT, 1, 0),

/* LOPGA */
SOC_WM899X_OUTPGA_SINGLE_R_TLV("LOPGA Volume", WM8990_LEFT_OPGA_VOLUME,
	WM8990_LOPGAVOL_SHIFT, WM8990_LOPGAVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("LOPGA ZC Switch", WM8990_LEFT_OPGA_VOLUME,
	WM8990_LOPGAZC_BIT, 1, 0),

/* ROPGA */
SOC_WM899X_OUTPGA_SINGLE_R_TLV("ROPGA Volume", WM8990_RIGHT_OPGA_VOLUME,
	WM8990_ROPGAVOL_SHIFT, WM8990_ROPGAVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("ROPGA ZC Switch", WM8990_RIGHT_OPGA_VOLUME,
	WM8990_ROPGAZC_BIT, 1, 0),

SOC_SINGLE("LON Mute Switch", WM8990_LINE_OUTPUTS_VOLUME,
	WM8990_LONMUTE_BIT, 1, 0),
SOC_SINGLE("LOP Mute Switch", WM8990_LINE_OUTPUTS_VOLUME,
	WM8990_LOPMUTE_BIT, 1, 0),
SOC_SINGLE("LOP Attenuation Switch", WM8990_LINE_OUTPUTS_VOLUME,
	WM8990_LOATTN_BIT, 1, 0),
SOC_SINGLE("RON Mute Switch", WM8990_LINE_OUTPUTS_VOLUME,
	WM8990_RONMUTE_BIT, 1, 0),
SOC_SINGLE("ROP Mute Switch", WM8990_LINE_OUTPUTS_VOLUME,
	WM8990_ROPMUTE_BIT, 1, 0),
SOC_SINGLE("ROP Attenuation Switch", WM8990_LINE_OUTPUTS_VOLUME,
	WM8990_ROATTN_BIT, 1, 0),

SOC_SINGLE("OUT3 Mute Switch", WM8990_OUT3_4_VOLUME,
	WM8990_OUT3MUTE_BIT, 1, 0),
SOC_SINGLE("OUT3 Attenuation Switch", WM8990_OUT3_4_VOLUME,
	WM8990_OUT3ATTN_BIT, 1, 0),

SOC_SINGLE("OUT4 Mute Switch", WM8990_OUT3_4_VOLUME,
	WM8990_OUT4MUTE_BIT, 1, 0),
SOC_SINGLE("OUT4 Attenuation Switch", WM8990_OUT3_4_VOLUME,
	WM8990_OUT4ATTN_BIT, 1, 0),

SOC_SINGLE("Speaker Mode Switch", WM8990_CLASSD1,
	WM8990_CDMODE_BIT, 1, 0),

SOC_SINGLE("Speaker Output Attenuation Volume", WM8990_SPEAKER_VOLUME,
	WM8990_SPKATTN_SHIFT, WM8990_SPKATTN_MASK, 0),
SOC_SINGLE("Speaker DC Boost Volume", WM8990_CLASSD3,
	WM8990_DCGAIN_SHIFT, WM8990_DCGAIN_MASK, 0),
SOC_SINGLE("Speaker AC Boost Volume", WM8990_CLASSD3,
	WM8990_ACGAIN_SHIFT, WM8990_ACGAIN_MASK, 0),
SOC_SINGLE_TLV("Speaker Volume", WM8990_CLASSD4,
	WM8990_SPKVOL_SHIFT, WM8990_SPKVOL_MASK, 0, out_pga_tlv),
SOC_SINGLE("Speaker ZC Switch", WM8990_CLASSD4,
	WM8990_SPKZC_SHIFT, WM8990_SPKZC_MASK, 0),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("Left DAC Digital Volume",
	WM8990_LEFT_DAC_DIGITAL_VOLUME,
	WM8990_DACL_VOL_SHIFT,
	WM8990_DACL_VOL_MASK,
	0,
	out_dac_tlv),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("Right DAC Digital Volume",
	WM8990_RIGHT_DAC_DIGITAL_VOLUME,
	WM8990_DACR_VOL_SHIFT,
	WM8990_DACR_VOL_MASK,
	0,
	out_dac_tlv),

SOC_ENUM("Left Digital Sidetone", wm8990_left_digital_sidetone_enum),
SOC_ENUM("Right Digital Sidetone", wm8990_right_digital_sidetone_enum),

SOC_SINGLE_TLV("Left Digital Sidetone Volume", WM8990_DIGITAL_SIDE_TONE,
	WM8990_ADCL_DAC_SVOL_SHIFT, WM8990_ADCL_DAC_SVOL_MASK, 0,
	out_sidetone_tlv),
SOC_SINGLE_TLV("Right Digital Sidetone Volume", WM8990_DIGITAL_SIDE_TONE,
	WM8990_ADCR_DAC_SVOL_SHIFT, WM8990_ADCR_DAC_SVOL_MASK, 0,
	out_sidetone_tlv),

SOC_SINGLE("ADC Digital High Pass Filter Switch", WM8990_ADC_CTRL,
	WM8990_ADC_HPF_ENA_BIT, 1, 0),

SOC_ENUM("ADC HPF Mode", wm8990_right_adcmode_enum),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("Left ADC Digital Volume",
	WM8990_LEFT_ADC_DIGITAL_VOLUME,
	WM8990_ADCL_VOL_SHIFT,
	WM8990_ADCL_VOL_MASK,
	0,
	in_adc_tlv),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("Right ADC Digital Volume",
	WM8990_RIGHT_ADC_DIGITAL_VOLUME,
	WM8990_ADCR_VOL_SHIFT,
	WM8990_ADCR_VOL_MASK,
	0,
	in_adc_tlv),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("LIN12 Volume",
	WM8990_LEFT_LINE_INPUT_1_2_VOLUME,
	WM8990_LIN12VOL_SHIFT,
	WM8990_LIN12VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("LIN12 ZC Switch", WM8990_LEFT_LINE_INPUT_1_2_VOLUME,
	WM8990_LI12ZC_BIT, 1, 0),

SOC_SINGLE("LIN12 Mute Switch", WM8990_LEFT_LINE_INPUT_1_2_VOLUME,
	WM8990_LI12MUTE_BIT, 1, 0),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("LIN34 Volume",
	WM8990_LEFT_LINE_INPUT_3_4_VOLUME,
	WM8990_LIN34VOL_SHIFT,
	WM8990_LIN34VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("LIN34 ZC Switch", WM8990_LEFT_LINE_INPUT_3_4_VOLUME,
	WM8990_LI34ZC_BIT, 1, 0),

SOC_SINGLE("LIN34 Mute Switch", WM8990_LEFT_LINE_INPUT_3_4_VOLUME,
	WM8990_LI34MUTE_BIT, 1, 0),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("RIN12 Volume",
	WM8990_RIGHT_LINE_INPUT_1_2_VOLUME,
	WM8990_RIN12VOL_SHIFT,
	WM8990_RIN12VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("RIN12 ZC Switch", WM8990_RIGHT_LINE_INPUT_1_2_VOLUME,
	WM8990_RI12ZC_BIT, 1, 0),

SOC_SINGLE("RIN12 Mute Switch", WM8990_RIGHT_LINE_INPUT_1_2_VOLUME,
	WM8990_RI12MUTE_BIT, 1, 0),

SOC_WM899X_OUTPGA_SINGLE_R_TLV("RIN34 Volume",
	WM8990_RIGHT_LINE_INPUT_3_4_VOLUME,
	WM8990_RIN34VOL_SHIFT,
	WM8990_RIN34VOL_MASK,
	0,
	in_pga_tlv),

SOC_SINGLE("RIN34 ZC Switch", WM8990_RIGHT_LINE_INPUT_3_4_VOLUME,
	WM8990_RI34ZC_BIT, 1, 0),

SOC_SINGLE("RIN34 Mute Switch", WM8990_RIGHT_LINE_INPUT_3_4_VOLUME,
	WM8990_RI34MUTE_BIT, 1, 0),

};

/*
 * _DAPM_ Controls
 */

static int outmixer_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	u32 reg_shift = kcontrol->private_value & 0xfff;
	int ret = 0;
	u16 reg;

	switch (reg_shift) {
	case WM8990_SPEAKER_MIXER | (WM8990_LDSPK_BIT << 8) :
		reg = snd_soc_component_read(component, WM8990_OUTPUT_MIXER1);
		if (reg & WM8990_LDLO) {
			printk(KERN_WARNING
			"Cannot set as Output Mixer 1 LDLO Set\n");
			ret = -1;
		}
		break;
	case WM8990_SPEAKER_MIXER | (WM8990_RDSPK_BIT << 8):
		reg = snd_soc_component_read(component, WM8990_OUTPUT_MIXER2);
		if (reg & WM8990_RDRO) {
			printk(KERN_WARNING
			"Cannot set as Output Mixer 2 RDRO Set\n");
			ret = -1;
		}
		break;
	case WM8990_OUTPUT_MIXER1 | (WM8990_LDLO_BIT << 8):
		reg = snd_soc_component_read(component, WM8990_SPEAKER_MIXER);
		if (reg & WM8990_LDSPK) {
			printk(KERN_WARNING
			"Cannot set as Speaker Mixer LDSPK Set\n");
			ret = -1;
		}
		break;
	case WM8990_OUTPUT_MIXER2 | (WM8990_RDRO_BIT << 8):
		reg = snd_soc_component_read(component, WM8990_SPEAKER_MIXER);
		if (reg & WM8990_RDSPK) {
			printk(KERN_WARNING
			"Cannot set as Speaker Mixer RDSPK Set\n");
			ret = -1;
		}
		break;
	}

	return ret;
}

/* INMIX dB values */
static const DECLARE_TLV_DB_SCALE(in_mix_tlv, -1200, 600, 0);

/* Left In PGA Connections */
static const struct snd_kcontrol_new wm8990_dapm_lin12_pga_controls[] = {
SOC_DAPM_SINGLE("LIN1 Switch", WM8990_INPUT_MIXER2, WM8990_LMN1_BIT, 1, 0),
SOC_DAPM_SINGLE("LIN2 Switch", WM8990_INPUT_MIXER2, WM8990_LMP2_BIT, 1, 0),
};

static const struct snd_kcontrol_new wm8990_dapm_lin34_pga_controls[] = {
SOC_DAPM_SINGLE("LIN3 Switch", WM8990_INPUT_MIXER2, WM8990_LMN3_BIT, 1, 0),
SOC_DAPM_SINGLE("LIN4 Switch", WM8990_INPUT_MIXER2, WM8990_LMP4_BIT, 1, 0),
};

/* Right In PGA Connections */
static const struct snd_kcontrol_new wm8990_dapm_rin12_pga_controls[] = {
SOC_DAPM_SINGLE("RIN1 Switch", WM8990_INPUT_MIXER2, WM8990_RMN1_BIT, 1, 0),
SOC_DAPM_SINGLE("RIN2 Switch", WM8990_INPUT_MIXER2, WM8990_RMP2_BIT, 1, 0),
};

static const struct snd_kcontrol_new wm8990_dapm_rin34_pga_controls[] = {
SOC_DAPM_SINGLE("RIN3 Switch", WM8990_INPUT_MIXER2, WM8990_RMN3_BIT, 1, 0),
SOC_DAPM_SINGLE("RIN4 Switch", WM8990_INPUT_MIXER2, WM8990_RMP4_BIT, 1, 0),
};

/* INMIXL */
static const struct snd_kcontrol_new wm8990_dapm_inmixl_controls[] = {
SOC_DAPM_SINGLE_TLV("Record Left Volume", WM8990_INPUT_MIXER3,
	WM8990_LDBVOL_SHIFT, WM8990_LDBVOL_MASK, 0, in_mix_tlv),
SOC_DAPM_SINGLE_TLV("LIN2 Volume", WM8990_INPUT_MIXER5, WM8990_LI2BVOL_SHIFT,
	7, 0, in_mix_tlv),
SOC_DAPM_SINGLE("LINPGA12 Switch", WM8990_INPUT_MIXER3, WM8990_L12MNB_BIT,
	1, 0),
SOC_DAPM_SINGLE("LINPGA34 Switch", WM8990_INPUT_MIXER3, WM8990_L34MNB_BIT,
	1, 0),
};

/* INMIXR */
static const struct snd_kcontrol_new wm8990_dapm_inmixr_controls[] = {
SOC_DAPM_SINGLE_TLV("Record Right Volume", WM8990_INPUT_MIXER4,
	WM8990_RDBVOL_SHIFT, WM8990_RDBVOL_MASK, 0, in_mix_tlv),
SOC_DAPM_SINGLE_TLV("RIN2 Volume", WM8990_INPUT_MIXER6, WM8990_RI2BVOL_SHIFT,
	7, 0, in_mix_tlv),
SOC_DAPM_SINGLE("RINPGA12 Switch", WM8990_INPUT_MIXER3, WM8990_L12MNB_BIT,
	1, 0),
SOC_DAPM_SINGLE("RINPGA34 Switch", WM8990_INPUT_MIXER3, WM8990_L34MNB_BIT,
	1, 0),
};

/* AINLMUX */
static const char *wm8990_ainlmux[] =
	{"INMIXL Mix", "RXVOICE Mix", "DIFFINL Mix"};

static SOC_ENUM_SINGLE_DECL(wm8990_ainlmux_enum,
			    WM8990_INPUT_MIXER1, WM8990_AINLMODE_SHIFT,
			    wm8990_ainlmux);

static const struct snd_kcontrol_new wm8990_dapm_ainlmux_controls =
SOC_DAPM_ENUM("Route", wm8990_ainlmux_enum);

/* DIFFINL */

/* AINRMUX */
static const char *wm8990_ainrmux[] =
	{"INMIXR Mix", "RXVOICE Mix", "DIFFINR Mix"};

static SOC_ENUM_SINGLE_DECL(wm8990_ainrmux_enum,
			    WM8990_INPUT_MIXER1, WM8990_AINRMODE_SHIFT,
			    wm8990_ainrmux);

static const struct snd_kcontrol_new wm8990_dapm_ainrmux_controls =
SOC_DAPM_ENUM("Route", wm8990_ainrmux_enum);

/* LOMIX */
static const struct snd_kcontrol_new wm8990_dapm_lomix_controls[] = {
SOC_DAPM_SINGLE("LOMIX Right ADC Bypass Switch", WM8990_OUTPUT_MIXER1,
	WM8990_LRBLO_BIT, 1, 0),
SOC_DAPM_SINGLE("LOMIX Left ADC Bypass Switch", WM8990_OUTPUT_MIXER1,
	WM8990_LLBLO_BIT, 1, 0),
SOC_DAPM_SINGLE("LOMIX RIN3 Bypass Switch", WM8990_OUTPUT_MIXER1,
	WM8990_LRI3LO_BIT, 1, 0),
SOC_DAPM_SINGLE("LOMIX LIN3 Bypass Switch", WM8990_OUTPUT_MIXER1,
	WM8990_LLI3LO_BIT, 1, 0),
SOC_DAPM_SINGLE("LOMIX RIN12 PGA Bypass Switch", WM8990_OUTPUT_MIXER1,
	WM8990_LR12LO_BIT, 1, 0),
SOC_DAPM_SINGLE("LOMIX LIN12 PGA Bypass Switch", WM8990_OUTPUT_MIXER1,
	WM8990_LL12LO_BIT, 1, 0),
SOC_DAPM_SINGLE("LOMIX Left DAC Switch", WM8990_OUTPUT_MIXER1,
	WM8990_LDLO_BIT, 1, 0),
};

/* ROMIX */
static const struct snd_kcontrol_new wm8990_dapm_romix_controls[] = {
SOC_DAPM_SINGLE("ROMIX Left ADC Bypass Switch", WM8990_OUTPUT_MIXER2,
	WM8990_RLBRO_BIT, 1, 0),
SOC_DAPM_SINGLE("ROMIX Right ADC Bypass Switch", WM8990_OUTPUT_MIXER2,
	WM8990_RRBRO_BIT, 1, 0),
SOC_DAPM_SINGLE("ROMIX LIN3 Bypass Switch", WM8990_OUTPUT_MIXER2,
	WM8990_RLI3RO_BIT, 1, 0),
SOC_DAPM_SINGLE("ROMIX RIN3 Bypass Switch", WM8990_OUTPUT_MIXER2,
	WM8990_RRI3RO_BIT, 1, 0),
SOC_DAPM_SINGLE("ROMIX LIN12 PGA Bypass Switch", WM8990_OUTPUT_MIXER2,
	WM8990_RL12RO_BIT, 1, 0),
SOC_DAPM_SINGLE("ROMIX RIN12 PGA Bypass Switch", WM8990_OUTPUT_MIXER2,
	WM8990_RR12RO_BIT, 1, 0),
SOC_DAPM_SINGLE("ROMIX Right DAC Switch", WM8990_OUTPUT_MIXER2,
	WM8990_RDRO_BIT, 1, 0),
};

/* LONMIX */
static const struct snd_kcontrol_new wm8990_dapm_lonmix_controls[] = {
SOC_DAPM_SINGLE("LONMIX Left Mixer PGA Switch", WM8990_LINE_MIXER1,
	WM8990_LLOPGALON_BIT, 1, 0),
SOC_DAPM_SINGLE("LONMIX Right Mixer PGA Switch", WM8990_LINE_MIXER1,
	WM8990_LROPGALON_BIT, 1, 0),
SOC_DAPM_SINGLE("LONMIX Inverted LOP Switch", WM8990_LINE_MIXER1,
	WM8990_LOPLON_BIT, 1, 0),
};

/* LOPMIX */
static const struct snd_kcontrol_new wm8990_dapm_lopmix_controls[] = {
SOC_DAPM_SINGLE("LOPMIX Right Mic Bypass Switch", WM8990_LINE_MIXER1,
	WM8990_LR12LOP_BIT, 1, 0),
SOC_DAPM_SINGLE("LOPMIX Left Mic Bypass Switch", WM8990_LINE_MIXER1,
	WM8990_LL12LOP_BIT, 1, 0),
SOC_DAPM_SINGLE("LOPMIX Left Mixer PGA Switch", WM8990_LINE_MIXER1,
	WM8990_LLOPGALOP_BIT, 1, 0),
};

/* RONMIX */
static const struct snd_kcontrol_new wm8990_dapm_ronmix_controls[] = {
SOC_DAPM_SINGLE("RONMIX Right Mixer PGA Switch", WM8990_LINE_MIXER2,
	WM8990_RROPGARON_BIT, 1, 0),
SOC_DAPM_SINGLE("RONMIX Left Mixer PGA Switch", WM8990_LINE_MIXER2,
	WM8990_RLOPGARON_BIT, 1, 0),
SOC_DAPM_SINGLE("RONMIX Inverted ROP Switch", WM8990_LINE_MIXER2,
	WM8990_ROPRON_BIT, 1, 0),
};

/* ROPMIX */
static const struct snd_kcontrol_new wm8990_dapm_ropmix_controls[] = {
SOC_DAPM_SINGLE("ROPMIX Left Mic Bypass Switch", WM8990_LINE_MIXER2,
	WM8990_RL12ROP_BIT, 1, 0),
SOC_DAPM_SINGLE("ROPMIX Right Mic Bypass Switch", WM8990_LINE_MIXER2,
	WM8990_RR12ROP_BIT, 1, 0),
SOC_DAPM_SINGLE("ROPMIX Right Mixer PGA Switch", WM8990_LINE_MIXER2,
	WM8990_RROPGAROP_BIT, 1, 0),
};

/* OUT3MIX */
static const struct snd_kcontrol_new wm8990_dapm_out3mix_controls[] = {
SOC_DAPM_SINGLE("OUT3MIX LIN4/RXP Bypass Switch", WM8990_OUT3_4_MIXER,
	WM8990_LI4O3_BIT, 1, 0),
SOC_DAPM_SINGLE("OUT3MIX Left Out PGA Switch", WM8990_OUT3_4_MIXER,
	WM8990_LPGAO3_BIT, 1, 0),
};

/* OUT4MIX */
static const struct snd_kcontrol_new wm8990_dapm_out4mix_controls[] = {
SOC_DAPM_SINGLE("OUT4MIX Right Out PGA Switch", WM8990_OUT3_4_MIXER,
	WM8990_RPGAO4_BIT, 1, 0),
SOC_DAPM_SINGLE("OUT4MIX RIN4/RXP Bypass Switch", WM8990_OUT3_4_MIXER,
	WM8990_RI4O4_BIT, 1, 0),
};

/* SPKMIX */
static const struct snd_kcontrol_new wm8990_dapm_spkmix_controls[] = {
SOC_DAPM_SINGLE("SPKMIX LIN2 Bypass Switch", WM8990_SPEAKER_MIXER,
	WM8990_LI2SPK_BIT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX LADC Bypass Switch", WM8990_SPEAKER_MIXER,
	WM8990_LB2SPK_BIT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Left Mixer PGA Switch", WM8990_SPEAKER_MIXER,
	WM8990_LOPGASPK_BIT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Left DAC Switch", WM8990_SPEAKER_MIXER,
	WM8990_LDSPK_BIT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Right DAC Switch", WM8990_SPEAKER_MIXER,
	WM8990_RDSPK_BIT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX Right Mixer PGA Switch", WM8990_SPEAKER_MIXER,
	WM8990_ROPGASPK_BIT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX RADC Bypass Switch", WM8990_SPEAKER_MIXER,
	WM8990_RL12ROP_BIT, 1, 0),
SOC_DAPM_SINGLE("SPKMIX RIN2 Bypass Switch", WM8990_SPEAKER_MIXER,
	WM8990_RI2SPK_BIT, 1, 0),
};

static const struct snd_soc_dapm_widget wm8990_dapm_widgets[] = {
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

SND_SOC_DAPM_SUPPLY("INL", WM8990_POWER_MANAGEMENT_2, WM8990_AINL_ENA_BIT, 0,
		    NULL, 0),
SND_SOC_DAPM_SUPPLY("INR", WM8990_POWER_MANAGEMENT_2, WM8990_AINR_ENA_BIT, 0,
		    NULL, 0),

/* DACs */
SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8990_POWER_MANAGEMENT_2,
	WM8990_ADCL_ENA_BIT, 0),
SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8990_POWER_MANAGEMENT_2,
	WM8990_ADCR_ENA_BIT, 0),

/* Input PGAs */
SND_SOC_DAPM_MIXER("LIN12 PGA", WM8990_POWER_MANAGEMENT_2, WM8990_LIN12_ENA_BIT,
	0, &wm8990_dapm_lin12_pga_controls[0],
	ARRAY_SIZE(wm8990_dapm_lin12_pga_controls)),
SND_SOC_DAPM_MIXER("LIN34 PGA", WM8990_POWER_MANAGEMENT_2, WM8990_LIN34_ENA_BIT,
	0, &wm8990_dapm_lin34_pga_controls[0],
	ARRAY_SIZE(wm8990_dapm_lin34_pga_controls)),
SND_SOC_DAPM_MIXER("RIN12 PGA", WM8990_POWER_MANAGEMENT_2, WM8990_RIN12_ENA_BIT,
	0, &wm8990_dapm_rin12_pga_controls[0],
	ARRAY_SIZE(wm8990_dapm_rin12_pga_controls)),
SND_SOC_DAPM_MIXER("RIN34 PGA", WM8990_POWER_MANAGEMENT_2, WM8990_RIN34_ENA_BIT,
	0, &wm8990_dapm_rin34_pga_controls[0],
	ARRAY_SIZE(wm8990_dapm_rin34_pga_controls)),

/* INMIXL */
SND_SOC_DAPM_MIXER("INMIXL", SND_SOC_NOPM, 0, 0,
	&wm8990_dapm_inmixl_controls[0],
	ARRAY_SIZE(wm8990_dapm_inmixl_controls)),

/* AINLMUX */
SND_SOC_DAPM_MUX("AINLMUX", SND_SOC_NOPM, 0, 0, &wm8990_dapm_ainlmux_controls),

/* INMIXR */
SND_SOC_DAPM_MIXER("INMIXR", SND_SOC_NOPM, 0, 0,
	&wm8990_dapm_inmixr_controls[0],
	ARRAY_SIZE(wm8990_dapm_inmixr_controls)),

/* AINRMUX */
SND_SOC_DAPM_MUX("AINRMUX", SND_SOC_NOPM, 0, 0, &wm8990_dapm_ainrmux_controls),

/* Output Side */
/* DACs */
SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8990_POWER_MANAGEMENT_3,
	WM8990_DACL_ENA_BIT, 0),
SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8990_POWER_MANAGEMENT_3,
	WM8990_DACR_ENA_BIT, 0),

/* LOMIX */
SND_SOC_DAPM_MIXER_E("LOMIX", WM8990_POWER_MANAGEMENT_3, WM8990_LOMIX_ENA_BIT,
	0, &wm8990_dapm_lomix_controls[0],
	ARRAY_SIZE(wm8990_dapm_lomix_controls),
	outmixer_event, SND_SOC_DAPM_PRE_REG),

/* LONMIX */
SND_SOC_DAPM_MIXER("LONMIX", WM8990_POWER_MANAGEMENT_3, WM8990_LON_ENA_BIT, 0,
	&wm8990_dapm_lonmix_controls[0],
	ARRAY_SIZE(wm8990_dapm_lonmix_controls)),

/* LOPMIX */
SND_SOC_DAPM_MIXER("LOPMIX", WM8990_POWER_MANAGEMENT_3, WM8990_LOP_ENA_BIT, 0,
	&wm8990_dapm_lopmix_controls[0],
	ARRAY_SIZE(wm8990_dapm_lopmix_controls)),

/* OUT3MIX */
SND_SOC_DAPM_MIXER("OUT3MIX", WM8990_POWER_MANAGEMENT_1, WM8990_OUT3_ENA_BIT, 0,
	&wm8990_dapm_out3mix_controls[0],
	ARRAY_SIZE(wm8990_dapm_out3mix_controls)),

/* SPKMIX */
SND_SOC_DAPM_MIXER_E("SPKMIX", WM8990_POWER_MANAGEMENT_1, WM8990_SPK_ENA_BIT, 0,
	&wm8990_dapm_spkmix_controls[0],
	ARRAY_SIZE(wm8990_dapm_spkmix_controls), outmixer_event,
	SND_SOC_DAPM_PRE_REG),

/* OUT4MIX */
SND_SOC_DAPM_MIXER("OUT4MIX", WM8990_POWER_MANAGEMENT_1, WM8990_OUT4_ENA_BIT, 0,
	&wm8990_dapm_out4mix_controls[0],
	ARRAY_SIZE(wm8990_dapm_out4mix_controls)),

/* ROPMIX */
SND_SOC_DAPM_MIXER("ROPMIX", WM8990_POWER_MANAGEMENT_3, WM8990_ROP_ENA_BIT, 0,
	&wm8990_dapm_ropmix_controls[0],
	ARRAY_SIZE(wm8990_dapm_ropmix_controls)),

/* RONMIX */
SND_SOC_DAPM_MIXER("RONMIX", WM8990_POWER_MANAGEMENT_3, WM8990_RON_ENA_BIT, 0,
	&wm8990_dapm_ronmix_controls[0],
	ARRAY_SIZE(wm8990_dapm_ronmix_controls)),

/* ROMIX */
SND_SOC_DAPM_MIXER_E("ROMIX", WM8990_POWER_MANAGEMENT_3, WM8990_ROMIX_ENA_BIT,
	0, &wm8990_dapm_romix_controls[0],
	ARRAY_SIZE(wm8990_dapm_romix_controls),
	outmixer_event, SND_SOC_DAPM_PRE_REG),

/* LOUT PGA */
SND_SOC_DAPM_PGA("LOUT PGA", WM8990_POWER_MANAGEMENT_1, WM8990_LOUT_ENA_BIT, 0,
	NULL, 0),

/* ROUT PGA */
SND_SOC_DAPM_PGA("ROUT PGA", WM8990_POWER_MANAGEMENT_1, WM8990_ROUT_ENA_BIT, 0,
	NULL, 0),

/* LOPGA */
SND_SOC_DAPM_PGA("LOPGA", WM8990_POWER_MANAGEMENT_3, WM8990_LOPGA_ENA_BIT, 0,
	NULL, 0),

/* ROPGA */
SND_SOC_DAPM_PGA("ROPGA", WM8990_POWER_MANAGEMENT_3, WM8990_ROPGA_ENA_BIT, 0,
	NULL, 0),

/* MICBIAS */
SND_SOC_DAPM_SUPPLY("MICBIAS", WM8990_POWER_MANAGEMENT_1,
		    WM8990_MICBIAS_ENA_BIT, 0, NULL, 0),

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

static const struct snd_soc_dapm_route wm8990_dapm_routes[] = {
	/* Make DACs turn on when playing even if not mixed into any outputs */
	{"Internal DAC Sink", NULL, "Left DAC"},
	{"Internal DAC Sink", NULL, "Right DAC"},

	/* Make ADCs turn on when recording even if not mixed from any inputs */
	{"Left ADC", NULL, "Internal ADC Source"},
	{"Right ADC", NULL, "Internal ADC Source"},

	{"AINLMUX", NULL, "INL"},
	{"INMIXL", NULL, "INL"},
	{"AINRMUX", NULL, "INR"},
	{"INMIXR", NULL, "INR"},

	/* Input Side */
	/* LIN12 PGA */
	{"LIN12 PGA", "LIN1 Switch", "LIN1"},
	{"LIN12 PGA", "LIN2 Switch", "LIN2"},
	/* LIN34 PGA */
	{"LIN34 PGA", "LIN3 Switch", "LIN3"},
	{"LIN34 PGA", "LIN4 Switch", "LIN4/RXN"},
	/* INMIXL */
	{"INMIXL", "Record Left Volume", "LOMIX"},
	{"INMIXL", "LIN2 Volume", "LIN2"},
	{"INMIXL", "LINPGA12 Switch", "LIN12 PGA"},
	{"INMIXL", "LINPGA34 Switch", "LIN34 PGA"},
	/* AINLMUX */
	{"AINLMUX", "INMIXL Mix", "INMIXL"},
	{"AINLMUX", "DIFFINL Mix", "LIN12 PGA"},
	{"AINLMUX", "DIFFINL Mix", "LIN34 PGA"},
	{"AINLMUX", "RXVOICE Mix", "LIN4/RXN"},
	{"AINLMUX", "RXVOICE Mix", "RIN4/RXP"},
	/* ADC */
	{"Left ADC", NULL, "AINLMUX"},

	/* RIN12 PGA */
	{"RIN12 PGA", "RIN1 Switch", "RIN1"},
	{"RIN12 PGA", "RIN2 Switch", "RIN2"},
	/* RIN34 PGA */
	{"RIN34 PGA", "RIN3 Switch", "RIN3"},
	{"RIN34 PGA", "RIN4 Switch", "RIN4/RXP"},
	/* INMIXL */
	{"INMIXR", "Record Right Volume", "ROMIX"},
	{"INMIXR", "RIN2 Volume", "RIN2"},
	{"INMIXR", "RINPGA12 Switch", "RIN12 PGA"},
	{"INMIXR", "RINPGA34 Switch", "RIN34 PGA"},
	/* AINRMUX */
	{"AINRMUX", "INMIXR Mix", "INMIXR"},
	{"AINRMUX", "DIFFINR Mix", "RIN12 PGA"},
	{"AINRMUX", "DIFFINR Mix", "RIN34 PGA"},
	{"AINRMUX", "RXVOICE Mix", "LIN4/RXN"},
	{"AINRMUX", "RXVOICE Mix", "RIN4/RXP"},
	/* ADC */
	{"Right ADC", NULL, "AINRMUX"},

	/* LOMIX */
	{"LOMIX", "LOMIX RIN3 Bypass Switch", "RIN3"},
	{"LOMIX", "LOMIX LIN3 Bypass Switch", "LIN3"},
	{"LOMIX", "LOMIX LIN12 PGA Bypass Switch", "LIN12 PGA"},
	{"LOMIX", "LOMIX RIN12 PGA Bypass Switch", "RIN12 PGA"},
	{"LOMIX", "LOMIX Right ADC Bypass Switch", "AINRMUX"},
	{"LOMIX", "LOMIX Left ADC Bypass Switch", "AINLMUX"},
	{"LOMIX", "LOMIX Left DAC Switch", "Left DAC"},

	/* ROMIX */
	{"ROMIX", "ROMIX RIN3 Bypass Switch", "RIN3"},
	{"ROMIX", "ROMIX LIN3 Bypass Switch", "LIN3"},
	{"ROMIX", "ROMIX LIN12 PGA Bypass Switch", "LIN12 PGA"},
	{"ROMIX", "ROMIX RIN12 PGA Bypass Switch", "RIN12 PGA"},
	{"ROMIX", "ROMIX Right ADC Bypass Switch", "AINRMUX"},
	{"ROMIX", "ROMIX Left ADC Bypass Switch", "AINLMUX"},
	{"ROMIX", "ROMIX Right DAC Switch", "Right DAC"},

	/* SPKMIX */
	{"SPKMIX", "SPKMIX LIN2 Bypass Switch", "LIN2"},
	{"SPKMIX", "SPKMIX RIN2 Bypass Switch", "RIN2"},
	{"SPKMIX", "SPKMIX LADC Bypass Switch", "AINLMUX"},
	{"SPKMIX", "SPKMIX RADC Bypass Switch", "AINRMUX"},
	{"SPKMIX", "SPKMIX Left Mixer PGA Switch", "LOPGA"},
	{"SPKMIX", "SPKMIX Right Mixer PGA Switch", "ROPGA"},
	{"SPKMIX", "SPKMIX Right DAC Switch", "Right DAC"},
	{"SPKMIX", "SPKMIX Left DAC Switch", "Left DAC"},

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

/* PLL divisors */
struct _pll_div {
	u32 div2;
	u32 n;
	u32 k;
};

/* The size in bits of the pll divide multiplied by 10
 * to allow rounding later */
#define FIXED_PLL_SIZE ((1 << 16) * 10)

static void pll_factors(struct _pll_div *pll_div, unsigned int target,
	unsigned int source)
{
	u64 Kpart;
	unsigned int K, Ndiv, Nmod;


	Ndiv = target / source;
	if (Ndiv < 6) {
		source >>= 1;
		pll_div->div2 = 1;
		Ndiv = target / source;
	} else
		pll_div->div2 = 0;

	if ((Ndiv < 6) || (Ndiv > 12))
		printk(KERN_WARNING
		"WM8990 N value outwith recommended range! N = %u\n", Ndiv);

	pll_div->n = Ndiv;
	Nmod = target % source;
	Kpart = FIXED_PLL_SIZE * (long long)Nmod;

	do_div(Kpart, source);

	K = Kpart & 0xFFFFFFFF;

	/* Check if we need to round */
	if ((K % 10) >= 5)
		K += 5;

	/* Move down to proper range now rounding is done */
	K /= 10;

	pll_div->k = K;
}

static int wm8990_set_dai_pll(struct snd_soc_dai *codec_dai, int pll_id,
		int source, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = codec_dai->component;
	struct _pll_div pll_div;

	if (freq_in && freq_out) {
		pll_factors(&pll_div, freq_out * 4, freq_in);

		/* Turn on PLL */
		snd_soc_component_update_bits(component, WM8990_POWER_MANAGEMENT_2,
				    WM8990_PLL_ENA, WM8990_PLL_ENA);

		/* sysclk comes from PLL */
		snd_soc_component_update_bits(component, WM8990_CLOCKING_2,
				    WM8990_SYSCLK_SRC, WM8990_SYSCLK_SRC);

		/* set up N , fractional mode and pre-divisor if necessary */
		snd_soc_component_write(component, WM8990_PLL1, pll_div.n | WM8990_SDM |
			(pll_div.div2?WM8990_PRESCALE:0));
		snd_soc_component_write(component, WM8990_PLL2, (u8)(pll_div.k>>8));
		snd_soc_component_write(component, WM8990_PLL3, (u8)(pll_div.k & 0xFF));
	} else {
		/* Turn off PLL */
		snd_soc_component_update_bits(component, WM8990_POWER_MANAGEMENT_2,
				    WM8990_PLL_ENA, 0);
	}
	return 0;
}

/*
 * Clock after PLL and dividers
 */
static int wm8990_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = codec_dai->component;
	struct wm8990_priv *wm8990 = snd_soc_component_get_drvdata(component);

	wm8990->sysclk = freq;
	return 0;
}

/*
 * Set's ADC and Voice DAC format.
 */
static int wm8990_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 audio1, audio3;

	audio1 = snd_soc_component_read(component, WM8990_AUDIO_INTERFACE_1);
	audio3 = snd_soc_component_read(component, WM8990_AUDIO_INTERFACE_3);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		audio3 &= ~WM8990_AIF_MSTR1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		audio3 |= WM8990_AIF_MSTR1;
		break;
	default:
		return -EINVAL;
	}

	audio1 &= ~WM8990_AIF_FMT_MASK;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		audio1 |= WM8990_AIF_TMF_I2S;
		audio1 &= ~WM8990_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		audio1 |= WM8990_AIF_TMF_RIGHTJ;
		audio1 &= ~WM8990_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		audio1 |= WM8990_AIF_TMF_LEFTJ;
		audio1 &= ~WM8990_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		audio1 |= WM8990_AIF_TMF_DSP;
		audio1 &= ~WM8990_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		audio1 |= WM8990_AIF_TMF_DSP | WM8990_AIF_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, WM8990_AUDIO_INTERFACE_1, audio1);
	snd_soc_component_write(component, WM8990_AUDIO_INTERFACE_3, audio3);
	return 0;
}

static int wm8990_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_component *component = codec_dai->component;

	switch (div_id) {
	case WM8990_MCLK_DIV:
		snd_soc_component_update_bits(component, WM8990_CLOCKING_2,
				    WM8990_MCLK_DIV_MASK, div);
		break;
	case WM8990_DACCLK_DIV:
		snd_soc_component_update_bits(component, WM8990_CLOCKING_2,
				    WM8990_DAC_CLKDIV_MASK, div);
		break;
	case WM8990_ADCCLK_DIV:
		snd_soc_component_update_bits(component, WM8990_CLOCKING_2,
				    WM8990_ADC_CLKDIV_MASK, div);
		break;
	case WM8990_BCLK_DIV:
		snd_soc_component_update_bits(component, WM8990_CLOCKING_1,
				    WM8990_BCLK_DIV_MASK, div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8990_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u16 audio1 = snd_soc_component_read(component, WM8990_AUDIO_INTERFACE_1);

	audio1 &= ~WM8990_AIF_WL_MASK;
	/* bit size */
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		audio1 |= WM8990_AIF_WL_20BITS;
		break;
	case 24:
		audio1 |= WM8990_AIF_WL_24BITS;
		break;
	case 32:
		audio1 |= WM8990_AIF_WL_32BITS;
		break;
	}

	snd_soc_component_write(component, WM8990_AUDIO_INTERFACE_1, audio1);
	return 0;
}

static int wm8990_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	u16 val;

	val  = snd_soc_component_read(component, WM8990_DAC_CTRL) & ~WM8990_DAC_MUTE;

	if (mute)
		snd_soc_component_write(component, WM8990_DAC_CTRL, val | WM8990_DAC_MUTE);
	else
		snd_soc_component_write(component, WM8990_DAC_CTRL, val);

	return 0;
}

static int wm8990_set_bias_level(struct snd_soc_component *component,
	enum snd_soc_bias_level level)
{
	struct wm8990_priv *wm8990 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID=2*50k */
		snd_soc_component_update_bits(component, WM8990_POWER_MANAGEMENT_1,
				    WM8990_VMID_MODE_MASK, 0x2);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			ret = regcache_sync(wm8990->regmap);
			if (ret < 0) {
				dev_err(component->dev, "Failed to sync cache: %d\n", ret);
				return ret;
			}

			/* Enable all output discharge bits */
			snd_soc_component_write(component, WM8990_ANTIPOP1, WM8990_DIS_LLINE |
				WM8990_DIS_RLINE | WM8990_DIS_OUT3 |
				WM8990_DIS_OUT4 | WM8990_DIS_LOUT |
				WM8990_DIS_ROUT);

			/* Enable POBCTRL, SOFT_ST, VMIDTOG and BUFDCOPEN */
			snd_soc_component_write(component, WM8990_ANTIPOP2, WM8990_SOFTST |
				     WM8990_BUFDCOPEN | WM8990_POBCTRL |
				     WM8990_VMIDTOG);

			/* Delay to allow output caps to discharge */
			msleep(300);

			/* Disable VMIDTOG */
			snd_soc_component_write(component, WM8990_ANTIPOP2, WM8990_SOFTST |
				     WM8990_BUFDCOPEN | WM8990_POBCTRL);

			/* disable all output discharge bits */
			snd_soc_component_write(component, WM8990_ANTIPOP1, 0);

			/* Enable outputs */
			snd_soc_component_write(component, WM8990_POWER_MANAGEMENT_1, 0x1b00);

			msleep(50);

			/* Enable VMID at 2x50k */
			snd_soc_component_write(component, WM8990_POWER_MANAGEMENT_1, 0x1f02);

			msleep(100);

			/* Enable VREF */
			snd_soc_component_write(component, WM8990_POWER_MANAGEMENT_1, 0x1f03);

			msleep(600);

			/* Enable BUFIOEN */
			snd_soc_component_write(component, WM8990_ANTIPOP2, WM8990_SOFTST |
				     WM8990_BUFDCOPEN | WM8990_POBCTRL |
				     WM8990_BUFIOEN);

			/* Disable outputs */
			snd_soc_component_write(component, WM8990_POWER_MANAGEMENT_1, 0x3);

			/* disable POBCTRL, SOFT_ST and BUFDCOPEN */
			snd_soc_component_write(component, WM8990_ANTIPOP2, WM8990_BUFIOEN);

			/* Enable workaround for ADC clocking issue. */
			snd_soc_component_write(component, WM8990_EXT_ACCESS_ENA, 0x2);
			snd_soc_component_write(component, WM8990_EXT_CTL1, 0xa003);
			snd_soc_component_write(component, WM8990_EXT_ACCESS_ENA, 0);
		}

		/* VMID=2*250k */
		snd_soc_component_update_bits(component, WM8990_POWER_MANAGEMENT_1,
				    WM8990_VMID_MODE_MASK, 0x4);
		break;

	case SND_SOC_BIAS_OFF:
		/* Enable POBCTRL and SOFT_ST */
		snd_soc_component_write(component, WM8990_ANTIPOP2, WM8990_SOFTST |
			WM8990_POBCTRL | WM8990_BUFIOEN);

		/* Enable POBCTRL, SOFT_ST and BUFDCOPEN */
		snd_soc_component_write(component, WM8990_ANTIPOP2, WM8990_SOFTST |
			WM8990_BUFDCOPEN | WM8990_POBCTRL |
			WM8990_BUFIOEN);

		/* mute DAC */
		snd_soc_component_update_bits(component, WM8990_DAC_CTRL,
				    WM8990_DAC_MUTE, WM8990_DAC_MUTE);

		/* Enable any disabled outputs */
		snd_soc_component_write(component, WM8990_POWER_MANAGEMENT_1, 0x1f03);

		/* Disable VMID */
		snd_soc_component_write(component, WM8990_POWER_MANAGEMENT_1, 0x1f01);

		msleep(300);

		/* Enable all output discharge bits */
		snd_soc_component_write(component, WM8990_ANTIPOP1, WM8990_DIS_LLINE |
			WM8990_DIS_RLINE | WM8990_DIS_OUT3 |
			WM8990_DIS_OUT4 | WM8990_DIS_LOUT |
			WM8990_DIS_ROUT);

		/* Disable VREF */
		snd_soc_component_write(component, WM8990_POWER_MANAGEMENT_1, 0x0);

		/* disable POBCTRL, SOFT_ST and BUFDCOPEN */
		snd_soc_component_write(component, WM8990_ANTIPOP2, 0x0);

		regcache_mark_dirty(wm8990->regmap);
		break;
	}

	return 0;
}

#define WM8990_RATES (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |\
	SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050 | SNDRV_PCM_RATE_44100 | \
	SNDRV_PCM_RATE_48000)

#define WM8990_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

/*
 * The WM8990 supports 2 different and mutually exclusive DAI
 * configurations.
 *
 * 1. ADC/DAC on Primary Interface
 * 2. ADC on Primary Interface/DAC on secondary
 */
static const struct snd_soc_dai_ops wm8990_dai_ops = {
	.hw_params	= wm8990_hw_params,
	.mute_stream	= wm8990_mute,
	.set_fmt	= wm8990_set_dai_fmt,
	.set_clkdiv	= wm8990_set_dai_clkdiv,
	.set_pll	= wm8990_set_dai_pll,
	.set_sysclk	= wm8990_set_dai_sysclk,
	.no_capture_mute = 1,
};

static struct snd_soc_dai_driver wm8990_dai = {
/* ADC/DAC on primary */
	.name = "wm8990-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8990_RATES,
		.formats = WM8990_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8990_RATES,
		.formats = WM8990_FORMATS,},
	.ops = &wm8990_dai_ops,
};

/*
 * initialise the WM8990 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int wm8990_probe(struct snd_soc_component *component)
{
	wm8990_reset(component);

	/* charge output caps */
	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_STANDBY);

	snd_soc_component_update_bits(component, WM8990_AUDIO_INTERFACE_4,
			    WM8990_ALRCGPIO1, WM8990_ALRCGPIO1);

	snd_soc_component_update_bits(component, WM8990_GPIO1_GPIO2,
			    WM8990_GPIO1_SEL_MASK, 1);

	snd_soc_component_update_bits(component, WM8990_POWER_MANAGEMENT_2,
			    WM8990_OPCLK_ENA, WM8990_OPCLK_ENA);

	snd_soc_component_write(component, WM8990_LEFT_OUTPUT_VOLUME, 0x50 | (1<<8));
	snd_soc_component_write(component, WM8990_RIGHT_OUTPUT_VOLUME, 0x50 | (1<<8));

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_wm8990 = {
	.probe			= wm8990_probe,
	.set_bias_level		= wm8990_set_bias_level,
	.controls		= wm8990_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8990_snd_controls),
	.dapm_widgets		= wm8990_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8990_dapm_widgets),
	.dapm_routes		= wm8990_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8990_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int wm8990_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct wm8990_priv *wm8990;
	int ret;

	wm8990 = devm_kzalloc(&i2c->dev, sizeof(struct wm8990_priv),
			      GFP_KERNEL);
	if (wm8990 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, wm8990);

	ret = devm_snd_soc_register_component(&i2c->dev,
			&soc_component_dev_wm8990, &wm8990_dai, 1);

	return ret;
}

static const struct i2c_device_id wm8990_i2c_id[] = {
	{ "wm8990", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8990_i2c_id);

static struct i2c_driver wm8990_i2c_driver = {
	.driver = {
		.name = "wm8990",
	},
	.probe =    wm8990_i2c_probe,
	.id_table = wm8990_i2c_id,
};

module_i2c_driver(wm8990_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8990 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
