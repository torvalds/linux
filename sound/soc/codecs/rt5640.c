/*
 * rt5640.c  --  RT5640/RT5639 ALSA SoC audio codec driver
 *
 * Copyright 2011 Realtek Semiconductor Corp.
 * Author: Johnny Hsu <johnnyhsu@realtek.com>
 * Copyright (c) 2013, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt5640.h"

#define RT5640_DEVICE_ID 0x6231

#define RT5640_PR_RANGE_BASE (0xff + 1)
#define RT5640_PR_SPACING 0x100

#define RT5640_PR_BASE (RT5640_PR_RANGE_BASE + (0 * RT5640_PR_SPACING))

static const struct regmap_range_cfg rt5640_ranges[] = {
	{ .name = "PR", .range_min = RT5640_PR_BASE,
	  .range_max = RT5640_PR_BASE + 0xb4,
	  .selector_reg = RT5640_PRIV_INDEX,
	  .selector_mask = 0xff,
	  .selector_shift = 0x0,
	  .window_start = RT5640_PRIV_DATA,
	  .window_len = 0x1, },
};

static struct reg_default init_list[] = {
	{RT5640_PR_BASE + 0x3d,	0x3600},
	{RT5640_PR_BASE + 0x12,	0x0aa8},
	{RT5640_PR_BASE + 0x14,	0x0aaa},
	{RT5640_PR_BASE + 0x20,	0x6110},
	{RT5640_PR_BASE + 0x21,	0xe0e0},
	{RT5640_PR_BASE + 0x23,	0x1804},
};
#define RT5640_INIT_REG_LEN ARRAY_SIZE(init_list)

static const struct reg_default rt5640_reg[] = {
	{ 0x00, 0x000e },
	{ 0x01, 0xc8c8 },
	{ 0x02, 0xc8c8 },
	{ 0x03, 0xc8c8 },
	{ 0x04, 0x8000 },
	{ 0x0d, 0x0000 },
	{ 0x0e, 0x0000 },
	{ 0x0f, 0x0808 },
	{ 0x19, 0xafaf },
	{ 0x1a, 0xafaf },
	{ 0x1b, 0x0000 },
	{ 0x1c, 0x2f2f },
	{ 0x1d, 0x2f2f },
	{ 0x1e, 0x0000 },
	{ 0x27, 0x7060 },
	{ 0x28, 0x7070 },
	{ 0x29, 0x8080 },
	{ 0x2a, 0x5454 },
	{ 0x2b, 0x5454 },
	{ 0x2c, 0xaa00 },
	{ 0x2d, 0x0000 },
	{ 0x2e, 0xa000 },
	{ 0x2f, 0x0000 },
	{ 0x3b, 0x0000 },
	{ 0x3c, 0x007f },
	{ 0x3d, 0x0000 },
	{ 0x3e, 0x007f },
	{ 0x45, 0xe000 },
	{ 0x46, 0x003e },
	{ 0x47, 0x003e },
	{ 0x48, 0xf800 },
	{ 0x49, 0x3800 },
	{ 0x4a, 0x0004 },
	{ 0x4c, 0xfc00 },
	{ 0x4d, 0x0000 },
	{ 0x4f, 0x01ff },
	{ 0x50, 0x0000 },
	{ 0x51, 0x0000 },
	{ 0x52, 0x01ff },
	{ 0x53, 0xf000 },
	{ 0x61, 0x0000 },
	{ 0x62, 0x0000 },
	{ 0x63, 0x00c0 },
	{ 0x64, 0x0000 },
	{ 0x65, 0x0000 },
	{ 0x66, 0x0000 },
	{ 0x6a, 0x0000 },
	{ 0x6c, 0x0000 },
	{ 0x70, 0x8000 },
	{ 0x71, 0x8000 },
	{ 0x72, 0x8000 },
	{ 0x73, 0x1114 },
	{ 0x74, 0x0c00 },
	{ 0x75, 0x1d00 },
	{ 0x80, 0x0000 },
	{ 0x81, 0x0000 },
	{ 0x82, 0x0000 },
	{ 0x83, 0x0000 },
	{ 0x84, 0x0000 },
	{ 0x85, 0x0008 },
	{ 0x89, 0x0000 },
	{ 0x8a, 0x0000 },
	{ 0x8b, 0x0600 },
	{ 0x8c, 0x0228 },
	{ 0x8d, 0xa000 },
	{ 0x8e, 0x0004 },
	{ 0x8f, 0x1100 },
	{ 0x90, 0x0646 },
	{ 0x91, 0x0c00 },
	{ 0x92, 0x0000 },
	{ 0x93, 0x3000 },
	{ 0xb0, 0x2080 },
	{ 0xb1, 0x0000 },
	{ 0xb4, 0x2206 },
	{ 0xb5, 0x1f00 },
	{ 0xb6, 0x0000 },
	{ 0xb8, 0x034b },
	{ 0xb9, 0x0066 },
	{ 0xba, 0x000b },
	{ 0xbb, 0x0000 },
	{ 0xbc, 0x0000 },
	{ 0xbd, 0x0000 },
	{ 0xbe, 0x0000 },
	{ 0xbf, 0x0000 },
	{ 0xc0, 0x0400 },
	{ 0xc2, 0x0000 },
	{ 0xc4, 0x0000 },
	{ 0xc5, 0x0000 },
	{ 0xc6, 0x2000 },
	{ 0xc8, 0x0000 },
	{ 0xc9, 0x0000 },
	{ 0xca, 0x0000 },
	{ 0xcb, 0x0000 },
	{ 0xcc, 0x0000 },
	{ 0xcf, 0x0013 },
	{ 0xd0, 0x0680 },
	{ 0xd1, 0x1c17 },
	{ 0xd2, 0x8c00 },
	{ 0xd3, 0xaa20 },
	{ 0xd6, 0x0400 },
	{ 0xd9, 0x0809 },
	{ 0xfe, 0x10ec },
	{ 0xff, 0x6231 },
};

static int rt5640_reset(struct snd_soc_codec *codec)
{
	return snd_soc_write(codec, RT5640_RESET, 0);
}

static bool rt5640_volatile_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5640_ranges); i++)
		if ((reg >= rt5640_ranges[i].window_start &&
		     reg <= rt5640_ranges[i].window_start +
		     rt5640_ranges[i].window_len) ||
		    (reg >= rt5640_ranges[i].range_min &&
		     reg <= rt5640_ranges[i].range_max))
			return true;

	switch (reg) {
	case RT5640_RESET:
	case RT5640_ASRC_5:
	case RT5640_EQ_CTRL1:
	case RT5640_DRC_AGC_1:
	case RT5640_ANC_CTRL1:
	case RT5640_IRQ_CTRL2:
	case RT5640_INT_IRQ_ST:
	case RT5640_DSP_CTRL2:
	case RT5640_DSP_CTRL3:
	case RT5640_PRIV_INDEX:
	case RT5640_PRIV_DATA:
	case RT5640_PGM_REG_ARR1:
	case RT5640_PGM_REG_ARR3:
	case RT5640_VENDOR_ID:
	case RT5640_VENDOR_ID1:
	case RT5640_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static bool rt5640_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5640_ranges); i++)
		if ((reg >= rt5640_ranges[i].window_start &&
		     reg <= rt5640_ranges[i].window_start +
		     rt5640_ranges[i].window_len) ||
		    (reg >= rt5640_ranges[i].range_min &&
		     reg <= rt5640_ranges[i].range_max))
			return true;

	switch (reg) {
	case RT5640_RESET:
	case RT5640_SPK_VOL:
	case RT5640_HP_VOL:
	case RT5640_OUTPUT:
	case RT5640_MONO_OUT:
	case RT5640_IN1_IN2:
	case RT5640_IN3_IN4:
	case RT5640_INL_INR_VOL:
	case RT5640_DAC1_DIG_VOL:
	case RT5640_DAC2_DIG_VOL:
	case RT5640_DAC2_CTRL:
	case RT5640_ADC_DIG_VOL:
	case RT5640_ADC_DATA:
	case RT5640_ADC_BST_VOL:
	case RT5640_STO_ADC_MIXER:
	case RT5640_MONO_ADC_MIXER:
	case RT5640_AD_DA_MIXER:
	case RT5640_STO_DAC_MIXER:
	case RT5640_MONO_DAC_MIXER:
	case RT5640_DIG_MIXER:
	case RT5640_DSP_PATH1:
	case RT5640_DSP_PATH2:
	case RT5640_DIG_INF_DATA:
	case RT5640_REC_L1_MIXER:
	case RT5640_REC_L2_MIXER:
	case RT5640_REC_R1_MIXER:
	case RT5640_REC_R2_MIXER:
	case RT5640_HPO_MIXER:
	case RT5640_SPK_L_MIXER:
	case RT5640_SPK_R_MIXER:
	case RT5640_SPO_L_MIXER:
	case RT5640_SPO_R_MIXER:
	case RT5640_SPO_CLSD_RATIO:
	case RT5640_MONO_MIXER:
	case RT5640_OUT_L1_MIXER:
	case RT5640_OUT_L2_MIXER:
	case RT5640_OUT_L3_MIXER:
	case RT5640_OUT_R1_MIXER:
	case RT5640_OUT_R2_MIXER:
	case RT5640_OUT_R3_MIXER:
	case RT5640_LOUT_MIXER:
	case RT5640_PWR_DIG1:
	case RT5640_PWR_DIG2:
	case RT5640_PWR_ANLG1:
	case RT5640_PWR_ANLG2:
	case RT5640_PWR_MIXER:
	case RT5640_PWR_VOL:
	case RT5640_PRIV_INDEX:
	case RT5640_PRIV_DATA:
	case RT5640_I2S1_SDP:
	case RT5640_I2S2_SDP:
	case RT5640_ADDA_CLK1:
	case RT5640_ADDA_CLK2:
	case RT5640_DMIC:
	case RT5640_GLB_CLK:
	case RT5640_PLL_CTRL1:
	case RT5640_PLL_CTRL2:
	case RT5640_ASRC_1:
	case RT5640_ASRC_2:
	case RT5640_ASRC_3:
	case RT5640_ASRC_4:
	case RT5640_ASRC_5:
	case RT5640_HP_OVCD:
	case RT5640_CLS_D_OVCD:
	case RT5640_CLS_D_OUT:
	case RT5640_DEPOP_M1:
	case RT5640_DEPOP_M2:
	case RT5640_DEPOP_M3:
	case RT5640_CHARGE_PUMP:
	case RT5640_PV_DET_SPK_G:
	case RT5640_MICBIAS:
	case RT5640_EQ_CTRL1:
	case RT5640_EQ_CTRL2:
	case RT5640_WIND_FILTER:
	case RT5640_DRC_AGC_1:
	case RT5640_DRC_AGC_2:
	case RT5640_DRC_AGC_3:
	case RT5640_SVOL_ZC:
	case RT5640_ANC_CTRL1:
	case RT5640_ANC_CTRL2:
	case RT5640_ANC_CTRL3:
	case RT5640_JD_CTRL:
	case RT5640_ANC_JD:
	case RT5640_IRQ_CTRL1:
	case RT5640_IRQ_CTRL2:
	case RT5640_INT_IRQ_ST:
	case RT5640_GPIO_CTRL1:
	case RT5640_GPIO_CTRL2:
	case RT5640_GPIO_CTRL3:
	case RT5640_DSP_CTRL1:
	case RT5640_DSP_CTRL2:
	case RT5640_DSP_CTRL3:
	case RT5640_DSP_CTRL4:
	case RT5640_PGM_REG_ARR1:
	case RT5640_PGM_REG_ARR2:
	case RT5640_PGM_REG_ARR3:
	case RT5640_PGM_REG_ARR4:
	case RT5640_PGM_REG_ARR5:
	case RT5640_SCB_FUNC:
	case RT5640_SCB_CTRL:
	case RT5640_BASE_BACK:
	case RT5640_MP3_PLUS1:
	case RT5640_MP3_PLUS2:
	case RT5640_3D_HP:
	case RT5640_ADJ_HPF:
	case RT5640_HP_CALIB_AMP_DET:
	case RT5640_HP_CALIB2:
	case RT5640_SV_ZCD1:
	case RT5640_SV_ZCD2:
	case RT5640_DUMMY1:
	case RT5640_DUMMY2:
	case RT5640_DUMMY3:
	case RT5640_VENDOR_ID:
	case RT5640_VENDOR_ID1:
	case RT5640_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(dac_vol_tlv, -65625, 375, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_SCALE(adc_vol_tlv, -17625, 375, 0);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static unsigned int bst_tlv[] = {
	TLV_DB_RANGE_HEAD(7),
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0),
};

/* Interface data select */
static const char * const rt5640_data_select[] = {
	"Normal", "left copy to right", "right copy to left", "Swap"};

