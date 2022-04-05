// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * wm8991.c  --  WM8991 ALSA Soc Audio driver
 *
 * Copyright 2007-2010 Wolfson Microelectronics PLC.
 * Author: Graeme Gregory
 *         Graeme.Gregory@wolfsonmicro.com
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
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/div64.h>

#include "wm8991.h"

struct wm8991_priv {
	struct regmap *regmap;
	unsigned int pcmclk;
};

static const struct reg_default wm8991_reg_defaults[] = {
	{  1, 0x0000 },     /* R1  - Power Management (1) */
	{  2, 0x6000 },     /* R2  - Power Management (2) */
	{  3, 0x0000 },     /* R3  - Power Management (3) */
	{  4, 0x4050 },     /* R4  - Audio Interface (1) */
	{  5, 0x4000 },     /* R5  - Audio Interface (2) */
	{  6, 0x01C8 },     /* R6  - Clocking (1) */
	{  7, 0x0000 },     /* R7  - Clocking (2) */
	{  8, 0x0040 },     /* R8  - Audio Interface (3) */
	{  9, 0x0040 },     /* R9  - Audio Interface (4) */
	{ 10, 0x0004 },     /* R10 - DAC CTRL */
	{ 11, 0x00C0 },     /* R11 - Left DAC Digital Volume */
	{ 12, 0x00C0 },     /* R12 - Right DAC Digital Volume */
	{ 13, 0x0000 },     /* R13 - Digital Side Tone */
	{ 14, 0x0100 },     /* R14 - ADC CTRL */
	{ 15, 0x00C0 },     /* R15 - Left ADC Digital Volume */
	{ 16, 0x00C0 },     /* R16 - Right ADC Digital Volume */

	{ 18, 0x0000 },     /* R18 - GPIO CTRL 1 */
	{ 19, 0x1000 },     /* R19 - GPIO1 & GPIO2 */
	{ 20, 0x1010 },     /* R20 - GPIO3 & GPIO4 */
	{ 21, 0x1010 },     /* R21 - GPIO5 & GPIO6 */
	{ 22, 0x8000 },     /* R22 - GPIOCTRL 2 */
	{ 23, 0x0800 },     /* R23 - GPIO_POL */
	{ 24, 0x008B },     /* R24 - Left Line Input 1&2 Volume */
	{ 25, 0x008B },     /* R25 - Left Line Input 3&4 Volume */
	{ 26, 0x008B },     /* R26 - Right Line Input 1&2 Volume */
	{ 27, 0x008B },     /* R27 - Right Line Input 3&4 Volume */
	{ 28, 0x0000 },     /* R28 - Left Output Volume */
	{ 29, 0x0000 },     /* R29 - Right Output Volume */
	{ 30, 0x0066 },     /* R30 - Line Outputs Volume */
	{ 31, 0x0022 },     /* R31 - Out3/4 Volume */
	{ 32, 0x0079 },     /* R32 - Left OPGA Volume */
	{ 33, 0x0079 },     /* R33 - Right OPGA Volume */
	{ 34, 0x0003 },     /* R34 - Speaker Volume */
	{ 35, 0x0003 },     /* R35 - ClassD1 */

	{ 37, 0x0100 },     /* R37 - ClassD3 */

	{ 39, 0x0000 },     /* R39 - Input Mixer1 */
	{ 40, 0x0000 },     /* R40 - Input Mixer2 */
	{ 41, 0x0000 },     /* R41 - Input Mixer3 */
	{ 42, 0x0000 },     /* R42 - Input Mixer4 */
	{ 43, 0x0000 },     /* R43 - Input Mixer5 */
	{ 44, 0x0000 },     /* R44 - Input Mixer6 */
	{ 45, 0x0000 },     /* R45 - Output Mixer1 */
	{ 46, 0x0000 },     /* R46 - Output Mixer2 */
	{ 47, 0x0000 },     /* R47 - Output Mixer3 */
	{ 48, 0x0000 },     /* R48 - Output Mixer4 */
	{ 49, 0x0000 },     /* R49 - Output Mixer5 */
	{ 50, 0x0000 },     /* R50 - Output Mixer6 */
	{ 51, 0x0180 },     /* R51 - Out3/4 Mixer */
	{ 52, 0x0000 },     /* R52 - Line Mixer1 */
	{ 53, 0x0000 },     /* R53 - Line Mixer2 */
	{ 54, 0x0000 },     /* R54 - Speaker Mixer */
	{ 55, 0x0000 },     /* R55 - Additional Control */
	{ 56, 0x0000 },     /* R56 - AntiPOP1 */
	{ 57, 0x0000 },     /* R57 - AntiPOP2 */
	{ 58, 0x0000 },     /* R58 - MICBIAS */

	{ 60, 0x0008 },     /* R60 - PLL1 */
	{ 61, 0x0031 },     /* R61 - PLL2 */
	{ 62, 0x0026 },     /* R62 - PLL3 */
};

static bool wm8991_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case WM8991_RESET:
		return true;
	default:
		return false;
	}
}

static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(in_pga_tlv, -1650, 150, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(out_mix_tlv, -2100, 300, 0);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(out_pga_tlv,
	0x00, 0x2f, SNDRV_CTL_TLVD_DB_SCALE_ITEM(SNDRV_CTL_TLVD_DB_GAIN_MUTE, 0, 1),
	0x30, 0x7f, SNDRV_CTL_TLVD_DB_SCALE_ITEM(-7300, 100, 0),
);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(out_dac_tlv,
	0x00, 0xbf, SNDRV_CTL_TLVD_DB_SCALE_ITEM(-71625, 375, 1),
	0xc0, 0xff, SNDRV_CTL_TLVD_DB_SCALE_ITEM(0, 0, 0),
);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(in_adc_tlv,
	0x00, 0xef, SNDRV_CTL_TLVD_DB_SCALE_ITEM(-71625, 375, 1),
	0xf0, 0xff, SNDRV_CTL_TLVD_DB_SCALE_ITEM(17625, 0, 0),
);
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(out_sidetone_tlv,
	0x00, 0x0c, SNDRV_CTL_TLVD_DB_SCALE_ITEM(-3600, 300, 0),
	0x0d, 0x0f, SNDRV_CTL_TLVD_DB_SCALE_ITEM(0, 0, 0),
);

static int wm899x_outpga_put_volsw_vu(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int ret;
	u16 val;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);
	if (ret < 0)
		return ret;

	/* now hit the volume update bits (always bit 8) */
	val = snd_soc_component_read(component, reg);
	return snd_soc_component_write(component, reg, val | 0x0100);
}

static const char *wm8991_digital_sidetone[] =
{"None", "Left ADC", "Right ADC", "Reserved"};

static SOC_ENUM_SINGLE_DECL(wm8991_left_digital_sidetone_enum,
			    WM8991_DIGITAL_SIDE_TONE,
			    WM8991_ADC_TO_DACL_SHIFT,
			    wm8991_digital_sidetone);

static SOC_ENUM_SINGLE_DECL(wm8991_right_digital_sidetone_enum,
			    WM8991_DIGITAL_SIDE_TONE,
			    WM8991_ADC_TO_DACR_SHIFT,
			    wm8991_digital_sidetone);

static const char *wm8991_adcmode[] =
{"Hi-fi mode", "Voice mode 1", "Voice mode 2", "Voice mode 3"};

static SOC_ENUM_SINGLE_DECL(wm8991_right_adcmode_enum,
			    WM8991_ADC_CTRL,
			    WM8991_ADC_HPF_CUT_SHIFT,
			    wm8991_adcmode);

static const struct snd_kcontrol_new wm8991_snd_controls[] = {
	/* INMIXL */
	SOC_SINGLE("LIN12 PGA Boost", WM8991_INPUT_MIXER3, WM8991_L12MNBST_BIT, 1, 0),
	SOC_SINGLE("LIN34 PGA Boost", WM8991_INPUT_MIXER3, WM8991_L34MNBST_BIT, 1, 0),
	/* INMIXR */
	SOC_SINGLE("RIN12 PGA Boost", WM8991_INPUT_MIXER3, WM8991_R12MNBST_BIT, 1, 0),
	SOC_SINGLE("RIN34 PGA Boost", WM8991_INPUT_MIXER3, WM8991_R34MNBST_BIT, 1, 0),

	/* LOMIX */
	SOC_SINGLE_TLV("LOMIX LIN3 Bypass Volume", WM8991_OUTPUT_MIXER3,
		WM8991_LLI3LOVOL_SHIFT, WM8991_LLI3LOVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("LOMIX RIN12 PGA Bypass Volume", WM8991_OUTPUT_MIXER3,
		WM8991_LR12LOVOL_SHIFT, WM8991_LR12LOVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("LOMIX LIN12 PGA Bypass Volume", WM8991_OUTPUT_MIXER3,
		WM8991_LL12LOVOL_SHIFT, WM8991_LL12LOVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("LOMIX RIN3 Bypass Volume", WM8991_OUTPUT_MIXER5,
		WM8991_LRI3LOVOL_SHIFT, WM8991_LRI3LOVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("LOMIX AINRMUX Bypass Volume", WM8991_OUTPUT_MIXER5,
		WM8991_LRBLOVOL_SHIFT, WM8991_LRBLOVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("LOMIX AINLMUX Bypass Volume", WM8991_OUTPUT_MIXER5,
		WM8991_LRBLOVOL_SHIFT, WM8991_LRBLOVOL_MASK, 1, out_mix_tlv),

	/* ROMIX */
	SOC_SINGLE_TLV("ROMIX RIN3 Bypass Volume", WM8991_OUTPUT_MIXER4,
		WM8991_RRI3ROVOL_SHIFT, WM8991_RRI3ROVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("ROMIX LIN12 PGA Bypass Volume", WM8991_OUTPUT_MIXER4,
		WM8991_RL12ROVOL_SHIFT, WM8991_RL12ROVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("ROMIX RIN12 PGA Bypass Volume", WM8991_OUTPUT_MIXER4,
		WM8991_RR12ROVOL_SHIFT, WM8991_RR12ROVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("ROMIX LIN3 Bypass Volume", WM8991_OUTPUT_MIXER6,
		WM8991_RLI3ROVOL_SHIFT, WM8991_RLI3ROVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("ROMIX AINLMUX Bypass Volume", WM8991_OUTPUT_MIXER6,
		WM8991_RLBROVOL_SHIFT, WM8991_RLBROVOL_MASK, 1, out_mix_tlv),
	SOC_SINGLE_TLV("ROMIX AINRMUX Bypass Volume", WM8991_OUTPUT_MIXER6,
		WM8991_RRBROVOL_SHIFT, WM8991_RRBROVOL_MASK, 1, out_mix_tlv),

	/* LOUT */
	SOC_WM899X_OUTPGA_SINGLE_R_TLV("LOUT Volume", WM8991_LEFT_OUTPUT_VOLUME,
		WM8991_LOUTVOL_SHIFT, WM8991_LOUTVOL_MASK, 0, out_pga_tlv),
	SOC_SINGLE("LOUT ZC", WM8991_LEFT_OUTPUT_VOLUME, WM8991_LOZC_BIT, 1, 0),

	/* ROUT */
	SOC_WM899X_OUTPGA_SINGLE_R_TLV("ROUT Volume", WM8991_RIGHT_OUTPUT_VOLUME,
		WM8991_ROUTVOL_SHIFT, WM8991_ROUTVOL_MASK, 0, out_pga_tlv),
	SOC_SINGLE("ROUT ZC", WM8991_RIGHT_OUTPUT_VOLUME, WM8991_ROZC_BIT, 1, 0),

	/* LOPGA */
	SOC_WM899X_OUTPGA_SINGLE_R_TLV("LOPGA Volume", WM8991_LEFT_OPGA_VOLUME,
		WM8991_LOPGAVOL_SHIFT, WM8991_LOPGAVOL_MASK, 0, out_pga_tlv),
	SOC_SINGLE("LOPGA ZC Switch", WM8991_LEFT_OPGA_VOLUME,
		WM8991_LOPGAZC_BIT, 1, 0),

	/* ROPGA */
	SOC_WM899X_OUTPGA_SINGLE_R_TLV("ROPGA Volume", WM8991_RIGHT_OPGA_VOLUME,
		WM8991_ROPGAVOL_SHIFT, WM8991_ROPGAVOL_MASK, 0, out_pga_tlv),
	SOC_SINGLE("ROPGA ZC Switch", WM8991_RIGHT_OPGA_VOLUME,
		WM8991_ROPGAZC_BIT, 1, 0),

	SOC_SINGLE("LON Mute Switch", WM8991_LINE_OUTPUTS_VOLUME,
		WM8991_LONMUTE_BIT, 1, 0),
	SOC_SINGLE("LOP Mute Switch", WM8991_LINE_OUTPUTS_VOLUME,
		WM8991_LOPMUTE_BIT, 1, 0),
	SOC_SINGLE("LOP Attenuation Switch", WM8991_LINE_OUTPUTS_VOLUME,
		WM8991_LOATTN_BIT, 1, 0),
	SOC_SINGLE("RON Mute Switch", WM8991_LINE_OUTPUTS_VOLUME,
		WM8991_RONMUTE_BIT, 1, 0),
	SOC_SINGLE("ROP Mute Switch", WM8991_LINE_OUTPUTS_VOLUME,
		WM8991_ROPMUTE_BIT, 1, 0),
	SOC_SINGLE("ROP Attenuation Switch", WM8991_LINE_OUTPUTS_VOLUME,
		WM8991_ROATTN_BIT, 1, 0),

	SOC_SINGLE("OUT3 Mute Switch", WM8991_OUT3_4_VOLUME,
		WM8991_OUT3MUTE_BIT, 1, 0),
	SOC_SINGLE("OUT3 Attenuation Switch", WM8991_OUT3_4_VOLUME,
		WM8991_OUT3ATTN_BIT, 1, 0),

	SOC_SINGLE("OUT4 Mute Switch", WM8991_OUT3_4_VOLUME,
		WM8991_OUT4MUTE_BIT, 1, 0),
	SOC_SINGLE("OUT4 Attenuation Switch", WM8991_OUT3_4_VOLUME,
		WM8991_OUT4ATTN_BIT, 1, 0),

	SOC_SINGLE("Speaker Mode Switch", WM8991_CLASSD1,
		WM8991_CDMODE_BIT, 1, 0),

	SOC_SINGLE("Speaker Output Attenuation Volume", WM8991_SPEAKER_VOLUME,
		WM8991_SPKVOL_SHIFT, WM8991_SPKVOL_MASK, 0),
	SOC_SINGLE("Speaker DC Boost Volume", WM8991_CLASSD3,
		WM8991_DCGAIN_SHIFT, WM8991_DCGAIN_MASK, 0),
	SOC_SINGLE("Speaker AC Boost Volume", WM8991_CLASSD3,
		WM8991_ACGAIN_SHIFT, WM8991_ACGAIN_MASK, 0),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("Left DAC Digital Volume",
		WM8991_LEFT_DAC_DIGITAL_VOLUME,
		WM8991_DACL_VOL_SHIFT,
		WM8991_DACL_VOL_MASK,
		0,
		out_dac_tlv),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("Right DAC Digital Volume",
		WM8991_RIGHT_DAC_DIGITAL_VOLUME,
		WM8991_DACR_VOL_SHIFT,
		WM8991_DACR_VOL_MASK,
		0,
		out_dac_tlv),

	SOC_ENUM("Left Digital Sidetone", wm8991_left_digital_sidetone_enum),
	SOC_ENUM("Right Digital Sidetone", wm8991_right_digital_sidetone_enum),

	SOC_SINGLE_TLV("Left Digital Sidetone Volume", WM8991_DIGITAL_SIDE_TONE,
		WM8991_ADCL_DAC_SVOL_SHIFT, WM8991_ADCL_DAC_SVOL_MASK, 0,
		out_sidetone_tlv),
	SOC_SINGLE_TLV("Right Digital Sidetone Volume", WM8991_DIGITAL_SIDE_TONE,
		WM8991_ADCR_DAC_SVOL_SHIFT, WM8991_ADCR_DAC_SVOL_MASK, 0,
		out_sidetone_tlv),

	SOC_SINGLE("ADC Digital High Pass Filter Switch", WM8991_ADC_CTRL,
		WM8991_ADC_HPF_ENA_BIT, 1, 0),

	SOC_ENUM("ADC HPF Mode", wm8991_right_adcmode_enum),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("Left ADC Digital Volume",
		WM8991_LEFT_ADC_DIGITAL_VOLUME,
		WM8991_ADCL_VOL_SHIFT,
		WM8991_ADCL_VOL_MASK,
		0,
		in_adc_tlv),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("Right ADC Digital Volume",
		WM8991_RIGHT_ADC_DIGITAL_VOLUME,
		WM8991_ADCR_VOL_SHIFT,
		WM8991_ADCR_VOL_MASK,
		0,
		in_adc_tlv),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("LIN12 Volume",
		WM8991_LEFT_LINE_INPUT_1_2_VOLUME,
		WM8991_LIN12VOL_SHIFT,
		WM8991_LIN12VOL_MASK,
		0,
		in_pga_tlv),

	SOC_SINGLE("LIN12 ZC Switch", WM8991_LEFT_LINE_INPUT_1_2_VOLUME,
		WM8991_LI12ZC_BIT, 1, 0),

	SOC_SINGLE("LIN12 Mute Switch", WM8991_LEFT_LINE_INPUT_1_2_VOLUME,
		WM8991_LI12MUTE_BIT, 1, 0),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("LIN34 Volume",
		WM8991_LEFT_LINE_INPUT_3_4_VOLUME,
		WM8991_LIN34VOL_SHIFT,
		WM8991_LIN34VOL_MASK,
		0,
		in_pga_tlv),

	SOC_SINGLE("LIN34 ZC Switch", WM8991_LEFT_LINE_INPUT_3_4_VOLUME,
		WM8991_LI34ZC_BIT, 1, 0),

	SOC_SINGLE("LIN34 Mute Switch", WM8991_LEFT_LINE_INPUT_3_4_VOLUME,
		WM8991_LI34MUTE_BIT, 1, 0),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("RIN12 Volume",
		WM8991_RIGHT_LINE_INPUT_1_2_VOLUME,
		WM8991_RIN12VOL_SHIFT,
		WM8991_RIN12VOL_MASK,
		0,
		in_pga_tlv),

	SOC_SINGLE("RIN12 ZC Switch", WM8991_RIGHT_LINE_INPUT_1_2_VOLUME,
		WM8991_RI12ZC_BIT, 1, 0),

	SOC_SINGLE("RIN12 Mute Switch", WM8991_RIGHT_LINE_INPUT_1_2_VOLUME,
		WM8991_RI12MUTE_BIT, 1, 0),

	SOC_WM899X_OUTPGA_SINGLE_R_TLV("RIN34 Volume",
		WM8991_RIGHT_LINE_INPUT_3_4_VOLUME,
		WM8991_RIN34VOL_SHIFT,
		WM8991_RIN34VOL_MASK,
		0,
		in_pga_tlv),

	SOC_SINGLE("RIN34 ZC Switch", WM8991_RIGHT_LINE_INPUT_3_4_VOLUME,
		WM8991_RI34ZC_BIT, 1, 0),

	SOC_SINGLE("RIN34 Mute Switch", WM8991_RIGHT_LINE_INPUT_3_4_VOLUME,
		WM8991_RI34MUTE_BIT, 1, 0),
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
	case WM8991_SPEAKER_MIXER | (WM8991_LDSPK_BIT << 8):
		reg = snd_soc_component_read(component, WM8991_OUTPUT_MIXER1);
		if (reg & WM8991_LDLO) {
			printk(KERN_WARNING
			       "Cannot set as Output Mixer 1 LDLO Set\n");
			ret = -1;
		}
		break;

	case WM8991_SPEAKER_MIXER | (WM8991_RDSPK_BIT << 8):
		reg = snd_soc_component_read(component, WM8991_OUTPUT_MIXER2);
		if (reg & WM8991_RDRO) {
			printk(KERN_WARNING
			       "Cannot set as Output Mixer 2 RDRO Set\n");
			ret = -1;
		}
		break;

	case WM8991_OUTPUT_MIXER1 | (WM8991_LDLO_BIT << 8):
		reg = snd_soc_component_read(component, WM8991_SPEAKER_MIXER);
		if (reg & WM8991_LDSPK) {
			printk(KERN_WARNING
			       "Cannot set as Speaker Mixer LDSPK Set\n");
			ret = -1;
		}
		break;

	case WM8991_OUTPUT_MIXER2 | (WM8991_RDRO_BIT << 8):
		reg = snd_soc_component_read(component, WM8991_SPEAKER_MIXER);
		if (reg & WM8991_RDSPK) {
			printk(KERN_WARNING
			       "Cannot set as Speaker Mixer RDSPK Set\n");
			ret = -1;
		}
		break;
	}

	return ret;
}

/* INMIX dB values */
static const SNDRV_CTL_TLVD_DECLARE_DB_SCALE(in_mix_tlv, -1200, 300, 1);

/* Left In PGA Connections */
static const struct snd_kcontrol_new wm8991_dapm_lin12_pga_controls[] = {
	SOC_DAPM_SINGLE("LIN1 Switch", WM8991_INPUT_MIXER2, WM8991_LMN1_BIT, 1, 0),
	SOC_DAPM_SINGLE("LIN2 Switch", WM8991_INPUT_MIXER2, WM8991_LMP2_BIT, 1, 0),
};

static const struct snd_kcontrol_new wm8991_dapm_lin34_pga_controls[] = {
	SOC_DAPM_SINGLE("LIN3 Switch", WM8991_INPUT_MIXER2, WM8991_LMN3_BIT, 1, 0),
	SOC_DAPM_SINGLE("LIN4 Switch", WM8991_INPUT_MIXER2, WM8991_LMP4_BIT, 1, 0),
};

/* Right In PGA Connections */
static const struct snd_kcontrol_new wm8991_dapm_rin12_pga_controls[] = {
	SOC_DAPM_SINGLE("RIN1 Switch", WM8991_INPUT_MIXER2, WM8991_RMN1_BIT, 1, 0),
	SOC_DAPM_SINGLE("RIN2 Switch", WM8991_INPUT_MIXER2, WM8991_RMP2_BIT, 1, 0),
};

static const struct snd_kcontrol_new wm8991_dapm_rin34_pga_controls[] = {
	SOC_DAPM_SINGLE("RIN3 Switch", WM8991_INPUT_MIXER2, WM8991_RMN3_BIT, 1, 0),
	SOC_DAPM_SINGLE("RIN4 Switch", WM8991_INPUT_MIXER2, WM8991_RMP4_BIT, 1, 0),
};

/* INMIXL */
static const struct snd_kcontrol_new wm8991_dapm_inmixl_controls[] = {
	SOC_DAPM_SINGLE_TLV("Record Left Volume", WM8991_INPUT_MIXER3,
		WM8991_LDBVOL_SHIFT, WM8991_LDBVOL_MASK, 0, in_mix_tlv),
	SOC_DAPM_SINGLE_TLV("LIN2 Volume", WM8991_INPUT_MIXER5, WM8991_LI2BVOL_SHIFT,
		7, 0, in_mix_tlv),
	SOC_DAPM_SINGLE("LINPGA12 Switch", WM8991_INPUT_MIXER3, WM8991_L12MNB_BIT,
		1, 0),
	SOC_DAPM_SINGLE("LINPGA34 Switch", WM8991_INPUT_MIXER3, WM8991_L34MNB_BIT,
		1, 0),
};

/* INMIXR */
static const struct snd_kcontrol_new wm8991_dapm_inmixr_controls[] = {
	SOC_DAPM_SINGLE_TLV("Record Right Volume", WM8991_INPUT_MIXER4,
		WM8991_RDBVOL_SHIFT, WM8991_RDBVOL_MASK, 0, in_mix_tlv),
	SOC_DAPM_SINGLE_TLV("RIN2 Volume", WM8991_INPUT_MIXER6, WM8991_RI2BVOL_SHIFT,
		7, 0, in_mix_tlv),
	SOC_DAPM_SINGLE("RINPGA12 Switch", WM8991_INPUT_MIXER3, WM8991_L12MNB_BIT,
		1, 0),
	SOC_DAPM_SINGLE("RINPGA34 Switch", WM8991_INPUT_MIXER3, WM8991_L34MNB_BIT,
		1, 0),
};

/* AINLMUX */
static const char *wm8991_ainlmux[] =
{"INMIXL Mix", "RXVOICE Mix", "DIFFINL Mix"};

static SOC_ENUM_SINGLE_DECL(wm8991_ainlmux_enum,
			    WM8991_INPUT_MIXER1, WM8991_AINLMODE_SHIFT,
			    wm8991_ainlmux);

static const struct snd_kcontrol_new wm8991_dapm_ainlmux_controls =
	SOC_DAPM_ENUM("Route", wm8991_ainlmux_enum);

/* DIFFINL */

/* AINRMUX */
static const char *wm8991_ainrmux[] =
{"INMIXR Mix", "RXVOICE Mix", "DIFFINR Mix"};

static SOC_ENUM_SINGLE_DECL(wm8991_ainrmux_enum,
			    WM8991_INPUT_MIXER1, WM8991_AINRMODE_SHIFT,
			    wm8991_ainrmux);

static const struct snd_kcontrol_new wm8991_dapm_ainrmux_controls =
	SOC_DAPM_ENUM("Route", wm8991_ainrmux_enum);

/* LOMIX */
static const struct snd_kcontrol_new wm8991_dapm_lomix_controls[] = {
	SOC_DAPM_SINGLE("LOMIX Right ADC Bypass Switch", WM8991_OUTPUT_MIXER1,
		WM8991_LRBLO_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOMIX Left ADC Bypass Switch", WM8991_OUTPUT_MIXER1,
		WM8991_LLBLO_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOMIX RIN3 Bypass Switch", WM8991_OUTPUT_MIXER1,
		WM8991_LRI3LO_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOMIX LIN3 Bypass Switch", WM8991_OUTPUT_MIXER1,
		WM8991_LLI3LO_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOMIX RIN12 PGA Bypass Switch", WM8991_OUTPUT_MIXER1,
		WM8991_LR12LO_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOMIX LIN12 PGA Bypass Switch", WM8991_OUTPUT_MIXER1,
		WM8991_LL12LO_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOMIX Left DAC Switch", WM8991_OUTPUT_MIXER1,
		WM8991_LDLO_BIT, 1, 0),
};

/* ROMIX */
static const struct snd_kcontrol_new wm8991_dapm_romix_controls[] = {
	SOC_DAPM_SINGLE("ROMIX Left ADC Bypass Switch", WM8991_OUTPUT_MIXER2,
		WM8991_RLBRO_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROMIX Right ADC Bypass Switch", WM8991_OUTPUT_MIXER2,
		WM8991_RRBRO_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROMIX LIN3 Bypass Switch", WM8991_OUTPUT_MIXER2,
		WM8991_RLI3RO_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROMIX RIN3 Bypass Switch", WM8991_OUTPUT_MIXER2,
		WM8991_RRI3RO_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROMIX LIN12 PGA Bypass Switch", WM8991_OUTPUT_MIXER2,
		WM8991_RL12RO_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROMIX RIN12 PGA Bypass Switch", WM8991_OUTPUT_MIXER2,
		WM8991_RR12RO_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROMIX Right DAC Switch", WM8991_OUTPUT_MIXER2,
		WM8991_RDRO_BIT, 1, 0),
};

/* LONMIX */
static const struct snd_kcontrol_new wm8991_dapm_lonmix_controls[] = {
	SOC_DAPM_SINGLE("LONMIX Left Mixer PGA Switch", WM8991_LINE_MIXER1,
		WM8991_LLOPGALON_BIT, 1, 0),
	SOC_DAPM_SINGLE("LONMIX Right Mixer PGA Switch", WM8991_LINE_MIXER1,
		WM8991_LROPGALON_BIT, 1, 0),
	SOC_DAPM_SINGLE("LONMIX Inverted LOP Switch", WM8991_LINE_MIXER1,
		WM8991_LOPLON_BIT, 1, 0),
};

/* LOPMIX */
static const struct snd_kcontrol_new wm8991_dapm_lopmix_controls[] = {
	SOC_DAPM_SINGLE("LOPMIX Right Mic Bypass Switch", WM8991_LINE_MIXER1,
		WM8991_LR12LOP_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOPMIX Left Mic Bypass Switch", WM8991_LINE_MIXER1,
		WM8991_LL12LOP_BIT, 1, 0),
	SOC_DAPM_SINGLE("LOPMIX Left Mixer PGA Switch", WM8991_LINE_MIXER1,
		WM8991_LLOPGALOP_BIT, 1, 0),
};

/* RONMIX */
static const struct snd_kcontrol_new wm8991_dapm_ronmix_controls[] = {
	SOC_DAPM_SINGLE("RONMIX Right Mixer PGA Switch", WM8991_LINE_MIXER2,
		WM8991_RROPGARON_BIT, 1, 0),
	SOC_DAPM_SINGLE("RONMIX Left Mixer PGA Switch", WM8991_LINE_MIXER2,
		WM8991_RLOPGARON_BIT, 1, 0),
	SOC_DAPM_SINGLE("RONMIX Inverted ROP Switch", WM8991_LINE_MIXER2,
		WM8991_ROPRON_BIT, 1, 0),
};

/* ROPMIX */
static const struct snd_kcontrol_new wm8991_dapm_ropmix_controls[] = {
	SOC_DAPM_SINGLE("ROPMIX Left Mic Bypass Switch", WM8991_LINE_MIXER2,
		WM8991_RL12ROP_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROPMIX Right Mic Bypass Switch", WM8991_LINE_MIXER2,
		WM8991_RR12ROP_BIT, 1, 0),
	SOC_DAPM_SINGLE("ROPMIX Right Mixer PGA Switch", WM8991_LINE_MIXER2,
		WM8991_RROPGAROP_BIT, 1, 0),
};

/* OUT3MIX */
static const struct snd_kcontrol_new wm8991_dapm_out3mix_controls[] = {
	SOC_DAPM_SINGLE("OUT3MIX LIN4RXN Bypass Switch", WM8991_OUT3_4_MIXER,
		WM8991_LI4O3_BIT, 1, 0),
	SOC_DAPM_SINGLE("OUT3MIX Left Out PGA Switch", WM8991_OUT3_4_MIXER,
		WM8991_LPGAO3_BIT, 1, 0),
};

/* OUT4MIX */
static const struct snd_kcontrol_new wm8991_dapm_out4mix_controls[] = {
	SOC_DAPM_SINGLE("OUT4MIX Right Out PGA Switch", WM8991_OUT3_4_MIXER,
		WM8991_RPGAO4_BIT, 1, 0),
	SOC_DAPM_SINGLE("OUT4MIX RIN4RXP Bypass Switch", WM8991_OUT3_4_MIXER,
		WM8991_RI4O4_BIT, 1, 0),
};

/* SPKMIX */
static const struct snd_kcontrol_new wm8991_dapm_spkmix_controls[] = {
	SOC_DAPM_SINGLE("SPKMIX LIN2 Bypass Switch", WM8991_SPEAKER_MIXER,
		WM8991_LI2SPK_BIT, 1, 0),
	SOC_DAPM_SINGLE("SPKMIX LADC Bypass Switch", WM8991_SPEAKER_MIXER,
		WM8991_LB2SPK_BIT, 1, 0),
	SOC_DAPM_SINGLE("SPKMIX Left Mixer PGA Switch", WM8991_SPEAKER_MIXER,
		WM8991_LOPGASPK_BIT, 1, 0),
	SOC_DAPM_SINGLE("SPKMIX Left DAC Switch", WM8991_SPEAKER_MIXER,
		WM8991_LDSPK_BIT, 1, 0),
	SOC_DAPM_SINGLE("SPKMIX Right DAC Switch", WM8991_SPEAKER_MIXER,
		WM8991_RDSPK_BIT, 1, 0),
	SOC_DAPM_SINGLE("SPKMIX Right Mixer PGA Switch", WM8991_SPEAKER_MIXER,
		WM8991_ROPGASPK_BIT, 1, 0),
	SOC_DAPM_SINGLE("SPKMIX RADC Bypass Switch", WM8991_SPEAKER_MIXER,
		WM8991_RL12ROP_BIT, 1, 0),
	SOC_DAPM_SINGLE("SPKMIX RIN2 Bypass Switch", WM8991_SPEAKER_MIXER,
		WM8991_RI2SPK_BIT, 1, 0),
};

static const struct snd_soc_dapm_widget wm8991_dapm_widgets[] = {
	/* Input Side */
	/* Input Lines */
	SND_SOC_DAPM_INPUT("LIN1"),
	SND_SOC_DAPM_INPUT("LIN2"),
	SND_SOC_DAPM_INPUT("LIN3"),
	SND_SOC_DAPM_INPUT("LIN4RXN"),
	SND_SOC_DAPM_INPUT("RIN3"),
	SND_SOC_DAPM_INPUT("RIN4RXP"),
	SND_SOC_DAPM_INPUT("RIN1"),
	SND_SOC_DAPM_INPUT("RIN2"),
	SND_SOC_DAPM_INPUT("Internal ADC Source"),

	SND_SOC_DAPM_SUPPLY("INL", WM8991_POWER_MANAGEMENT_2,
			    WM8991_AINL_ENA_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("INR", WM8991_POWER_MANAGEMENT_2,
			    WM8991_AINR_ENA_BIT, 0, NULL, 0),

	/* DACs */
	SND_SOC_DAPM_ADC("Left ADC", "Left Capture", WM8991_POWER_MANAGEMENT_2,
		WM8991_ADCL_ENA_BIT, 0),
	SND_SOC_DAPM_ADC("Right ADC", "Right Capture", WM8991_POWER_MANAGEMENT_2,
		WM8991_ADCR_ENA_BIT, 0),

	/* Input PGAs */
	SND_SOC_DAPM_MIXER("LIN12 PGA", WM8991_POWER_MANAGEMENT_2, WM8991_LIN12_ENA_BIT,
		0, &wm8991_dapm_lin12_pga_controls[0],
		ARRAY_SIZE(wm8991_dapm_lin12_pga_controls)),
	SND_SOC_DAPM_MIXER("LIN34 PGA", WM8991_POWER_MANAGEMENT_2, WM8991_LIN34_ENA_BIT,
		0, &wm8991_dapm_lin34_pga_controls[0],
		ARRAY_SIZE(wm8991_dapm_lin34_pga_controls)),
	SND_SOC_DAPM_MIXER("RIN12 PGA", WM8991_POWER_MANAGEMENT_2, WM8991_RIN12_ENA_BIT,
		0, &wm8991_dapm_rin12_pga_controls[0],
		ARRAY_SIZE(wm8991_dapm_rin12_pga_controls)),
	SND_SOC_DAPM_MIXER("RIN34 PGA", WM8991_POWER_MANAGEMENT_2, WM8991_RIN34_ENA_BIT,
		0, &wm8991_dapm_rin34_pga_controls[0],
		ARRAY_SIZE(wm8991_dapm_rin34_pga_controls)),

	/* INMIXL */
	SND_SOC_DAPM_MIXER("INMIXL", SND_SOC_NOPM, 0, 0,
		&wm8991_dapm_inmixl_controls[0],
		ARRAY_SIZE(wm8991_dapm_inmixl_controls)),

	/* AINLMUX */
	SND_SOC_DAPM_MUX("AINLMUX", SND_SOC_NOPM, 0, 0,
		&wm8991_dapm_ainlmux_controls),

	/* INMIXR */
	SND_SOC_DAPM_MIXER("INMIXR", SND_SOC_NOPM, 0, 0,
		&wm8991_dapm_inmixr_controls[0],
		ARRAY_SIZE(wm8991_dapm_inmixr_controls)),

	/* AINRMUX */
	SND_SOC_DAPM_MUX("AINRMUX", SND_SOC_NOPM, 0, 0,
		&wm8991_dapm_ainrmux_controls),

	/* Output Side */
	/* DACs */
	SND_SOC_DAPM_DAC("Left DAC", "Left Playback", WM8991_POWER_MANAGEMENT_3,
		WM8991_DACL_ENA_BIT, 0),
	SND_SOC_DAPM_DAC("Right DAC", "Right Playback", WM8991_POWER_MANAGEMENT_3,
		WM8991_DACR_ENA_BIT, 0),

	/* LOMIX */
	SND_SOC_DAPM_MIXER_E("LOMIX", WM8991_POWER_MANAGEMENT_3, WM8991_LOMIX_ENA_BIT,
		0, &wm8991_dapm_lomix_controls[0],
		ARRAY_SIZE(wm8991_dapm_lomix_controls),
		outmixer_event, SND_SOC_DAPM_PRE_REG),

	/* LONMIX */
	SND_SOC_DAPM_MIXER("LONMIX", WM8991_POWER_MANAGEMENT_3, WM8991_LON_ENA_BIT, 0,
		&wm8991_dapm_lonmix_controls[0],
		ARRAY_SIZE(wm8991_dapm_lonmix_controls)),

	/* LOPMIX */
	SND_SOC_DAPM_MIXER("LOPMIX", WM8991_POWER_MANAGEMENT_3, WM8991_LOP_ENA_BIT, 0,
		&wm8991_dapm_lopmix_controls[0],
		ARRAY_SIZE(wm8991_dapm_lopmix_controls)),

	/* OUT3MIX */
	SND_SOC_DAPM_MIXER("OUT3MIX", WM8991_POWER_MANAGEMENT_1, WM8991_OUT3_ENA_BIT, 0,
		&wm8991_dapm_out3mix_controls[0],
		ARRAY_SIZE(wm8991_dapm_out3mix_controls)),

	/* SPKMIX */
	SND_SOC_DAPM_MIXER_E("SPKMIX", WM8991_POWER_MANAGEMENT_1, WM8991_SPK_ENA_BIT, 0,
		&wm8991_dapm_spkmix_controls[0],
		ARRAY_SIZE(wm8991_dapm_spkmix_controls), outmixer_event,
		SND_SOC_DAPM_PRE_REG),

	/* OUT4MIX */
	SND_SOC_DAPM_MIXER("OUT4MIX", WM8991_POWER_MANAGEMENT_1, WM8991_OUT4_ENA_BIT, 0,
		&wm8991_dapm_out4mix_controls[0],
		ARRAY_SIZE(wm8991_dapm_out4mix_controls)),

	/* ROPMIX */
	SND_SOC_DAPM_MIXER("ROPMIX", WM8991_POWER_MANAGEMENT_3, WM8991_ROP_ENA_BIT, 0,
		&wm8991_dapm_ropmix_controls[0],
		ARRAY_SIZE(wm8991_dapm_ropmix_controls)),

	/* RONMIX */
	SND_SOC_DAPM_MIXER("RONMIX", WM8991_POWER_MANAGEMENT_3, WM8991_RON_ENA_BIT, 0,
		&wm8991_dapm_ronmix_controls[0],
		ARRAY_SIZE(wm8991_dapm_ronmix_controls)),

	/* ROMIX */
	SND_SOC_DAPM_MIXER_E("ROMIX", WM8991_POWER_MANAGEMENT_3, WM8991_ROMIX_ENA_BIT,
		0, &wm8991_dapm_romix_controls[0],
		ARRAY_SIZE(wm8991_dapm_romix_controls),
		outmixer_event, SND_SOC_DAPM_PRE_REG),

	/* LOUT PGA */
	SND_SOC_DAPM_PGA("LOUT PGA", WM8991_POWER_MANAGEMENT_1, WM8991_LOUT_ENA_BIT, 0,
		NULL, 0),

	/* ROUT PGA */
	SND_SOC_DAPM_PGA("ROUT PGA", WM8991_POWER_MANAGEMENT_1, WM8991_ROUT_ENA_BIT, 0,
		NULL, 0),

	/* LOPGA */
	SND_SOC_DAPM_PGA("LOPGA", WM8991_POWER_MANAGEMENT_3, WM8991_LOPGA_ENA_BIT, 0,
		NULL, 0),

	/* ROPGA */
	SND_SOC_DAPM_PGA("ROPGA", WM8991_POWER_MANAGEMENT_3, WM8991_ROPGA_ENA_BIT, 0,
		NULL, 0),

	/* MICBIAS */
	SND_SOC_DAPM_SUPPLY("MICBIAS", WM8991_POWER_MANAGEMENT_1,
			    WM8991_MICBIAS_ENA_BIT, 0, NULL, 0),

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
	SND_SOC_DAPM_OUTPUT("OUT"),

	SND_SOC_DAPM_OUTPUT("Internal DAC Sink"),
};

static const struct snd_soc_dapm_route wm8991_dapm_routes[] = {
	/* Make DACs turn on when playing even if not mixed into any outputs */
	{"Internal DAC Sink", NULL, "Left DAC"},
	{"Internal DAC Sink", NULL, "Right DAC"},

	/* Make ADCs turn on when recording even if not mixed from any inputs */
	{"Left ADC", NULL, "Internal ADC Source"},
	{"Right ADC", NULL, "Internal ADC Source"},

	/* Input Side */
	{"INMIXL", NULL, "INL"},
	{"AINLMUX", NULL, "INL"},
	{"INMIXR", NULL, "INR"},
	{"AINRMUX", NULL, "INR"},
	/* LIN12 PGA */
	{"LIN12 PGA", "LIN1 Switch", "LIN1"},
	{"LIN12 PGA", "LIN2 Switch", "LIN2"},
	/* LIN34 PGA */
	{"LIN34 PGA", "LIN3 Switch", "LIN3"},
	{"LIN34 PGA", "LIN4 Switch", "LIN4RXN"},
	/* INMIXL */
	{"INMIXL", "Record Left Volume", "LOMIX"},
	{"INMIXL", "LIN2 Volume", "LIN2"},
	{"INMIXL", "LINPGA12 Switch", "LIN12 PGA"},
	{"INMIXL", "LINPGA34 Switch", "LIN34 PGA"},
	/* AINLMUX */
	{"AINLMUX", "INMIXL Mix", "INMIXL"},
	{"AINLMUX", "DIFFINL Mix", "LIN12 PGA"},
	{"AINLMUX", "DIFFINL Mix", "LIN34 PGA"},
	{"AINLMUX", "RXVOICE Mix", "LIN4RXN"},
	{"AINLMUX", "RXVOICE Mix", "RIN4RXP"},
	/* ADC */
	{"Left ADC", NULL, "AINLMUX"},

	/* RIN12 PGA */
	{"RIN12 PGA", "RIN1 Switch", "RIN1"},
	{"RIN12 PGA", "RIN2 Switch", "RIN2"},
	/* RIN34 PGA */
	{"RIN34 PGA", "RIN3 Switch", "RIN3"},
	{"RIN34 PGA", "RIN4 Switch", "RIN4RXP"},
	/* INMIXL */
	{"INMIXR", "Record Right Volume", "ROMIX"},
	{"INMIXR", "RIN2 Volume", "RIN2"},
	{"INMIXR", "RINPGA12 Switch", "RIN12 PGA"},
	{"INMIXR", "RINPGA34 Switch", "RIN34 PGA"},
	/* AINRMUX */
	{"AINRMUX", "INMIXR Mix", "INMIXR"},
	{"AINRMUX", "DIFFINR Mix", "RIN12 PGA"},
	{"AINRMUX", "DIFFINR Mix", "RIN34 PGA"},
	{"AINRMUX", "RXVOICE Mix", "LIN4RXN"},
	{"AINRMUX", "RXVOICE Mix", "RIN4RXP"},
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
	{"OUT3MIX", "OUT3MIX LIN4RXN Bypass Switch", "LIN4RXN"},
	{"OUT3MIX", "OUT3MIX Left Out PGA Switch", "LOPGA"},

	/* OUT4MIX */
	{"OUT4MIX", "OUT4MIX Right Out PGA Switch", "ROPGA"},
	{"OUT4MIX", "OUT4MIX RIN4RXP Bypass Switch", "RIN4RXP"},

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
	{"OUT", NULL, "OUT3MIX"},
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
		       "WM8991 N value outwith recommended range! N = %d\n", Ndiv);

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

static int wm8991_set_dai_pll(struct snd_soc_dai *codec_dai,
			      int pll_id, int src, unsigned int freq_in, unsigned int freq_out)
{
	u16 reg;
	struct snd_soc_component *component = codec_dai->component;
	struct _pll_div pll_div;

	if (freq_in && freq_out) {
		pll_factors(&pll_div, freq_out * 4, freq_in);

		/* Turn on PLL */
		reg = snd_soc_component_read(component, WM8991_POWER_MANAGEMENT_2);
		reg |= WM8991_PLL_ENA;
		snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_2, reg);

		/* sysclk comes from PLL */
		reg = snd_soc_component_read(component, WM8991_CLOCKING_2);
		snd_soc_component_write(component, WM8991_CLOCKING_2, reg | WM8991_SYSCLK_SRC);

		/* set up N , fractional mode and pre-divisor if necessary */
		snd_soc_component_write(component, WM8991_PLL1, pll_div.n | WM8991_SDM |
			      (pll_div.div2 ? WM8991_PRESCALE : 0));
		snd_soc_component_write(component, WM8991_PLL2, (u8)(pll_div.k>>8));
		snd_soc_component_write(component, WM8991_PLL3, (u8)(pll_div.k & 0xFF));
	} else {
		/* Turn on PLL */
		reg = snd_soc_component_read(component, WM8991_POWER_MANAGEMENT_2);
		reg &= ~WM8991_PLL_ENA;
		snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_2, reg);
	}
	return 0;
}

/*
 * Set's ADC and Voice DAC format.
 */
static int wm8991_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int fmt)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 audio1, audio3;

	audio1 = snd_soc_component_read(component, WM8991_AUDIO_INTERFACE_1);
	audio3 = snd_soc_component_read(component, WM8991_AUDIO_INTERFACE_3);

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		audio3 &= ~WM8991_AIF_MSTR1;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		audio3 |= WM8991_AIF_MSTR1;
		break;
	default:
		return -EINVAL;
	}

	audio1 &= ~WM8991_AIF_FMT_MASK;

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		audio1 |= WM8991_AIF_TMF_I2S;
		audio1 &= ~WM8991_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		audio1 |= WM8991_AIF_TMF_RIGHTJ;
		audio1 &= ~WM8991_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		audio1 |= WM8991_AIF_TMF_LEFTJ;
		audio1 &= ~WM8991_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		audio1 |= WM8991_AIF_TMF_DSP;
		audio1 &= ~WM8991_AIF_LRCLK_INV;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		audio1 |= WM8991_AIF_TMF_DSP | WM8991_AIF_LRCLK_INV;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, WM8991_AUDIO_INTERFACE_1, audio1);
	snd_soc_component_write(component, WM8991_AUDIO_INTERFACE_3, audio3);
	return 0;
}

static int wm8991_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
				 int div_id, int div)
{
	struct snd_soc_component *component = codec_dai->component;
	u16 reg;

	switch (div_id) {
	case WM8991_MCLK_DIV:
		reg = snd_soc_component_read(component, WM8991_CLOCKING_2) &
		      ~WM8991_MCLK_DIV_MASK;
		snd_soc_component_write(component, WM8991_CLOCKING_2, reg | div);
		break;
	case WM8991_DACCLK_DIV:
		reg = snd_soc_component_read(component, WM8991_CLOCKING_2) &
		      ~WM8991_DAC_CLKDIV_MASK;
		snd_soc_component_write(component, WM8991_CLOCKING_2, reg | div);
		break;
	case WM8991_ADCCLK_DIV:
		reg = snd_soc_component_read(component, WM8991_CLOCKING_2) &
		      ~WM8991_ADC_CLKDIV_MASK;
		snd_soc_component_write(component, WM8991_CLOCKING_2, reg | div);
		break;
	case WM8991_BCLK_DIV:
		reg = snd_soc_component_read(component, WM8991_CLOCKING_1) &
		      ~WM8991_BCLK_DIV_MASK;
		snd_soc_component_write(component, WM8991_CLOCKING_1, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * Set PCM DAI bit size and sample rate.
 */
static int wm8991_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u16 audio1 = snd_soc_component_read(component, WM8991_AUDIO_INTERFACE_1);

	audio1 &= ~WM8991_AIF_WL_MASK;
	/* bit size */
	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		audio1 |= WM8991_AIF_WL_20BITS;
		break;
	case 24:
		audio1 |= WM8991_AIF_WL_24BITS;
		break;
	case 32:
		audio1 |= WM8991_AIF_WL_32BITS;
		break;
	}

	snd_soc_component_write(component, WM8991_AUDIO_INTERFACE_1, audio1);
	return 0;
}

static int wm8991_mute(struct snd_soc_dai *dai, int mute, int direction)
{
	struct snd_soc_component *component = dai->component;
	u16 val;

	val  = snd_soc_component_read(component, WM8991_DAC_CTRL) & ~WM8991_DAC_MUTE;
	if (mute)
		snd_soc_component_write(component, WM8991_DAC_CTRL, val | WM8991_DAC_MUTE);
	else
		snd_soc_component_write(component, WM8991_DAC_CTRL, val);
	return 0;
}

static int wm8991_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct wm8991_priv *wm8991 = snd_soc_component_get_drvdata(component);
	u16 val;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/* VMID=2*50k */
		val = snd_soc_component_read(component, WM8991_POWER_MANAGEMENT_1) &
		      ~WM8991_VMID_MODE_MASK;
		snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, val | 0x2);
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			regcache_sync(wm8991->regmap);
			/* Enable all output discharge bits */
			snd_soc_component_write(component, WM8991_ANTIPOP1, WM8991_DIS_LLINE |
				      WM8991_DIS_RLINE | WM8991_DIS_OUT3 |
				      WM8991_DIS_OUT4 | WM8991_DIS_LOUT |
				      WM8991_DIS_ROUT);

			/* Enable POBCTRL, SOFT_ST, VMIDTOG and BUFDCOPEN */
			snd_soc_component_write(component, WM8991_ANTIPOP2, WM8991_SOFTST |
				      WM8991_BUFDCOPEN | WM8991_POBCTRL |
				      WM8991_VMIDTOG);

			/* Delay to allow output caps to discharge */
			msleep(300);

			/* Disable VMIDTOG */
			snd_soc_component_write(component, WM8991_ANTIPOP2, WM8991_SOFTST |
				      WM8991_BUFDCOPEN | WM8991_POBCTRL);

			/* disable all output discharge bits */
			snd_soc_component_write(component, WM8991_ANTIPOP1, 0);

			/* Enable outputs */
			snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, 0x1b00);

			msleep(50);

			/* Enable VMID at 2x50k */
			snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, 0x1f02);

			msleep(100);

			/* Enable VREF */
			snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, 0x1f03);

			msleep(600);

			/* Enable BUFIOEN */
			snd_soc_component_write(component, WM8991_ANTIPOP2, WM8991_SOFTST |
				      WM8991_BUFDCOPEN | WM8991_POBCTRL |
				      WM8991_BUFIOEN);

			/* Disable outputs */
			snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, 0x3);

			/* disable POBCTRL, SOFT_ST and BUFDCOPEN */
			snd_soc_component_write(component, WM8991_ANTIPOP2, WM8991_BUFIOEN);
		}

		/* VMID=2*250k */
		val = snd_soc_component_read(component, WM8991_POWER_MANAGEMENT_1) &
		      ~WM8991_VMID_MODE_MASK;
		snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, val | 0x4);
		break;

	case SND_SOC_BIAS_OFF:
		/* Enable POBCTRL and SOFT_ST */
		snd_soc_component_write(component, WM8991_ANTIPOP2, WM8991_SOFTST |
			      WM8991_POBCTRL | WM8991_BUFIOEN);

		/* Enable POBCTRL, SOFT_ST and BUFDCOPEN */
		snd_soc_component_write(component, WM8991_ANTIPOP2, WM8991_SOFTST |
			      WM8991_BUFDCOPEN | WM8991_POBCTRL |
			      WM8991_BUFIOEN);

		/* mute DAC */
		val = snd_soc_component_read(component, WM8991_DAC_CTRL);
		snd_soc_component_write(component, WM8991_DAC_CTRL, val | WM8991_DAC_MUTE);

		/* Enable any disabled outputs */
		snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, 0x1f03);

		/* Disable VMID */
		snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, 0x1f01);

		msleep(300);

		/* Enable all output discharge bits */
		snd_soc_component_write(component, WM8991_ANTIPOP1, WM8991_DIS_LLINE |
			      WM8991_DIS_RLINE | WM8991_DIS_OUT3 |
			      WM8991_DIS_OUT4 | WM8991_DIS_LOUT |
			      WM8991_DIS_ROUT);

		/* Disable VREF */
		snd_soc_component_write(component, WM8991_POWER_MANAGEMENT_1, 0x0);

		/* disable POBCTRL, SOFT_ST and BUFDCOPEN */
		snd_soc_component_write(component, WM8991_ANTIPOP2, 0x0);
		regcache_mark_dirty(wm8991->regmap);
		break;
	}

	return 0;
}