static SOC_ENUM_SINGLE_DECL(rt5640_if1_dac_enum, RT5640_DIG_INF_DATA,
			    RT5640_IF1_DAC_SEL_SFT, rt5640_data_select);

static SOC_ENUM_SINGLE_DECL(rt5640_if1_adc_enum, RT5640_DIG_INF_DATA,
			    RT5640_IF1_ADC_SEL_SFT, rt5640_data_select);

static SOC_ENUM_SINGLE_DECL(rt5640_if2_dac_enum, RT5640_DIG_INF_DATA,
			    RT5640_IF2_DAC_SEL_SFT, rt5640_data_select);

static SOC_ENUM_SINGLE_DECL(rt5640_if2_adc_enum, RT5640_DIG_INF_DATA,
			    RT5640_IF2_ADC_SEL_SFT, rt5640_data_select);

/* Class D speaker gain ratio */
static const char * const rt5640_clsd_spk_ratio[] = {"1.66x", "1.83x", "1.94x",
	"2x", "2.11x", "2.22x", "2.33x", "2.44x", "2.55x", "2.66x", "2.77x"};

static SOC_ENUM_SINGLE_DECL(rt5640_clsd_spk_ratio_enum, RT5640_CLS_D_OUT,
			    RT5640_CLSD_RATIO_SFT, rt5640_clsd_spk_ratio);

static const struct snd_kcontrol_new rt5640_snd_controls[] = {
	/* Speaker Output Volume */
	SOC_DOUBLE("Speaker Channel Switch", RT5640_SPK_VOL,
		RT5640_VOL_L_SFT, RT5640_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("Speaker Playback Volume", RT5640_SPK_VOL,
		RT5640_L_VOL_SFT, RT5640_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* Headphone Output Volume */
	SOC_DOUBLE("HP Channel Switch", RT5640_HP_VOL,
		RT5640_VOL_L_SFT, RT5640_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("HP Playback Volume", RT5640_HP_VOL,
		RT5640_L_VOL_SFT, RT5640_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* OUTPUT Control */
	SOC_DOUBLE("OUT Playback Switch", RT5640_OUTPUT,
		RT5640_L_MUTE_SFT, RT5640_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("OUT Channel Switch", RT5640_OUTPUT,
		RT5640_VOL_L_SFT, RT5640_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5640_OUTPUT,
		RT5640_L_VOL_SFT, RT5640_R_VOL_SFT, 39, 1, out_vol_tlv),

	/* DAC Digital Volume */
	SOC_DOUBLE("DAC2 Playback Switch", RT5640_DAC2_CTRL,
		RT5640_M_DAC_L2_VOL_SFT, RT5640_M_DAC_R2_VOL_SFT, 1, 1),
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5640_DAC1_DIG_VOL,
			RT5640_L_VOL_SFT, RT5640_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	/* IN1/IN2 Control */
	SOC_SINGLE_TLV("IN1 Boost", RT5640_IN1_IN2,
		RT5640_BST_SFT1, 8, 0, bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost", RT5640_IN3_IN4,
		RT5640_BST_SFT2, 8, 0, bst_tlv),
	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT5640_INL_INR_VOL,
			RT5640_INL_VOL_SFT, RT5640_INR_VOL_SFT,
			31, 1, in_vol_tlv),
	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT5640_ADC_DIG_VOL,
		RT5640_L_MUTE_SFT, RT5640_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT5640_ADC_DIG_VOL,
			RT5640_L_VOL_SFT, RT5640_R_VOL_SFT,
			127, 0, adc_vol_tlv),
	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT5640_ADC_DATA,
			RT5640_L_VOL_SFT, RT5640_R_VOL_SFT,
			127, 0, adc_vol_tlv),
	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("ADC Boost Gain", RT5640_ADC_BST_VOL,
			RT5640_ADC_L_BST_SFT, RT5640_ADC_R_BST_SFT,
			3, 0, adc_bst_tlv),
	/* Class D speaker gain ratio */
	SOC_ENUM("Class D SPK Ratio Control", rt5640_clsd_spk_ratio_enum),

	SOC_ENUM("ADC IF1 Data Switch", rt5640_if1_adc_enum),
	SOC_ENUM("DAC IF1 Data Switch", rt5640_if1_dac_enum),
	SOC_ENUM("ADC IF2 Data Switch", rt5640_if2_adc_enum),
	SOC_ENUM("DAC IF2 Data Switch", rt5640_if2_dac_enum),
};

static const struct snd_kcontrol_new rt5640_specific_snd_controls[] = {
	/* MONO Output Control */
	SOC_SINGLE("Mono Playback Switch", RT5640_MONO_OUT, RT5640_L_MUTE_SFT,
		1, 1),

	SOC_DOUBLE_TLV("Mono DAC Playback Volume", RT5640_DAC2_DIG_VOL,
		RT5640_L_VOL_SFT, RT5640_R_VOL_SFT, 175, 0, dac_vol_tlv),
};

/**
 * set_dmic_clk - Set parameter of dmic.
 *
 * @w: DAPM widget.
 * @kcontrol: The kcontrol of this widget.
 * @event: Event id.
 *
 */
static int set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);
	int idx = -EINVAL;

	idx = rl6231_calc_dmic_clk(rt5640->sysclk);

	if (idx < 0)
		dev_err(codec->dev, "Failed to set DMIC clock\n");
	else
		snd_soc_update_bits(codec, RT5640_DMIC, RT5640_DMIC_CLK_MASK,
					idx << RT5640_DMIC_CLK_SFT);
	return idx;
}