#define WM8991_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static const struct snd_soc_dai_ops wm8991_ops = {
	.hw_params = wm8991_hw_params,
	.mute_stream = wm8991_mute,
	.set_fmt = wm8991_set_dai_fmt,
	.set_clkdiv = wm8991_set_dai_clkdiv,
	.set_pll = wm8991_set_dai_pll,
	.no_capture_mute = 1,
};

/*
 * The WM8991 supports 2 different and mutually exclusive DAI
 * configurations.
 *
 * 1. ADC/DAC on Primary Interface
 * 2. ADC on Primary Interface/DAC on secondary
 */
static struct snd_soc_dai_driver wm8991_dai = {
	/* ADC/DAC on primary */
	.name = "wm8991",
	.id = 1,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = WM8991_FORMATS
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = WM8991_FORMATS
	},
	.ops = &wm8991_ops
};

static const struct snd_soc_component_driver soc_component_dev_wm8991 = {
	.set_bias_level		= wm8991_set_bias_level,
	.controls		= wm8991_snd_controls,
	.num_controls		= ARRAY_SIZE(wm8991_snd_controls),
	.dapm_widgets		= wm8991_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8991_dapm_widgets),
	.dapm_routes		= wm8991_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8991_dapm_routes),
	.suspend_bias_off	= 1,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config wm8991_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = WM8991_PLL3,
	.volatile_reg = wm8991_volatile,
	.reg_defaults = wm8991_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(wm8991_reg_defaults),
	.cache_type = REGCACHE_RBTREE,
};

static int wm8991_i2c_probe(struct i2c_client *i2c)
{
	struct wm8991_priv *wm8991;
	unsigned int val;
	int ret;

	wm8991 = devm_kzalloc(&i2c->dev, sizeof(*wm8991), GFP_KERNEL);
	if (!wm8991)
		return -ENOMEM;

	wm8991->regmap = devm_regmap_init_i2c(i2c, &wm8991_regmap);
	if (IS_ERR(wm8991->regmap))
		return PTR_ERR(wm8991->regmap);

	i2c_set_clientdata(i2c, wm8991);

	ret = regmap_read(wm8991->regmap, WM8991_RESET, &val);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read device ID: %d\n", ret);
		return ret;
	}
	if (val != 0x8991) {
		dev_err(&i2c->dev, "Device with ID %x is not a WM8991\n", val);
		return -EINVAL;
	}

	ret = regmap_write(wm8991->regmap, WM8991_RESET, 0);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to issue reset: %d\n", ret);
		return ret;
	}

	regmap_update_bits(wm8991->regmap, WM8991_AUDIO_INTERFACE_4,
			   WM8991_ALRCGPIO1, WM8991_ALRCGPIO1);

	regmap_update_bits(wm8991->regmap, WM8991_GPIO1_GPIO2,
			   WM8991_GPIO1_SEL_MASK, 1);

	regmap_update_bits(wm8991->regmap, WM8991_POWER_MANAGEMENT_1,
			   WM8991_VREF_ENA | WM8991_VMID_MODE_MASK,
			   WM8991_VREF_ENA | WM8991_VMID_MODE_MASK);

	regmap_update_bits(wm8991->regmap, WM8991_POWER_MANAGEMENT_2,
			   WM8991_OPCLK_ENA, WM8991_OPCLK_ENA);

	regmap_write(wm8991->regmap, WM8991_DAC_CTRL, 0);
	regmap_write(wm8991->regmap, WM8991_LEFT_OUTPUT_VOLUME,
		     0x50 | (1<<8));
	regmap_write(wm8991->regmap, WM8991_RIGHT_OUTPUT_VOLUME,
		     0x50 | (1<<8));

	ret = devm_snd_soc_register_component(&i2c->dev,
				     &soc_component_dev_wm8991, &wm8991_dai, 1);

	return ret;
}

static const struct i2c_device_id wm8991_i2c_id[] = {
	{ "wm8991", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8991_i2c_id);

static struct i2c_driver wm8991_i2c_driver = {
	.driver = {
		.name = "wm8991",
	},
	.probe_new = wm8991_i2c_probe,
	.id_table = wm8991_i2c_id,
};

module_i2c_driver(wm8991_i2c_driver);

MODULE_DESCRIPTION("ASoC WM8991 driver");
MODULE_AUTHOR("Graeme Gregory");
MODULE_LICENSE("GPL");