static int is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	unsigned int val;

	val = snd_soc_read(source->codec, RT5640_GLB_CLK);
	val &= RT5640_SCLK_SRC_MASK;
	if (val == RT5640_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5640_sto_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5640_STO_ADC_MIXER,
			RT5640_M_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5640_STO_ADC_MIXER,
			RT5640_M_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_sto_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5640_STO_ADC_MIXER,
			RT5640_M_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5640_STO_ADC_MIXER,
			RT5640_M_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_mono_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5640_MONO_ADC_MIXER,
			RT5640_M_MONO_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5640_MONO_ADC_MIXER,
			RT5640_M_MONO_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_mono_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5640_MONO_ADC_MIXER,
			RT5640_M_MONO_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5640_MONO_ADC_MIXER,
			RT5640_M_MONO_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5640_AD_DA_MIXER,
			RT5640_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT5640_AD_DA_MIXER,
			RT5640_M_IF1_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5640_AD_DA_MIXER,
			RT5640_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT5640_AD_DA_MIXER,
			RT5640_M_IF1_DAC_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_L2_SFT, 1, 1),
	SOC_DAPM_SINGLE("ANC Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_ANC_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_R2_SFT, 1, 1),
	SOC_DAPM_SINGLE("ANC Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_ANC_DAC_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5639_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5639_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_STO_DAC_MIXER,
			RT5640_M_DAC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_mono_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_MONO_DAC_MIXER,
			RT5640_M_DAC_L1_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_MONO_DAC_MIXER,
			RT5640_M_DAC_L2_MONO_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_MONO_DAC_MIXER,
			RT5640_M_DAC_R2_MONO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_mono_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_MONO_DAC_MIXER,
			RT5640_M_DAC_R1_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_MONO_DAC_MIXER,
			RT5640_M_DAC_R2_MONO_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_MONO_DAC_MIXER,
			RT5640_M_DAC_L2_MONO_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_dig_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_DIG_MIXER,
			RT5640_M_STO_L_DAC_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_DIG_MIXER,
			RT5640_M_DAC_L2_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_dig_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_DIG_MIXER,
			RT5640_M_STO_R_DAC_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_DIG_MIXER,
			RT5640_M_DAC_R2_DAC_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5640_rec_l_mix[] = {
	SOC_DAPM_SINGLE("HPOL Switch", RT5640_REC_L2_MIXER,
			RT5640_M_HP_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5640_REC_L2_MIXER,
			RT5640_M_IN_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5640_REC_L2_MIXER,
			RT5640_M_BST4_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_REC_L2_MIXER,
			RT5640_M_BST1_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXL Switch", RT5640_REC_L2_MIXER,
			RT5640_M_OM_L_RM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_rec_r_mix[] = {
	SOC_DAPM_SINGLE("HPOR Switch", RT5640_REC_R2_MIXER,
			RT5640_M_HP_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5640_REC_R2_MIXER,
			RT5640_M_IN_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5640_REC_R2_MIXER,
			RT5640_M_BST4_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_REC_R2_MIXER,
			RT5640_M_BST1_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXR Switch", RT5640_REC_R2_MIXER,
			RT5640_M_OM_R_RM_R_SFT, 1, 1),
};

/* Analog Output Mixer */
static const struct snd_kcontrol_new rt5640_spk_l_mix[] = {
	SOC_DAPM_SINGLE("REC MIXL Switch", RT5640_SPK_L_MIXER,
			RT5640_M_RM_L_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5640_SPK_L_MIXER,
			RT5640_M_IN_L_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_SPK_L_MIXER,
			RT5640_M_DAC_L1_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_SPK_L_MIXER,
			RT5640_M_DAC_L2_SM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXL Switch", RT5640_SPK_L_MIXER,
			RT5640_M_OM_L_SM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_spk_r_mix[] = {
	SOC_DAPM_SINGLE("REC MIXR Switch", RT5640_SPK_R_MIXER,
			RT5640_M_RM_R_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5640_SPK_R_MIXER,
			RT5640_M_IN_R_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_SPK_R_MIXER,
			RT5640_M_DAC_R1_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_SPK_R_MIXER,
			RT5640_M_DAC_R2_SM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXR Switch", RT5640_SPK_R_MIXER,
			RT5640_M_OM_R_SM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_out_l_mix[] = {
	SOC_DAPM_SINGLE("SPK MIXL Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_SM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_IN_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXL Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_RM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_DAC_R2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_DAC_L2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_DAC_L1_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_out_r_mix[] = {
	SOC_DAPM_SINGLE("SPK MIXR Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_SM_L_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_BST4_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_BST1_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_IN_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXR Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_RM_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_DAC_L2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_DAC_R2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_DAC_R1_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5639_out_l_mix[] = {
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_IN_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXL Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_RM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_OUT_L3_MIXER,
			RT5640_M_DAC_L1_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5639_out_r_mix[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_BST4_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_BST1_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_IN_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXR Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_RM_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_OUT_R3_MIXER,
			RT5640_M_DAC_R1_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_spo_l_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_SPO_L_MIXER,
			RT5640_M_DAC_R1_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_SPO_L_MIXER,
			RT5640_M_DAC_L1_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL R Switch", RT5640_SPO_L_MIXER,
			RT5640_M_SV_R_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL L Switch", RT5640_SPO_L_MIXER,
			RT5640_M_SV_L_SPM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_SPO_L_MIXER,
			RT5640_M_BST1_SPM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_spo_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_SPO_R_MIXER,
			RT5640_M_DAC_R1_SPM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL R Switch", RT5640_SPO_R_MIXER,
			RT5640_M_SV_R_SPM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_SPO_R_MIXER,
			RT5640_M_BST1_SPM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_hpo_mix[] = {
	SOC_DAPM_SINGLE("HPO MIX DAC2 Switch", RT5640_HPO_MIXER,
			RT5640_M_DAC2_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPO MIX DAC1 Switch", RT5640_HPO_MIXER,
			RT5640_M_DAC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPO MIX HPVOL Switch", RT5640_HPO_MIXER,
			RT5640_M_HPVOL_HM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5639_hpo_mix[] = {
	SOC_DAPM_SINGLE("HPO MIX DAC1 Switch", RT5640_HPO_MIXER,
			RT5640_M_DAC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPO MIX HPVOL Switch", RT5640_HPO_MIXER,
			RT5640_M_HPVOL_HM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_lout_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5640_LOUT_MIXER,
			RT5640_M_DAC_L1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5640_LOUT_MIXER,
			RT5640_M_DAC_R1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT5640_LOUT_MIXER,
			RT5640_M_OV_L_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT5640_LOUT_MIXER,
			RT5640_M_OV_R_LM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5640_mono_mix[] = {
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5640_MONO_MIXER,
			RT5640_M_DAC_R2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5640_MONO_MIXER,
			RT5640_M_DAC_L2_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT5640_MONO_MIXER,
			RT5640_M_OV_R_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT5640_MONO_MIXER,
			RT5640_M_OV_L_MM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5640_MONO_MIXER,
			RT5640_M_BST1_MM_SFT, 1, 1),
};

static const struct snd_kcontrol_new spk_l_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5640_SPK_VOL,
		RT5640_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new spk_r_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5640_SPK_VOL,
		RT5640_R_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hp_l_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5640_HP_VOL,
		RT5640_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hp_r_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5640_HP_VOL,
		RT5640_R_MUTE_SFT, 1, 1);

/* Stereo ADC source */
static const char * const rt5640_stereo_adc1_src[] = {
	"DIG MIX", "ADC"
};

static SOC_ENUM_SINGLE_DECL(rt5640_stereo_adc1_enum, RT5640_STO_ADC_MIXER,
			    RT5640_ADC_1_SRC_SFT, rt5640_stereo_adc1_src);

static const struct snd_kcontrol_new rt5640_sto_adc_1_mux =
	SOC_DAPM_ENUM("Stereo ADC1 Mux", rt5640_stereo_adc1_enum);

static const char * const rt5640_stereo_adc2_src[] = {
	"DMIC1", "DMIC2", "DIG MIX"
};

static SOC_ENUM_SINGLE_DECL(rt5640_stereo_adc2_enum, RT5640_STO_ADC_MIXER,
			    RT5640_ADC_2_SRC_SFT, rt5640_stereo_adc2_src);

static const struct snd_kcontrol_new rt5640_sto_adc_2_mux =
	SOC_DAPM_ENUM("Stereo ADC2 Mux", rt5640_stereo_adc2_enum);

/* Mono ADC source */
static const char * const rt5640_mono_adc_l1_src[] = {
	"Mono DAC MIXL", "ADCL"
};

static SOC_ENUM_SINGLE_DECL(rt5640_mono_adc_l1_enum, RT5640_MONO_ADC_MIXER,
			    RT5640_MONO_ADC_L1_SRC_SFT, rt5640_mono_adc_l1_src);

static const struct snd_kcontrol_new rt5640_mono_adc_l1_mux =
	SOC_DAPM_ENUM("Mono ADC1 left source", rt5640_mono_adc_l1_enum);

static const char * const rt5640_mono_adc_l2_src[] = {
	"DMIC L1", "DMIC L2", "Mono DAC MIXL"
};

static SOC_ENUM_SINGLE_DECL(rt5640_mono_adc_l2_enum, RT5640_MONO_ADC_MIXER,
			    RT5640_MONO_ADC_L2_SRC_SFT, rt5640_mono_adc_l2_src);

static const struct snd_kcontrol_new rt5640_mono_adc_l2_mux =
	SOC_DAPM_ENUM("Mono ADC2 left source", rt5640_mono_adc_l2_enum);

static const char * const rt5640_mono_adc_r1_src[] = {
	"Mono DAC MIXR", "ADCR"
};

static SOC_ENUM_SINGLE_DECL(rt5640_mono_adc_r1_enum, RT5640_MONO_ADC_MIXER,
			    RT5640_MONO_ADC_R1_SRC_SFT, rt5640_mono_adc_r1_src);

static const struct snd_kcontrol_new rt5640_mono_adc_r1_mux =
	SOC_DAPM_ENUM("Mono ADC1 right source", rt5640_mono_adc_r1_enum);

static const char * const rt5640_mono_adc_r2_src[] = {
	"DMIC R1", "DMIC R2", "Mono DAC MIXR"
};

static SOC_ENUM_SINGLE_DECL(rt5640_mono_adc_r2_enum, RT5640_MONO_ADC_MIXER,
			    RT5640_MONO_ADC_R2_SRC_SFT, rt5640_mono_adc_r2_src);

static const struct snd_kcontrol_new rt5640_mono_adc_r2_mux =
	SOC_DAPM_ENUM("Mono ADC2 right source", rt5640_mono_adc_r2_enum);

/* DAC2 channel source */
static const char * const rt5640_dac_l2_src[] = {
	"IF2", "Base L/R"
};

static int rt5640_dac_l2_values[] = {
	0,
	3,
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5640_dac_l2_enum,
				  RT5640_DSP_PATH2, RT5640_DAC_L2_SEL_SFT,
				  0x3, rt5640_dac_l2_src, rt5640_dac_l2_values);

static const struct snd_kcontrol_new rt5640_dac_l2_mux =
	SOC_DAPM_ENUM("DAC2 left channel source", rt5640_dac_l2_enum);

static const char * const rt5640_dac_r2_src[] = {
	"IF2",
};

static int rt5640_dac_r2_values[] = {
	0,
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5640_dac_r2_enum,
				  RT5640_DSP_PATH2, RT5640_DAC_R2_SEL_SFT,
				  0x3, rt5640_dac_r2_src, rt5640_dac_r2_values);

static const struct snd_kcontrol_new rt5640_dac_r2_mux =
	SOC_DAPM_ENUM("DAC2 right channel source", rt5640_dac_r2_enum);

/* digital interface and iis interface map */
static const char * const rt5640_dai_iis_map[] = {
	"1:1|2:2", "1:2|2:1", "1:1|2:1", "1:2|2:2"
};

static int rt5640_dai_iis_map_values[] = {
	0,
	5,
	6,
	7,
};

static SOC_VALUE_ENUM_SINGLE_DECL(rt5640_dai_iis_map_enum,
				  RT5640_I2S1_SDP, RT5640_I2S_IF_SFT,
				  0x7, rt5640_dai_iis_map,
				  rt5640_dai_iis_map_values);

static const struct snd_kcontrol_new rt5640_dai_mux =
	SOC_DAPM_ENUM("DAI select", rt5640_dai_iis_map_enum);

/* SDI select */
static const char * const rt5640_sdi_sel[] = {
	"IF1", "IF2"
};

static SOC_ENUM_SINGLE_DECL(rt5640_sdi_sel_enum, RT5640_I2S2_SDP,
			    RT5640_I2S2_SDI_SFT, rt5640_sdi_sel);

static const struct snd_kcontrol_new rt5640_sdi_mux =
	SOC_DAPM_ENUM("SDI select", rt5640_sdi_sel_enum);

static void hp_amp_power_on(struct snd_soc_codec *codec)
{
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);

	/* depop parameters */
	regmap_update_bits(rt5640->regmap, RT5640_PR_BASE +
		RT5640_CHPUMP_INT_REG1, 0x0700, 0x0200);
	regmap_update_bits(rt5640->regmap, RT5640_DEPOP_M2,
		RT5640_DEPOP_MASK, RT5640_DEPOP_MAN);
	regmap_update_bits(rt5640->regmap, RT5640_DEPOP_M1,
		RT5640_HP_CP_MASK | RT5640_HP_SG_MASK | RT5640_HP_CB_MASK,
		RT5640_HP_CP_PU | RT5640_HP_SG_DIS | RT5640_HP_CB_PU);
	regmap_write(rt5640->regmap, RT5640_PR_BASE + RT5640_HP_DCC_INT1,
			   0x9f00);
	/* headphone amp power on */
	regmap_update_bits(rt5640->regmap, RT5640_PWR_ANLG1,
		RT5640_PWR_FV1 | RT5640_PWR_FV2, 0);
	regmap_update_bits(rt5640->regmap, RT5640_PWR_ANLG1,
		RT5640_PWR_HA,
		RT5640_PWR_HA);
	usleep_range(10000, 15000);
	regmap_update_bits(rt5640->regmap, RT5640_PWR_ANLG1,
		RT5640_PWR_FV1 | RT5640_PWR_FV2 ,
		RT5640_PWR_FV1 | RT5640_PWR_FV2);
}

static void rt5640_pmu_depop(struct snd_soc_codec *codec)
{
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);

	regmap_update_bits(rt5640->regmap, RT5640_DEPOP_M2,
		RT5640_DEPOP_MASK | RT5640_DIG_DP_MASK,
		RT5640_DEPOP_AUTO | RT5640_DIG_DP_EN);
	regmap_update_bits(rt5640->regmap, RT5640_CHARGE_PUMP,
		RT5640_PM_HP_MASK, RT5640_PM_HP_HV);

	regmap_update_bits(rt5640->regmap, RT5640_DEPOP_M3,
		RT5640_CP_FQ1_MASK | RT5640_CP_FQ2_MASK | RT5640_CP_FQ3_MASK,
		(RT5640_CP_FQ_192_KHZ << RT5640_CP_FQ1_SFT) |
		(RT5640_CP_FQ_12_KHZ << RT5640_CP_FQ2_SFT) |
		(RT5640_CP_FQ_192_KHZ << RT5640_CP_FQ3_SFT));

	regmap_write(rt5640->regmap, RT5640_PR_BASE +
		RT5640_MAMP_INT_REG2, 0x1c00);
	regmap_update_bits(rt5640->regmap, RT5640_DEPOP_M1,
		RT5640_HP_CP_MASK | RT5640_HP_SG_MASK,
		RT5640_HP_CP_PD | RT5640_HP_SG_EN);
	regmap_update_bits(rt5640->regmap, RT5640_PR_BASE +
		RT5640_CHPUMP_INT_REG1, 0x0700, 0x0400);
}

static int rt5640_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt5640_pmu_depop(codec);
		rt5640->hp_mute = 0;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		rt5640->hp_mute = 1;
		usleep_range(70000, 75000);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5640_hp_power_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		hp_amp_power_on(codec);
		break;
	default:
		return 0;
	}

	return 0;
}

static int rt5640_hp_post_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (!rt5640->hp_mute)
			usleep_range(80000, 85000);

		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5640_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL1", RT5640_PWR_ANLG2,
			RT5640_PWR_PLL_BIT, 0, NULL, 0),
	/* Input Side */
	/* micbias */
	SND_SOC_DAPM_SUPPLY("LDO2", RT5640_PWR_ANLG1,
			RT5640_PWR_LDO2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5640_PWR_ANLG2,
			RT5640_PWR_MB1_BIT, 0, NULL, 0),
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),
	SND_SOC_DAPM_PGA("DMIC L1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC R1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC L2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DMIC R2", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC1 Power", RT5640_DMIC, RT5640_DMIC_1_EN_SFT, 0,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC2 Power", RT5640_DMIC, RT5640_DMIC_2_EN_SFT, 0,
		NULL, 0),
	/* Boost */
	SND_SOC_DAPM_PGA("BST1", RT5640_PWR_ANLG2,
		RT5640_PWR_BST1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("BST2", RT5640_PWR_ANLG2,
		RT5640_PWR_BST4_BIT, 0, NULL, 0),
	/* Input Volume */
	SND_SOC_DAPM_PGA("INL VOL", RT5640_PWR_VOL,
		RT5640_PWR_IN_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR VOL", RT5640_PWR_VOL,
		RT5640_PWR_IN_R_BIT, 0, NULL, 0),
	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL", RT5640_PWR_MIXER, RT5640_PWR_RM_L_BIT, 0,
			rt5640_rec_l_mix, ARRAY_SIZE(rt5640_rec_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIXR", RT5640_PWR_MIXER, RT5640_PWR_RM_R_BIT, 0,
			rt5640_rec_r_mix, ARRAY_SIZE(rt5640_rec_r_mix)),
	/* ADCs */
	SND_SOC_DAPM_ADC("ADC L", NULL, RT5640_PWR_DIG1,
			RT5640_PWR_ADC_L_BIT, 0),
	SND_SOC_DAPM_ADC("ADC R", NULL, RT5640_PWR_DIG1,
			RT5640_PWR_ADC_R_BIT, 0),
	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo ADC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_sto_adc_2_mux),
	SND_SOC_DAPM_MUX("Stereo ADC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_sto_adc_2_mux),
	SND_SOC_DAPM_MUX("Stereo ADC L1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_sto_adc_1_mux),
	SND_SOC_DAPM_MUX("Stereo ADC R1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_sto_adc_1_mux),
	SND_SOC_DAPM_MUX("Mono ADC L2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_mono_adc_l2_mux),
	SND_SOC_DAPM_MUX("Mono ADC L1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_mono_adc_l1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R1 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_mono_adc_r1_mux),
	SND_SOC_DAPM_MUX("Mono ADC R2 Mux", SND_SOC_NOPM, 0, 0,
				&rt5640_mono_adc_r2_mux),
	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("Stereo Filter", RT5640_PWR_DIG2,
		RT5640_PWR_ADC_SF_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5640_sto_adc_l_mix, ARRAY_SIZE(rt5640_sto_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5640_sto_adc_r_mix, ARRAY_SIZE(rt5640_sto_adc_r_mix)),
	SND_SOC_DAPM_SUPPLY("Mono Left Filter", RT5640_PWR_DIG2,
		RT5640_PWR_ADC_MF_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5640_mono_adc_l_mix, ARRAY_SIZE(rt5640_mono_adc_l_mix)),
	SND_SOC_DAPM_SUPPLY("Mono Right Filter", RT5640_PWR_DIG2,
		RT5640_PWR_ADC_MF_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Mono ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5640_mono_adc_r_mix, ARRAY_SIZE(rt5640_mono_adc_r_mix)),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5640_PWR_DIG1,
		RT5640_PWR_I2S1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2", RT5640_PWR_DIG1,
		RT5640_PWR_I2S2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 ADC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	/* Digital Interface Select */
	SND_SOC_DAPM_MUX("DAI1 RX Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("DAI1 TX Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("DAI1 IF1 Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("DAI1 IF2 Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("SDI1 TX Mux", SND_SOC_NOPM, 0, 0, &rt5640_sdi_mux),
	SND_SOC_DAPM_MUX("DAI2 RX Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("DAI2 TX Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("DAI2 IF1 Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("DAI2 IF2 Mux", SND_SOC_NOPM, 0, 0, &rt5640_dai_mux),
	SND_SOC_DAPM_MUX("SDI2 TX Mux", SND_SOC_NOPM, 0, 0, &rt5640_sdi_mux),
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5640_dac_l_mix, ARRAY_SIZE(rt5640_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5640_dac_r_mix, ARRAY_SIZE(rt5640_dac_r_mix)),

	/* DAC Mixer */
	SND_SOC_DAPM_MIXER("Mono DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5640_mono_dac_l_mix, ARRAY_SIZE(rt5640_mono_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Mono DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5640_mono_dac_r_mix, ARRAY_SIZE(rt5640_mono_dac_r_mix)),
	SND_SOC_DAPM_MIXER("DIG MIXL", SND_SOC_NOPM, 0, 0,
		rt5640_dig_l_mix, ARRAY_SIZE(rt5640_dig_l_mix)),
	SND_SOC_DAPM_MIXER("DIG MIXR", SND_SOC_NOPM, 0, 0,
		rt5640_dig_r_mix, ARRAY_SIZE(rt5640_dig_r_mix)),
	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, RT5640_PWR_DIG1,
			RT5640_PWR_DAC_L1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, RT5640_PWR_DIG1,
			RT5640_PWR_DAC_R1_BIT, 0),

	/* SPK/OUT Mixer */
	SND_SOC_DAPM_MIXER("SPK MIXL", RT5640_PWR_MIXER, RT5640_PWR_SM_L_BIT,
		0, rt5640_spk_l_mix, ARRAY_SIZE(rt5640_spk_l_mix)),
	SND_SOC_DAPM_MIXER("SPK MIXR", RT5640_PWR_MIXER, RT5640_PWR_SM_R_BIT,
		0, rt5640_spk_r_mix, ARRAY_SIZE(rt5640_spk_r_mix)),
	/* Ouput Volume */
	SND_SOC_DAPM_PGA("SPKVOL L", RT5640_PWR_VOL,
		RT5640_PWR_SV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("SPKVOL R", RT5640_PWR_VOL,
		RT5640_PWR_SV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OUTVOL L", RT5640_PWR_VOL,
		RT5640_PWR_OV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OUTVOL R", RT5640_PWR_VOL,
		RT5640_PWR_OV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL L", RT5640_PWR_VOL,
		RT5640_PWR_HV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL R", RT5640_PWR_VOL,
		RT5640_PWR_HV_R_BIT, 0, NULL, 0),
	/* SPO/HPO/LOUT/Mono Mixer */
	SND_SOC_DAPM_MIXER("SPOL MIX", SND_SOC_NOPM, 0,
		0, rt5640_spo_l_mix, ARRAY_SIZE(rt5640_spo_l_mix)),
	SND_SOC_DAPM_MIXER("SPOR MIX", SND_SOC_NOPM, 0,
		0, rt5640_spo_r_mix, ARRAY_SIZE(rt5640_spo_r_mix)),
	SND_SOC_DAPM_MIXER("LOUT MIX", RT5640_PWR_ANLG1, RT5640_PWR_LM_BIT, 0,
		rt5640_lout_mix, ARRAY_SIZE(rt5640_lout_mix)),
	SND_SOC_DAPM_SUPPLY_S("Improve HP Amp Drv", 1, SND_SOC_NOPM,
		0, 0, rt5640_hp_power_event, SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0,
		rt5640_hp_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("HP L Amp", RT5640_PWR_ANLG1,
		RT5640_PWR_HP_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("HP R Amp", RT5640_PWR_ANLG1,
		RT5640_PWR_HP_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Improve SPK Amp Drv", RT5640_PWR_DIG1,
		RT5640_PWR_CLS_D_BIT, 0, NULL, 0),

	/* Output Switch */
	SND_SOC_DAPM_SWITCH("Speaker L Playback", SND_SOC_NOPM, 0, 0,
			&spk_l_enable_control),
	SND_SOC_DAPM_SWITCH("Speaker R Playback", SND_SOC_NOPM, 0, 0,
			&spk_r_enable_control),
	SND_SOC_DAPM_SWITCH("HP L Playback", SND_SOC_NOPM, 0, 0,
			&hp_l_enable_control),
	SND_SOC_DAPM_SWITCH("HP R Playback", SND_SOC_NOPM, 0, 0,
			&hp_r_enable_control),
	SND_SOC_DAPM_POST("HP Post", rt5640_hp_post_event),
	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("SPOLP"),
	SND_SOC_DAPM_OUTPUT("SPOLN"),
	SND_SOC_DAPM_OUTPUT("SPORP"),
	SND_SOC_DAPM_OUTPUT("SPORN"),
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
};

static const struct snd_soc_dapm_widget rt5640_specific_dapm_widgets[] = {
	/* Audio DSP */
	SND_SOC_DAPM_PGA("Audio DSP", SND_SOC_NOPM, 0, 0, NULL, 0),
	/* ANC */
	SND_SOC_DAPM_PGA("ANC", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DAC2 channel Mux */
	SND_SOC_DAPM_MUX("DAC L2 Mux", SND_SOC_NOPM, 0, 0, &rt5640_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Mux", SND_SOC_NOPM, 0, 0, &rt5640_dac_r2_mux),

	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5640_sto_dac_l_mix, ARRAY_SIZE(rt5640_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5640_sto_dac_r_mix, ARRAY_SIZE(rt5640_sto_dac_r_mix)),

	SND_SOC_DAPM_DAC("DAC R2", NULL, RT5640_PWR_DIG1, RT5640_PWR_DAC_R2_BIT,
		0),
	SND_SOC_DAPM_DAC("DAC L2", NULL, RT5640_PWR_DIG1, RT5640_PWR_DAC_L2_BIT,
		0),

	SND_SOC_DAPM_MIXER("OUT MIXL", RT5640_PWR_MIXER, RT5640_PWR_OM_L_BIT,
		0, rt5640_out_l_mix, ARRAY_SIZE(rt5640_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5640_PWR_MIXER, RT5640_PWR_OM_R_BIT,
		0, rt5640_out_r_mix, ARRAY_SIZE(rt5640_out_r_mix)),

	SND_SOC_DAPM_MIXER("HPO MIX L", SND_SOC_NOPM, 0, 0,
		rt5640_hpo_mix, ARRAY_SIZE(rt5640_hpo_mix)),
	SND_SOC_DAPM_MIXER("HPO MIX R", SND_SOC_NOPM, 0, 0,
		rt5640_hpo_mix, ARRAY_SIZE(rt5640_hpo_mix)),

	SND_SOC_DAPM_MIXER("Mono MIX", RT5640_PWR_ANLG1, RT5640_PWR_MM_BIT, 0,
		rt5640_mono_mix, ARRAY_SIZE(rt5640_mono_mix)),
	SND_SOC_DAPM_SUPPLY("Improve MONO Amp Drv", RT5640_PWR_ANLG1,
		RT5640_PWR_MA_BIT, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("MONOP"),
	SND_SOC_DAPM_OUTPUT("MONON"),
};

static const struct snd_soc_dapm_widget rt5639_specific_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5639_sto_dac_l_mix, ARRAY_SIZE(rt5639_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5639_sto_dac_r_mix, ARRAY_SIZE(rt5639_sto_dac_r_mix)),

	SND_SOC_DAPM_SUPPLY("DAC L2 Filter", RT5640_PWR_DIG1,
		RT5640_PWR_DAC_L2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R2 Filter", RT5640_PWR_DIG1,
		RT5640_PWR_DAC_R2_BIT, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("OUT MIXL", RT5640_PWR_MIXER, RT5640_PWR_OM_L_BIT,
		0, rt5639_out_l_mix, ARRAY_SIZE(rt5639_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5640_PWR_MIXER, RT5640_PWR_OM_R_BIT,
		0, rt5639_out_r_mix, ARRAY_SIZE(rt5639_out_r_mix)),

	SND_SOC_DAPM_MIXER("HPO MIX L", SND_SOC_NOPM, 0, 0,
		rt5639_hpo_mix, ARRAY_SIZE(rt5639_hpo_mix)),
	SND_SOC_DAPM_MIXER("HPO MIX R", SND_SOC_NOPM, 0, 0,
		rt5639_hpo_mix, ARRAY_SIZE(rt5639_hpo_mix)),
};

static const struct snd_soc_dapm_route rt5640_dapm_routes[] = {
	{"IN1P", NULL, "LDO2"},
	{"IN2P", NULL, "LDO2"},

	{"DMIC L1", NULL, "DMIC1"},
	{"DMIC R1", NULL, "DMIC1"},
	{"DMIC L2", NULL, "DMIC2"},
	{"DMIC R2", NULL, "DMIC2"},

	{"BST1", NULL, "IN1P"},
	{"BST1", NULL, "IN1N"},
	{"BST2", NULL, "IN2P"},
	{"BST2", NULL, "IN2N"},

	{"INL VOL", NULL, "IN2P"},
	{"INR VOL", NULL, "IN2N"},

	{"RECMIXL", "HPOL Switch", "HPOL"},
	{"RECMIXL", "INL Switch", "INL VOL"},
	{"RECMIXL", "BST2 Switch", "BST2"},
	{"RECMIXL", "BST1 Switch", "BST1"},
	{"RECMIXL", "OUT MIXL Switch", "OUT MIXL"},

	{"RECMIXR", "HPOR Switch", "HPOR"},
	{"RECMIXR", "INR Switch", "INR VOL"},
	{"RECMIXR", "BST2 Switch", "BST2"},
	{"RECMIXR", "BST1 Switch", "BST1"},
	{"RECMIXR", "OUT MIXR Switch", "OUT MIXR"},

	{"ADC L", NULL, "RECMIXL"},
	{"ADC R", NULL, "RECMIXR"},

	{"DMIC L1", NULL, "DMIC CLK"},
	{"DMIC L1", NULL, "DMIC1 Power"},
	{"DMIC R1", NULL, "DMIC CLK"},
	{"DMIC R1", NULL, "DMIC1 Power"},
	{"DMIC L2", NULL, "DMIC CLK"},
	{"DMIC L2", NULL, "DMIC2 Power"},
	{"DMIC R2", NULL, "DMIC CLK"},
	{"DMIC R2", NULL, "DMIC2 Power"},

	{"Stereo ADC L2 Mux", "DMIC1", "DMIC L1"},
	{"Stereo ADC L2 Mux", "DMIC2", "DMIC L2"},
	{"Stereo ADC L2 Mux", "DIG MIX", "DIG MIXL"},
	{"Stereo ADC L1 Mux", "ADC", "ADC L"},
	{"Stereo ADC L1 Mux", "DIG MIX", "DIG MIXL"},

	{"Stereo ADC R1 Mux", "ADC", "ADC R"},
	{"Stereo ADC R1 Mux", "DIG MIX", "DIG MIXR"},
	{"Stereo ADC R2 Mux", "DMIC1", "DMIC R1"},
	{"Stereo ADC R2 Mux", "DMIC2", "DMIC R2"},
	{"Stereo ADC R2 Mux", "DIG MIX", "DIG MIXR"},

	{"Mono ADC L2 Mux", "DMIC L1", "DMIC L1"},
	{"Mono ADC L2 Mux", "DMIC L2", "DMIC L2"},
	{"Mono ADC L2 Mux", "Mono DAC MIXL", "Mono DAC MIXL"},
	{"Mono ADC L1 Mux", "Mono DAC MIXL", "Mono DAC MIXL"},
	{"Mono ADC L1 Mux", "ADCL", "ADC L"},

	{"Mono ADC R1 Mux", "Mono DAC MIXR", "Mono DAC MIXR"},
	{"Mono ADC R1 Mux", "ADCR", "ADC R"},
	{"Mono ADC R2 Mux", "DMIC R1", "DMIC R1"},
	{"Mono ADC R2 Mux", "DMIC R2", "DMIC R2"},
	{"Mono ADC R2 Mux", "Mono DAC MIXR", "Mono DAC MIXR"},

	{"Stereo ADC MIXL", "ADC1 Switch", "Stereo ADC L1 Mux"},
	{"Stereo ADC MIXL", "ADC2 Switch", "Stereo ADC L2 Mux"},
	{"Stereo ADC MIXL", NULL, "Stereo Filter"},
	{"Stereo Filter", NULL, "PLL1", is_sys_clk_from_pll},

	{"Stereo ADC MIXR", "ADC1 Switch", "Stereo ADC R1 Mux"},
	{"Stereo ADC MIXR", "ADC2 Switch", "Stereo ADC R2 Mux"},
	{"Stereo ADC MIXR", NULL, "Stereo Filter"},
	{"Stereo Filter", NULL, "PLL1", is_sys_clk_from_pll},

	{"Mono ADC MIXL", "ADC1 Switch", "Mono ADC L1 Mux"},
	{"Mono ADC MIXL", "ADC2 Switch", "Mono ADC L2 Mux"},
	{"Mono ADC MIXL", NULL, "Mono Left Filter"},
	{"Mono Left Filter", NULL, "PLL1", is_sys_clk_from_pll},

	{"Mono ADC MIXR", "ADC1 Switch", "Mono ADC R1 Mux"},
	{"Mono ADC MIXR", "ADC2 Switch", "Mono ADC R2 Mux"},
	{"Mono ADC MIXR", NULL, "Mono Right Filter"},
	{"Mono Right Filter", NULL, "PLL1", is_sys_clk_from_pll},

	{"IF2 ADC L", NULL, "Mono ADC MIXL"},
	{"IF2 ADC R", NULL, "Mono ADC MIXR"},
	{"IF1 ADC L", NULL, "Stereo ADC MIXL"},
	{"IF1 ADC R", NULL, "Stereo ADC MIXR"},

	{"IF1 ADC", NULL, "I2S1"},
	{"IF1 ADC", NULL, "IF1 ADC L"},
	{"IF1 ADC", NULL, "IF1 ADC R"},
	{"IF2 ADC", NULL, "I2S2"},
	{"IF2 ADC", NULL, "IF2 ADC L"},
	{"IF2 ADC", NULL, "IF2 ADC R"},

	{"DAI1 TX Mux", "1:1|2:2", "IF1 ADC"},
	{"DAI1 TX Mux", "1:2|2:1", "IF2 ADC"},
	{"DAI1 IF1 Mux", "1:1|2:1", "IF1 ADC"},
	{"DAI1 IF2 Mux", "1:1|2:1", "IF2 ADC"},
	{"SDI1 TX Mux", "IF1", "DAI1 IF1 Mux"},
	{"SDI1 TX Mux", "IF2", "DAI1 IF2 Mux"},

	{"DAI2 TX Mux", "1:2|2:1", "IF1 ADC"},
	{"DAI2 TX Mux", "1:1|2:2", "IF2 ADC"},
	{"DAI2 IF1 Mux", "1:2|2:2", "IF1 ADC"},
	{"DAI2 IF2 Mux", "1:2|2:2", "IF2 ADC"},
	{"SDI2 TX Mux", "IF1", "DAI2 IF1 Mux"},
	{"SDI2 TX Mux", "IF2", "DAI2 IF2 Mux"},

	{"AIF1TX", NULL, "DAI1 TX Mux"},
	{"AIF1TX", NULL, "SDI1 TX Mux"},
	{"AIF2TX", NULL, "DAI2 TX Mux"},
	{"AIF2TX", NULL, "SDI2 TX Mux"},

	{"DAI1 RX Mux", "1:1|2:2", "AIF1RX"},
	{"DAI1 RX Mux", "1:1|2:1", "AIF1RX"},
	{"DAI1 RX Mux", "1:2|2:1", "AIF2RX"},
	{"DAI1 RX Mux", "1:2|2:2", "AIF2RX"},

	{"DAI2 RX Mux", "1:2|2:1", "AIF1RX"},
	{"DAI2 RX Mux", "1:1|2:1", "AIF1RX"},
	{"DAI2 RX Mux", "1:1|2:2", "AIF2RX"},
	{"DAI2 RX Mux", "1:2|2:2", "AIF2RX"},

	{"IF1 DAC", NULL, "I2S1"},
	{"IF1 DAC", NULL, "DAI1 RX Mux"},
	{"IF2 DAC", NULL, "I2S2"},
	{"IF2 DAC", NULL, "DAI2 RX Mux"},

	{"IF1 DAC L", NULL, "IF1 DAC"},
	{"IF1 DAC R", NULL, "IF1 DAC"},
	{"IF2 DAC L", NULL, "IF2 DAC"},
	{"IF2 DAC R", NULL, "IF2 DAC"},

	{"DAC MIXL", "Stereo ADC Switch", "Stereo ADC MIXL"},
	{"DAC MIXL", "INF1 Switch", "IF1 DAC L"},
	{"DAC MIXR", "Stereo ADC Switch", "Stereo ADC MIXR"},
	{"DAC MIXR", "INF1 Switch", "IF1 DAC R"},

	{"Stereo DAC MIXL", "DAC L1 Switch", "DAC MIXL"},
	{"Stereo DAC MIXR", "DAC R1 Switch", "DAC MIXR"},

	{"Mono DAC MIXL", "DAC L1 Switch", "DAC MIXL"},
	{"Mono DAC MIXR", "DAC R1 Switch", "DAC MIXR"},

	{"DIG MIXL", "DAC L1 Switch", "DAC MIXL"},
	{"DIG MIXR", "DAC R1 Switch", "DAC MIXR"},

	{"DAC L1", NULL, "Stereo DAC MIXL"},
	{"DAC L1", NULL, "PLL1", is_sys_clk_from_pll},
	{"DAC R1", NULL, "Stereo DAC MIXR"},
	{"DAC R1", NULL, "PLL1", is_sys_clk_from_pll},

	{"SPK MIXL", "REC MIXL Switch", "RECMIXL"},
	{"SPK MIXL", "INL Switch", "INL VOL"},
	{"SPK MIXL", "DAC L1 Switch", "DAC L1"},
	{"SPK MIXL", "OUT MIXL Switch", "OUT MIXL"},
	{"SPK MIXR", "REC MIXR Switch", "RECMIXR"},
	{"SPK MIXR", "INR Switch", "INR VOL"},
	{"SPK MIXR", "DAC R1 Switch", "DAC R1"},
	{"SPK MIXR", "OUT MIXR Switch", "OUT MIXR"},

	{"OUT MIXL", "BST1 Switch", "BST1"},
	{"OUT MIXL", "INL Switch", "INL VOL"},
	{"OUT MIXL", "REC MIXL Switch", "RECMIXL"},
	{"OUT MIXL", "DAC L1 Switch", "DAC L1"},

	{"OUT MIXR", "BST2 Switch", "BST2"},
	{"OUT MIXR", "BST1 Switch", "BST1"},
	{"OUT MIXR", "INR Switch", "INR VOL"},
	{"OUT MIXR", "REC MIXR Switch", "RECMIXR"},
	{"OUT MIXR", "DAC R1 Switch", "DAC R1"},

	{"SPKVOL L", NULL, "SPK MIXL"},
	{"SPKVOL R", NULL, "SPK MIXR"},
	{"HPOVOL L", NULL, "OUT MIXL"},
	{"HPOVOL R", NULL, "OUT MIXR"},
	{"OUTVOL L", NULL, "OUT MIXL"},
	{"OUTVOL R", NULL, "OUT MIXR"},

	{"SPOL MIX", "DAC R1 Switch", "DAC R1"},
	{"SPOL MIX", "DAC L1 Switch", "DAC L1"},
	{"SPOL MIX", "SPKVOL R Switch", "SPKVOL R"},
	{"SPOL MIX", "SPKVOL L Switch", "SPKVOL L"},
	{"SPOL MIX", "BST1 Switch", "BST1"},
	{"SPOR MIX", "DAC R1 Switch", "DAC R1"},
	{"SPOR MIX", "SPKVOL R Switch", "SPKVOL R"},
	{"SPOR MIX", "BST1 Switch", "BST1"},

	{"HPO MIX L", "HPO MIX DAC1 Switch", "DAC L1"},
	{"HPO MIX L", "HPO MIX HPVOL Switch", "HPOVOL L"},
	{"HPO MIX L", NULL, "HP L Amp"},
	{"HPO MIX R", "HPO MIX DAC1 Switch", "DAC R1"},
	{"HPO MIX R", "HPO MIX HPVOL Switch", "HPOVOL R"},
	{"HPO MIX R", NULL, "HP R Amp"},

	{"LOUT MIX", "DAC L1 Switch", "DAC L1"},
	{"LOUT MIX", "DAC R1 Switch", "DAC R1"},
	{"LOUT MIX", "OUTVOL L Switch", "OUTVOL L"},
	{"LOUT MIX", "OUTVOL R Switch", "OUTVOL R"},

	{"HP Amp", NULL, "HPO MIX L"},
	{"HP Amp", NULL, "HPO MIX R"},

	{"Speaker L Playback", "Switch", "SPOL MIX"},
	{"Speaker R Playback", "Switch", "SPOR MIX"},
	{"SPOLP", NULL, "Speaker L Playback"},
	{"SPOLN", NULL, "Speaker L Playback"},
	{"SPORP", NULL, "Speaker R Playback"},
	{"SPORN", NULL, "Speaker R Playback"},

	{"SPOLP", NULL, "Improve SPK Amp Drv"},
	{"SPOLN", NULL, "Improve SPK Amp Drv"},
	{"SPORP", NULL, "Improve SPK Amp Drv"},
	{"SPORN", NULL, "Improve SPK Amp Drv"},

	{"HPOL", NULL, "Improve HP Amp Drv"},
	{"HPOR", NULL, "Improve HP Amp Drv"},

	{"HP L Playback", "Switch", "HP Amp"},
	{"HP R Playback", "Switch", "HP Amp"},
	{"HPOL", NULL, "HP L Playback"},
	{"HPOR", NULL, "HP R Playback"},
	{"LOUTL", NULL, "LOUT MIX"},
	{"LOUTR", NULL, "LOUT MIX"},
};

static const struct snd_soc_dapm_route rt5640_specific_dapm_routes[] = {
	{"ANC", NULL, "Stereo ADC MIXL"},
	{"ANC", NULL, "Stereo ADC MIXR"},

	{"Audio DSP", NULL, "DAC MIXL"},
	{"Audio DSP", NULL, "DAC MIXR"},

	{"DAC L2 Mux", "IF2", "IF2 DAC L"},
	{"DAC L2 Mux", "Base L/R", "Audio DSP"},

	{"DAC R2 Mux", "IF2", "IF2 DAC R"},

	{"Stereo DAC MIXL", "DAC L2 Switch", "DAC L2 Mux"},
	{"Stereo DAC MIXL", "ANC Switch", "ANC"},
	{"Stereo DAC MIXR", "DAC R2 Switch", "DAC R2 Mux"},
	{"Stereo DAC MIXR", "ANC Switch", "ANC"},

	{"Mono DAC MIXL", "DAC L2 Switch", "DAC L2 Mux"},
	{"Mono DAC MIXL", "DAC R2 Switch", "DAC R2 Mux"},

	{"Mono DAC MIXR", "DAC R2 Switch", "DAC R2 Mux"},
	{"Mono DAC MIXR", "DAC L2 Switch", "DAC L2 Mux"},

	{"DIG MIXR", "DAC R2 Switch", "DAC R2 Mux"},
	{"DIG MIXL", "DAC L2 Switch", "DAC L2 Mux"},

	{"DAC L2", NULL, "Mono DAC MIXL"},
	{"DAC L2", NULL, "PLL1", is_sys_clk_from_pll},
	{"DAC R2", NULL, "Mono DAC MIXR"},
	{"DAC R2", NULL, "PLL1", is_sys_clk_from_pll},

	{"SPK MIXL", "DAC L2 Switch", "DAC L2"},
	{"SPK MIXR", "DAC R2 Switch", "DAC R2"},

	{"OUT MIXL", "SPK MIXL Switch", "SPK MIXL"},
	{"OUT MIXR", "SPK MIXR Switch", "SPK MIXR"},

	{"OUT MIXL", "DAC R2 Switch", "DAC R2"},
	{"OUT MIXL", "DAC L2 Switch", "DAC L2"},

	{"OUT MIXR", "DAC L2 Switch", "DAC L2"},
	{"OUT MIXR", "DAC R2 Switch", "DAC R2"},

	{"HPO MIX L", "HPO MIX DAC2 Switch", "DAC L2"},
	{"HPO MIX R", "HPO MIX DAC2 Switch", "DAC R2"},

	{"Mono MIX", "DAC R2 Switch", "DAC R2"},
	{"Mono MIX", "DAC L2 Switch", "DAC L2"},
	{"Mono MIX", "OUTVOL R Switch", "OUTVOL R"},
	{"Mono MIX", "OUTVOL L Switch", "OUTVOL L"},
	{"Mono MIX", "BST1 Switch", "BST1"},

	{"MONOP", NULL, "Mono MIX"},
	{"MONON", NULL, "Mono MIX"},
	{"MONOP", NULL, "Improve MONO Amp Drv"},
};

static const struct snd_soc_dapm_route rt5639_specific_dapm_routes[] = {
	{"Stereo DAC MIXL", "DAC L2 Switch", "IF2 DAC L"},
	{"Stereo DAC MIXR", "DAC R2 Switch", "IF2 DAC R"},

	{"Mono DAC MIXL", "DAC L2 Switch", "IF2 DAC L"},
	{"Mono DAC MIXL", "DAC R2 Switch", "IF2 DAC R"},

	{"Mono DAC MIXR", "DAC R2 Switch", "IF2 DAC R"},
	{"Mono DAC MIXR", "DAC L2 Switch", "IF2 DAC L"},

	{"DIG MIXL", "DAC L2 Switch", "IF2 DAC L"},
	{"DIG MIXR", "DAC R2 Switch", "IF2 DAC R"},

	{"IF2 DAC L", NULL, "DAC L2 Filter"},
	{"IF2 DAC R", NULL, "DAC R2 Filter"},
};

static int get_sdp_info(struct snd_soc_codec *codec, int dai_id)
{
	int ret = 0, val;

	if (codec == NULL)
		return -EINVAL;

	val = snd_soc_read(codec, RT5640_I2S1_SDP);
	val = (val & RT5640_I2S_IF_MASK) >> RT5640_I2S_IF_SFT;
	switch (dai_id) {
	case RT5640_AIF1:
		switch (val) {
		case RT5640_IF_123:
		case RT5640_IF_132:
			ret |= RT5640_U_IF1;
			break;
		case RT5640_IF_113:
			ret |= RT5640_U_IF1;
		case RT5640_IF_312:
		case RT5640_IF_213:
			ret |= RT5640_U_IF2;
			break;
		}
		break;

	case RT5640_AIF2:
		switch (val) {
		case RT5640_IF_231:
		case RT5640_IF_213:
			ret |= RT5640_U_IF1;
			break;
		case RT5640_IF_223:
			ret |= RT5640_U_IF1;
		case RT5640_IF_123:
		case RT5640_IF_321:
			ret |= RT5640_U_IF2;
			break;
		}
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rt5640_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, val_clk, mask_clk;
	int dai_sel, pre_div, bclk_ms, frame_size;

	rt5640->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5640->sysclk, rt5640->lrck[dai->id]);
	if (pre_div < 0) {
		dev_err(codec->dev, "Unsupported clock setting %d for DAI %d\n",
			rt5640->lrck[dai->id], dai->id);
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(codec->dev, "Unsupported frame size: %d\n", frame_size);
		return frame_size;
	}
	if (frame_size > 32)
		bclk_ms = 1;
	else
		bclk_ms = 0;
	rt5640->bclk[dai->id] = rt5640->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5640->bclk[dai->id], rt5640->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		val_len |= RT5640_I2S_DL_20;
		break;
	case 24:
		val_len |= RT5640_I2S_DL_24;
		break;
	case 8:
		val_len |= RT5640_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	dai_sel = get_sdp_info(codec, dai->id);
	if (dai_sel < 0) {
		dev_err(codec->dev, "Failed to get sdp info: %d\n", dai_sel);
		return -EINVAL;
	}
	if (dai_sel & RT5640_U_IF1) {
		mask_clk = RT5640_I2S_BCLK_MS1_MASK | RT5640_I2S_PD1_MASK;
		val_clk = bclk_ms << RT5640_I2S_BCLK_MS1_SFT |
			pre_div << RT5640_I2S_PD1_SFT;
		snd_soc_update_bits(codec, RT5640_I2S1_SDP,
			RT5640_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT5640_ADDA_CLK1, mask_clk, val_clk);
	}
	if (dai_sel & RT5640_U_IF2) {
		mask_clk = RT5640_I2S_BCLK_MS2_MASK | RT5640_I2S_PD2_MASK;
		val_clk = bclk_ms << RT5640_I2S_BCLK_MS2_SFT |
			pre_div << RT5640_I2S_PD2_SFT;
		snd_soc_update_bits(codec, RT5640_I2S2_SDP,
			RT5640_I2S_DL_MASK, val_len);
		snd_soc_update_bits(codec, RT5640_ADDA_CLK1, mask_clk, val_clk);
	}

	return 0;
}

static int rt5640_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;
	int dai_sel;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5640->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5640_I2S_MS_S;
		rt5640->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5640_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5640_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5640_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val  |= RT5640_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	dai_sel = get_sdp_info(codec, dai->id);
	if (dai_sel < 0) {
		dev_err(codec->dev, "Failed to get sdp info: %d\n", dai_sel);
		return -EINVAL;
	}
	if (dai_sel & RT5640_U_IF1) {
		snd_soc_update_bits(codec, RT5640_I2S1_SDP,
			RT5640_I2S_MS_MASK | RT5640_I2S_BP_MASK |
			RT5640_I2S_DF_MASK, reg_val);
	}
	if (dai_sel & RT5640_U_IF2) {
		snd_soc_update_bits(codec, RT5640_I2S2_SDP,
			RT5640_I2S_MS_MASK | RT5640_I2S_BP_MASK |
			RT5640_I2S_DF_MASK, reg_val);
	}

	return 0;
}

static int rt5640_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	if (freq == rt5640->sysclk && clk_id == rt5640->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5640_SCLK_S_MCLK:
		reg_val |= RT5640_SCLK_SRC_MCLK;
		break;
	case RT5640_SCLK_S_PLL1:
		reg_val |= RT5640_SCLK_SRC_PLL1;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT5640_GLB_CLK,
		RT5640_SCLK_SRC_MASK, reg_val);
	rt5640->sysclk = freq;
	rt5640->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);
	return 0;
}

static int rt5640_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);
	struct rl6231_pll_code pll_code;
	int ret, dai_sel;

	if (source == rt5640->pll_src && freq_in == rt5640->pll_in &&
	    freq_out == rt5640->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt5640->pll_in = 0;
		rt5640->pll_out = 0;
		snd_soc_update_bits(codec, RT5640_GLB_CLK,
			RT5640_SCLK_SRC_MASK, RT5640_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5640_PLL1_S_MCLK:
		snd_soc_update_bits(codec, RT5640_GLB_CLK,
			RT5640_PLL1_SRC_MASK, RT5640_PLL1_SRC_MCLK);
		break;
	case RT5640_PLL1_S_BCLK1:
	case RT5640_PLL1_S_BCLK2:
		dai_sel = get_sdp_info(codec, dai->id);
		if (dai_sel < 0) {
			dev_err(codec->dev,
				"Failed to get sdp info: %d\n", dai_sel);
			return -EINVAL;
		}
		if (dai_sel & RT5640_U_IF1) {
			snd_soc_update_bits(codec, RT5640_GLB_CLK,
				RT5640_PLL1_SRC_MASK, RT5640_PLL1_SRC_BCLK1);
		}
		if (dai_sel & RT5640_U_IF2) {
			snd_soc_update_bits(codec, RT5640_GLB_CLK,
				RT5640_PLL1_SRC_MASK, RT5640_PLL1_SRC_BCLK2);
		}
		break;
	default:
		dev_err(codec->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(codec->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(codec->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_write(codec, RT5640_PLL_CTRL1,
		pll_code.n_code << RT5640_PLL_N_SFT | pll_code.k_code);
	snd_soc_write(codec, RT5640_PLL_CTRL2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5640_PLL_M_SFT |
		pll_code.m_bp << RT5640_PLL_M_BP_SFT);

	rt5640->pll_in = freq_in;
	rt5640->pll_out = freq_out;
	rt5640->pll_src = source;

	return 0;
}

static int rt5640_set_bias_level(struct snd_soc_codec *codec,
			enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		if (SND_SOC_BIAS_OFF == codec->dapm.bias_level) {
			snd_soc_update_bits(codec, RT5640_PWR_ANLG1,
				RT5640_PWR_VREF1 | RT5640_PWR_MB |
				RT5640_PWR_BG | RT5640_PWR_VREF2,
				RT5640_PWR_VREF1 | RT5640_PWR_MB |
				RT5640_PWR_BG | RT5640_PWR_VREF2);
			usleep_range(10000, 15000);
			snd_soc_update_bits(codec, RT5640_PWR_ANLG1,
				RT5640_PWR_FV1 | RT5640_PWR_FV2,
				RT5640_PWR_FV1 | RT5640_PWR_FV2);
			snd_soc_update_bits(codec, RT5640_DUMMY1,
						0x0301, 0x0301);
			snd_soc_update_bits(codec, RT5640_MICBIAS,
						0x0030, 0x0030);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_write(codec, RT5640_DEPOP_M1, 0x0004);
		snd_soc_write(codec, RT5640_DEPOP_M2, 0x1100);
		snd_soc_update_bits(codec, RT5640_DUMMY1, 0x1, 0);
		snd_soc_write(codec, RT5640_PWR_DIG1, 0x0000);
		snd_soc_write(codec, RT5640_PWR_DIG2, 0x0000);
		snd_soc_write(codec, RT5640_PWR_VOL, 0x0000);
		snd_soc_write(codec, RT5640_PWR_MIXER, 0x0000);
		snd_soc_write(codec, RT5640_PWR_ANLG1, 0x0000);
		snd_soc_write(codec, RT5640_PWR_ANLG2, 0x0000);
		break;

	default:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

static int rt5640_probe(struct snd_soc_codec *codec)
{
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);

	rt5640->codec = codec;

	rt5640_set_bias_level(codec, SND_SOC_BIAS_OFF);

	snd_soc_update_bits(codec, RT5640_DUMMY1, 0x0301, 0x0301);
	snd_soc_update_bits(codec, RT5640_MICBIAS, 0x0030, 0x0030);
	snd_soc_update_bits(codec, RT5640_DSP_PATH2, 0xfc00, 0x0c00);

	switch (snd_soc_read(codec, RT5640_RESET) & RT5640_ID_MASK) {
	case RT5640_ID_5640:
	case RT5640_ID_5642:
		snd_soc_add_codec_controls(codec,
			rt5640_specific_snd_controls,
			ARRAY_SIZE(rt5640_specific_snd_controls));
		snd_soc_dapm_new_controls(&codec->dapm,
			rt5640_specific_dapm_widgets,
			ARRAY_SIZE(rt5640_specific_dapm_widgets));
		snd_soc_dapm_add_routes(&codec->dapm,
			rt5640_specific_dapm_routes,
			ARRAY_SIZE(rt5640_specific_dapm_routes));
		break;
	case RT5640_ID_5639:
		snd_soc_dapm_new_controls(&codec->dapm,
			rt5639_specific_dapm_widgets,
			ARRAY_SIZE(rt5639_specific_dapm_widgets));
		snd_soc_dapm_add_routes(&codec->dapm,
			rt5639_specific_dapm_routes,
			ARRAY_SIZE(rt5639_specific_dapm_routes));
		break;
	default:
		dev_err(codec->dev,
			"The driver is for RT5639 RT5640 or RT5642 only\n");
		return -ENODEV;
	}

	return 0;
}

static int rt5640_remove(struct snd_soc_codec *codec)
{
	rt5640_reset(codec);

	return 0;
}

#ifdef CONFIG_PM
static int rt5640_suspend(struct snd_soc_codec *codec)
{
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);

	rt5640_set_bias_level(codec, SND_SOC_BIAS_OFF);
	rt5640_reset(codec);
	regcache_cache_only(rt5640->regmap, true);
	regcache_mark_dirty(rt5640->regmap);
	if (gpio_is_valid(rt5640->pdata.ldo1_en))
		gpio_set_value_cansleep(rt5640->pdata.ldo1_en, 0);

	return 0;
}

static int rt5640_resume(struct snd_soc_codec *codec)
{
	struct rt5640_priv *rt5640 = snd_soc_codec_get_drvdata(codec);

	if (gpio_is_valid(rt5640->pdata.ldo1_en)) {
		gpio_set_value_cansleep(rt5640->pdata.ldo1_en, 1);
		msleep(400);
	}

	regcache_cache_only(rt5640->regmap, false);
	regcache_sync(rt5640->regmap);

	return 0;
}
#else
#define rt5640_suspend NULL
#define rt5640_resume NULL
#endif

#define RT5640_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define RT5640_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt5640_aif_dai_ops = {
	.hw_params = rt5640_hw_params,
	.set_fmt = rt5640_set_dai_fmt,
	.set_sysclk = rt5640_set_dai_sysclk,
	.set_pll = rt5640_set_dai_pll,
};

static struct snd_soc_dai_driver rt5640_dai[] = {
	{
		.name = "rt5640-aif1",
		.id = RT5640_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5640_STEREO_RATES,
			.formats = RT5640_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5640_STEREO_RATES,
			.formats = RT5640_FORMATS,
		},
		.ops = &rt5640_aif_dai_ops,
	},
	{
		.name = "rt5640-aif2",
		.id = RT5640_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5640_STEREO_RATES,
			.formats = RT5640_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5640_STEREO_RATES,
			.formats = RT5640_FORMATS,
		},
		.ops = &rt5640_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5640 = {
	.probe = rt5640_probe,
	.remove = rt5640_remove,
	.suspend = rt5640_suspend,
	.resume = rt5640_resume,
	.set_bias_level = rt5640_set_bias_level,
	.idle_bias_off = true,
	.controls = rt5640_snd_controls,
	.num_controls = ARRAY_SIZE(rt5640_snd_controls),
	.dapm_widgets = rt5640_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5640_dapm_widgets),
	.dapm_routes = rt5640_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5640_dapm_routes),
};

static const struct regmap_config rt5640_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.use_single_rw = true,

	.max_register = RT5640_VENDOR_ID2 + 1 + (ARRAY_SIZE(rt5640_ranges) *
					       RT5640_PR_SPACING),
	.volatile_reg = rt5640_volatile_register,
	.readable_reg = rt5640_readable_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5640_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5640_reg),
	.ranges = rt5640_ranges,
	.num_ranges = ARRAY_SIZE(rt5640_ranges),
};

static const struct i2c_device_id rt5640_i2c_id[] = {
	{ "rt5640", 0 },
	{ "rt5639", 0 },
	{ "rt5642", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5640_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id rt5640_of_match[] = {
	{ .compatible = "realtek,rt5639", },
	{ .compatible = "realtek,rt5640", },
	{},
};
MODULE_DEVICE_TABLE(of, rt5640_of_match);
#endif

#ifdef CONFIG_ACPI
static struct acpi_device_id rt5640_acpi_match[] = {
	{ "INT33CA", 0 },
	{ "10EC5640", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, rt5640_acpi_match);
#endif

static int rt5640_parse_dt(struct rt5640_priv *rt5640, struct device_node *np)
{
	rt5640->pdata.in1_diff = of_property_read_bool(np,
					"realtek,in1-differential");
	rt5640->pdata.in2_diff = of_property_read_bool(np,
					"realtek,in2-differential");

	rt5640->pdata.ldo1_en = of_get_named_gpio(np,
					"realtek,ldo1-en-gpios", 0);
	/*
	 * LDO1_EN is optional (it may be statically tied on the board).
	 * -ENOENT means that the property doesn't exist, i.e. there is no
	 * GPIO, so is not an error. Any other error code means the property
	 * exists, but could not be parsed.
	 */
	if (!gpio_is_valid(rt5640->pdata.ldo1_en) &&
			(rt5640->pdata.ldo1_en != -ENOENT))
		return rt5640->pdata.ldo1_en;

	return 0;
}

static int rt5640_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5640_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5640_priv *rt5640;
	int ret;
	unsigned int val;

	rt5640 = devm_kzalloc(&i2c->dev,
				sizeof(struct rt5640_priv),
				GFP_KERNEL);
	if (NULL == rt5640)
		return -ENOMEM;
	i2c_set_clientdata(i2c, rt5640);

	if (pdata) {
		rt5640->pdata = *pdata;
		/*
		 * Translate zero'd out (default) pdata value to an invalid
		 * GPIO ID. This makes the pdata and DT paths consistent in
		 * terms of the value left in this field when no GPIO is
		 * specified, but means we can't actually use GPIO 0.
		 */
		if (!rt5640->pdata.ldo1_en)
			rt5640->pdata.ldo1_en = -EINVAL;
	} else if (i2c->dev.of_node) {
		ret = rt5640_parse_dt(rt5640, i2c->dev.of_node);
		if (ret)
			return ret;
	} else
		rt5640->pdata.ldo1_en = -EINVAL;

	rt5640->regmap = devm_regmap_init_i2c(i2c, &rt5640_regmap);
	if (IS_ERR(rt5640->regmap)) {
		ret = PTR_ERR(rt5640->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	if (gpio_is_valid(rt5640->pdata.ldo1_en)) {
		ret = devm_gpio_request_one(&i2c->dev, rt5640->pdata.ldo1_en,
					    GPIOF_OUT_INIT_HIGH,
					    "RT5640 LDO1_EN");
		if (ret < 0) {
			dev_err(&i2c->dev, "Failed to request LDO1_EN %d: %d\n",
				rt5640->pdata.ldo1_en, ret);
			return ret;
		}
		msleep(400);
	}

	regmap_read(rt5640->regmap, RT5640_VENDOR_ID2, &val);
	if (val != RT5640_DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %x is not rt5640/39\n", val);
		return -ENODEV;
	}

	regmap_write(rt5640->regmap, RT5640_RESET, 0);

	ret = regmap_register_patch(rt5640->regmap, init_list,
				    ARRAY_SIZE(init_list));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	if (rt5640->pdata.in1_diff)
		regmap_update_bits(rt5640->regmap, RT5640_IN1_IN2,
					RT5640_IN_DF1, RT5640_IN_DF1);

	if (rt5640->pdata.in2_diff)
		regmap_update_bits(rt5640->regmap, RT5640_IN3_IN4,
					RT5640_IN_DF2, RT5640_IN_DF2);

	if (rt5640->pdata.dmic_en) {
		regmap_update_bits(rt5640->regmap, RT5640_GPIO_CTRL1,
			RT5640_GP2_PIN_MASK, RT5640_GP2_PIN_DMIC1_SCL);

		if (rt5640->pdata.dmic1_data_pin) {
			regmap_update_bits(rt5640->regmap, RT5640_DMIC,
				RT5640_DMIC_1_DP_MASK, RT5640_DMIC_1_DP_GPIO3);
			regmap_update_bits(rt5640->regmap, RT5640_GPIO_CTRL1,
				RT5640_GP3_PIN_MASK, RT5640_GP3_PIN_DMIC1_SDA);
		}

		if (rt5640->pdata.dmic2_data_pin) {
			regmap_update_bits(rt5640->regmap, RT5640_DMIC,
				RT5640_DMIC_2_DP_MASK, RT5640_DMIC_2_DP_GPIO4);
			regmap_update_bits(rt5640->regmap, RT5640_GPIO_CTRL1,
				RT5640_GP4_PIN_MASK, RT5640_GP4_PIN_DMIC2_SDA);
		}
	}

	rt5640->hp_mute = 1;

	return snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5640,
				      rt5640_dai, ARRAY_SIZE(rt5640_dai));
}

static int rt5640_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}

static struct i2c_driver rt5640_i2c_driver = {
	.driver = {
		.name = "rt5640",
		.owner = THIS_MODULE,
		.acpi_match_table = ACPI_PTR(rt5640_acpi_match),
		.of_match_table = of_match_ptr(rt5640_of_match),
	},
	.probe = rt5640_i2c_probe,
	.remove   = rt5640_i2c_remove,
	.id_table = rt5640_i2c_id,
};
module_i2c_driver(rt5640_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5640/RT5639 driver");
MODULE_AUTHOR("Johnny Hsu <johnnyhsu@realtek.com>");
MODULE_LICENSE("GPL v2");
