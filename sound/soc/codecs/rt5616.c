/*
 * rt5616.c  --  RT5616 ALSA SoC audio codec driver
 *
 * Copyright 2015 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#include "rl6231.h"
#include "rt5616.h"

#define RT5616_PR_RANGE_BASE (0xff + 1)
#define RT5616_PR_SPACING 0x100

#define RT5616_PR_BASE (RT5616_PR_RANGE_BASE + (0 * RT5616_PR_SPACING))

static const struct regmap_range_cfg rt5616_ranges[] = {
	{
		.name = "PR",
		.range_min = RT5616_PR_BASE,
		.range_max = RT5616_PR_BASE + 0xf8,
		.selector_reg = RT5616_PRIV_INDEX,
		.selector_mask = 0xff,
		.selector_shift = 0x0,
		.window_start = RT5616_PRIV_DATA,
		.window_len = 0x1,
	},
};

static const struct reg_sequence init_list[] = {
	{RT5616_PR_BASE + 0x3d,	0x3e00},
	{RT5616_PR_BASE + 0x25,	0x6110},
	{RT5616_PR_BASE + 0x20,	0x611f},
	{RT5616_PR_BASE + 0x21,	0x4040},
	{RT5616_PR_BASE + 0x23,	0x0004},
};

#define RT5616_INIT_REG_LEN ARRAY_SIZE(init_list)

static const struct reg_default rt5616_reg[] = {
	{ 0x00, 0x0021 },
	{ 0x02, 0xc8c8 },
	{ 0x03, 0xc8c8 },
	{ 0x05, 0x0000 },
	{ 0x0d, 0x0000 },
	{ 0x0f, 0x0808 },
	{ 0x19, 0xafaf },
	{ 0x1c, 0x2f2f },
	{ 0x1e, 0x0000 },
	{ 0x27, 0x7860 },
	{ 0x29, 0x8080 },
	{ 0x2a, 0x5252 },
	{ 0x3b, 0x0000 },
	{ 0x3c, 0x006f },
	{ 0x3d, 0x0000 },
	{ 0x3e, 0x006f },
	{ 0x45, 0x6000 },
	{ 0x4d, 0x0000 },
	{ 0x4e, 0x0000 },
	{ 0x4f, 0x0279 },
	{ 0x50, 0x0000 },
	{ 0x51, 0x0000 },
	{ 0x52, 0x0279 },
	{ 0x53, 0xf000 },
	{ 0x61, 0x0000 },
	{ 0x62, 0x0000 },
	{ 0x63, 0x00c0 },
	{ 0x64, 0x0000 },
	{ 0x65, 0x0000 },
	{ 0x66, 0x0000 },
	{ 0x70, 0x8000 },
	{ 0x73, 0x1104 },
	{ 0x74, 0x0c00 },
	{ 0x80, 0x0000 },
	{ 0x81, 0x0000 },
	{ 0x82, 0x0000 },
	{ 0x8b, 0x0600 },
	{ 0x8e, 0x0004 },
	{ 0x8f, 0x1100 },
	{ 0x90, 0x0000 },
	{ 0x91, 0x0000 },
	{ 0x92, 0x0000 },
	{ 0x93, 0x2000 },
	{ 0x94, 0x0200 },
	{ 0x95, 0x0000 },
	{ 0xb0, 0x2080 },
	{ 0xb1, 0x0000 },
	{ 0xb2, 0x0000 },
	{ 0xb4, 0x2206 },
	{ 0xb5, 0x1f00 },
	{ 0xb6, 0x0000 },
	{ 0xb7, 0x0000 },
	{ 0xbb, 0x0000 },
	{ 0xbc, 0x0000 },
	{ 0xbd, 0x0000 },
	{ 0xbe, 0x0000 },
	{ 0xbf, 0x0000 },
	{ 0xc0, 0x0100 },
	{ 0xc1, 0x0000 },
	{ 0xc2, 0x0000 },
	{ 0xc8, 0x0000 },
	{ 0xc9, 0x0000 },
	{ 0xca, 0x0000 },
	{ 0xcb, 0x0000 },
	{ 0xcc, 0x0000 },
	{ 0xcd, 0x0000 },
	{ 0xce, 0x0000 },
	{ 0xcf, 0x0013 },
	{ 0xd0, 0x0680 },
	{ 0xd1, 0x1c17 },
	{ 0xd3, 0xb320 },
	{ 0xd4, 0x0000 },
	{ 0xd6, 0x0000 },
	{ 0xd7, 0x0000 },
	{ 0xd9, 0x0809 },
	{ 0xda, 0x0000 },
	{ 0xfa, 0x0010 },
	{ 0xfb, 0x0000 },
	{ 0xfc, 0x0000 },
	{ 0xfe, 0x10ec },
	{ 0xff, 0x6281 },
};

struct rt5616_priv {
	struct snd_soc_codec *codec;
	struct delayed_work patch_work;
	struct regmap *regmap;
	struct clk *mclk;

	int sysclk;
	int sysclk_src;
	int lrck[RT5616_AIFS];
	int bclk[RT5616_AIFS];
	int master[RT5616_AIFS];

	int pll_src;
	int pll_in;
	int pll_out;

};

static bool rt5616_volatile_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5616_ranges); i++) {
		if (reg >= rt5616_ranges[i].range_min &&
		    reg <= rt5616_ranges[i].range_max)
			return true;
	}

	switch (reg) {
	case RT5616_RESET:
	case RT5616_PRIV_DATA:
	case RT5616_EQ_CTRL1:
	case RT5616_DRC_AGC_1:
	case RT5616_IRQ_CTRL2:
	case RT5616_INT_IRQ_ST:
	case RT5616_PGM_REG_ARR1:
	case RT5616_PGM_REG_ARR3:
	case RT5616_VENDOR_ID:
	case RT5616_DEVICE_ID:
		return true;
	default:
		return false;
	}
}

static bool rt5616_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5616_ranges); i++) {
		if (reg >= rt5616_ranges[i].range_min &&
		    reg <= rt5616_ranges[i].range_max)
			return true;
	}

	switch (reg) {
	case RT5616_RESET:
	case RT5616_VERSION_ID:
	case RT5616_VENDOR_ID:
	case RT5616_DEVICE_ID:
	case RT5616_HP_VOL:
	case RT5616_LOUT_CTRL1:
	case RT5616_LOUT_CTRL2:
	case RT5616_IN1_IN2:
	case RT5616_INL1_INR1_VOL:
	case RT5616_DAC1_DIG_VOL:
	case RT5616_ADC_DIG_VOL:
	case RT5616_ADC_BST_VOL:
	case RT5616_STO1_ADC_MIXER:
	case RT5616_AD_DA_MIXER:
	case RT5616_STO_DAC_MIXER:
	case RT5616_REC_L1_MIXER:
	case RT5616_REC_L2_MIXER:
	case RT5616_REC_R1_MIXER:
	case RT5616_REC_R2_MIXER:
	case RT5616_HPO_MIXER:
	case RT5616_OUT_L1_MIXER:
	case RT5616_OUT_L2_MIXER:
	case RT5616_OUT_L3_MIXER:
	case RT5616_OUT_R1_MIXER:
	case RT5616_OUT_R2_MIXER:
	case RT5616_OUT_R3_MIXER:
	case RT5616_LOUT_MIXER:
	case RT5616_PWR_DIG1:
	case RT5616_PWR_DIG2:
	case RT5616_PWR_ANLG1:
	case RT5616_PWR_ANLG2:
	case RT5616_PWR_MIXER:
	case RT5616_PWR_VOL:
	case RT5616_PRIV_INDEX:
	case RT5616_PRIV_DATA:
	case RT5616_I2S1_SDP:
	case RT5616_ADDA_CLK1:
	case RT5616_ADDA_CLK2:
	case RT5616_GLB_CLK:
	case RT5616_PLL_CTRL1:
	case RT5616_PLL_CTRL2:
	case RT5616_HP_OVCD:
	case RT5616_DEPOP_M1:
	case RT5616_DEPOP_M2:
	case RT5616_DEPOP_M3:
	case RT5616_CHARGE_PUMP:
	case RT5616_PV_DET_SPK_G:
	case RT5616_MICBIAS:
	case RT5616_A_JD_CTL1:
	case RT5616_A_JD_CTL2:
	case RT5616_EQ_CTRL1:
	case RT5616_EQ_CTRL2:
	case RT5616_WIND_FILTER:
	case RT5616_DRC_AGC_1:
	case RT5616_DRC_AGC_2:
	case RT5616_DRC_AGC_3:
	case RT5616_SVOL_ZC:
	case RT5616_JD_CTRL1:
	case RT5616_JD_CTRL2:
	case RT5616_IRQ_CTRL1:
	case RT5616_IRQ_CTRL2:
	case RT5616_INT_IRQ_ST:
	case RT5616_GPIO_CTRL1:
	case RT5616_GPIO_CTRL2:
	case RT5616_GPIO_CTRL3:
	case RT5616_PGM_REG_ARR1:
	case RT5616_PGM_REG_ARR2:
	case RT5616_PGM_REG_ARR3:
	case RT5616_PGM_REG_ARR4:
	case RT5616_PGM_REG_ARR5:
	case RT5616_SCB_FUNC:
	case RT5616_SCB_CTRL:
	case RT5616_BASE_BACK:
	case RT5616_MP3_PLUS1:
	case RT5616_MP3_PLUS2:
	case RT5616_ADJ_HPF_CTRL1:
	case RT5616_ADJ_HPF_CTRL2:
	case RT5616_HP_CALIB_AMP_DET:
	case RT5616_HP_CALIB2:
	case RT5616_SV_ZCD1:
	case RT5616_SV_ZCD2:
	case RT5616_D_MISC:
	case RT5616_DUMMY2:
	case RT5616_DUMMY3:
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
static const SNDRV_CTL_TLVD_DECLARE_DB_RANGE(bst_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0),
);

static const struct snd_kcontrol_new rt5616_snd_controls[] = {
	/* Headphone Output Volume */
	SOC_DOUBLE("HP Playback Switch", RT5616_HP_VOL,
		   RT5616_L_MUTE_SFT, RT5616_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("HPVOL Playback Switch", RT5616_HP_VOL,
		   RT5616_VOL_L_SFT, RT5616_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("HP Playback Volume", RT5616_HP_VOL,
		       RT5616_L_VOL_SFT, RT5616_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* OUTPUT Control */
	SOC_DOUBLE("OUT Playback Switch", RT5616_LOUT_CTRL1,
		   RT5616_L_MUTE_SFT, RT5616_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE("OUT Channel Switch", RT5616_LOUT_CTRL1,
		   RT5616_VOL_L_SFT, RT5616_VOL_R_SFT, 1, 1),
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5616_LOUT_CTRL1,
		       RT5616_L_VOL_SFT, RT5616_R_VOL_SFT, 39, 1, out_vol_tlv),

	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5616_DAC1_DIG_VOL,
		       RT5616_L_VOL_SFT, RT5616_R_VOL_SFT,
		       175, 0, dac_vol_tlv),
	/* IN1/IN2 Control */
	SOC_SINGLE_TLV("IN1 Boost Volume", RT5616_IN1_IN2,
		       RT5616_BST_SFT1, 8, 0, bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost Volume", RT5616_IN1_IN2,
		       RT5616_BST_SFT2, 8, 0, bst_tlv),
	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT5616_INL1_INR1_VOL,
		       RT5616_INL_VOL_SFT, RT5616_INR_VOL_SFT,
		       31, 1, in_vol_tlv),
	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT5616_ADC_DIG_VOL,
		   RT5616_L_MUTE_SFT, RT5616_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT5616_ADC_DIG_VOL,
		       RT5616_L_VOL_SFT, RT5616_R_VOL_SFT,
		       127, 0, adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("ADC Boost Volume", RT5616_ADC_BST_VOL,
		       RT5616_ADC_L_BST_SFT, RT5616_ADC_R_BST_SFT,
		       3, 0, adc_bst_tlv),
};

static int is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			       struct snd_soc_dapm_widget *sink)
{
	unsigned int val;

	val = snd_soc_read(snd_soc_dapm_to_codec(source->dapm), RT5616_GLB_CLK);
	val &= RT5616_SCLK_SRC_MASK;
	if (val == RT5616_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5616_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5616_STO1_ADC_MIXER,
			RT5616_M_STO1_ADC_L1_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5616_STO1_ADC_MIXER,
			RT5616_M_STO1_ADC_R1_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5616_AD_DA_MIXER,
			RT5616_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT5616_AD_DA_MIXER,
			RT5616_M_IF1_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5616_AD_DA_MIXER,
			RT5616_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT5616_AD_DA_MIXER,
			RT5616_M_IF1_DAC_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5616_STO_DAC_MIXER,
			RT5616_M_DAC_L1_MIXL_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5616_STO_DAC_MIXER,
			RT5616_M_DAC_R1_MIXL_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5616_STO_DAC_MIXER,
			RT5616_M_DAC_R1_MIXR_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5616_STO_DAC_MIXER,
			RT5616_M_DAC_L1_MIXR_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5616_rec_l_mix[] = {
	SOC_DAPM_SINGLE("INL1 Switch", RT5616_REC_L2_MIXER,
			RT5616_M_IN1_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5616_REC_L2_MIXER,
			RT5616_M_BST2_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5616_REC_L2_MIXER,
			RT5616_M_BST1_RM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_rec_r_mix[] = {
	SOC_DAPM_SINGLE("INR1 Switch", RT5616_REC_R2_MIXER,
			RT5616_M_IN1_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5616_REC_R2_MIXER,
			RT5616_M_BST2_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5616_REC_R2_MIXER,
			RT5616_M_BST1_RM_R_SFT, 1, 1),
};

/* Analog Output Mixer */

static const struct snd_kcontrol_new rt5616_out_l_mix[] = {
	SOC_DAPM_SINGLE("BST1 Switch", RT5616_OUT_L3_MIXER,
			RT5616_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5616_OUT_L3_MIXER,
			RT5616_M_BST2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL1 Switch", RT5616_OUT_L3_MIXER,
			RT5616_M_IN1_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXL Switch", RT5616_OUT_L3_MIXER,
			RT5616_M_RM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5616_OUT_L3_MIXER,
			RT5616_M_DAC_L1_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_out_r_mix[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5616_OUT_R3_MIXER,
			RT5616_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5616_OUT_R3_MIXER,
			RT5616_M_BST1_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR1 Switch", RT5616_OUT_R3_MIXER,
			RT5616_M_IN1_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXR Switch", RT5616_OUT_R3_MIXER,
			RT5616_M_RM_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5616_OUT_R3_MIXER,
			RT5616_M_DAC_R1_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_hpo_mix[] = {
	SOC_DAPM_SINGLE("DAC1 Switch", RT5616_HPO_MIXER,
			RT5616_M_DAC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPVOL Switch", RT5616_HPO_MIXER,
			RT5616_M_HPVOL_HM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5616_lout_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5616_LOUT_MIXER,
			RT5616_M_DAC_L1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5616_LOUT_MIXER,
			RT5616_M_DAC_R1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT5616_LOUT_MIXER,
			RT5616_M_OV_L_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT5616_LOUT_MIXER,
			RT5616_M_OV_R_LM_SFT, 1, 1),
};

static int rt5616_adc_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5616_ADC_DIG_VOL,
				    RT5616_L_MUTE | RT5616_R_MUTE, 0);
		break;

	case SND_SOC_DAPM_POST_PMD:
		snd_soc_update_bits(codec, RT5616_ADC_DIG_VOL,
				    RT5616_L_MUTE | RT5616_R_MUTE,
				    RT5616_L_MUTE | RT5616_R_MUTE);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5616_charge_pump_event(struct snd_soc_dapm_widget *w,
				    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* depop parameters */
		snd_soc_update_bits(codec, RT5616_DEPOP_M2,
				    RT5616_DEPOP_MASK, RT5616_DEPOP_MAN);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_HP_CP_MASK | RT5616_HP_SG_MASK |
				    RT5616_HP_CB_MASK, RT5616_HP_CP_PU |
				    RT5616_HP_SG_DIS | RT5616_HP_CB_PU);
		snd_soc_write(codec, RT5616_PR_BASE +
			      RT5616_HP_DCC_INT1, 0x9f00);
		/* headphone amp power on */
		snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
				    RT5616_PWR_FV1 | RT5616_PWR_FV2, 0);
		snd_soc_update_bits(codec, RT5616_PWR_VOL,
				    RT5616_PWR_HV_L | RT5616_PWR_HV_R,
				    RT5616_PWR_HV_L | RT5616_PWR_HV_R);
		snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
				    RT5616_PWR_HP_L | RT5616_PWR_HP_R |
				    RT5616_PWR_HA, RT5616_PWR_HP_L |
				    RT5616_PWR_HP_R | RT5616_PWR_HA);
		msleep(50);
		snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
				    RT5616_PWR_FV1 | RT5616_PWR_FV2,
				    RT5616_PWR_FV1 | RT5616_PWR_FV2);

		snd_soc_update_bits(codec, RT5616_CHARGE_PUMP,
				    RT5616_PM_HP_MASK, RT5616_PM_HP_HV);
		snd_soc_update_bits(codec, RT5616_PR_BASE +
				    RT5616_CHOP_DAC_ADC, 0x0200, 0x0200);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_HP_CO_MASK | RT5616_HP_SG_MASK,
				    RT5616_HP_CO_EN | RT5616_HP_SG_EN);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5616_PR_BASE +
				    RT5616_CHOP_DAC_ADC, 0x0200, 0x0);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_HP_SG_MASK | RT5616_HP_L_SMT_MASK |
				    RT5616_HP_R_SMT_MASK, RT5616_HP_SG_DIS |
				    RT5616_HP_L_SMT_DIS | RT5616_HP_R_SMT_DIS);
		/* headphone amp power down */
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_SMT_TRIG_MASK |
				    RT5616_HP_CD_PD_MASK | RT5616_HP_CO_MASK |
				    RT5616_HP_CP_MASK | RT5616_HP_SG_MASK |
				    RT5616_HP_CB_MASK,
				    RT5616_SMT_TRIG_DIS | RT5616_HP_CD_PD_EN |
				    RT5616_HP_CO_DIS | RT5616_HP_CP_PD |
				    RT5616_HP_SG_EN | RT5616_HP_CB_PD);
		snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
				    RT5616_PWR_HP_L | RT5616_PWR_HP_R |
				    RT5616_PWR_HA, 0);
		break;
	default:
		return 0;
	}

	return 0;
}

static int rt5616_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* headphone unmute sequence */
		snd_soc_update_bits(codec, RT5616_DEPOP_M3,
				    RT5616_CP_FQ1_MASK | RT5616_CP_FQ2_MASK |
				    RT5616_CP_FQ3_MASK,
				    RT5616_CP_FQ_192_KHZ << RT5616_CP_FQ1_SFT |
				    RT5616_CP_FQ_12_KHZ << RT5616_CP_FQ2_SFT |
				    RT5616_CP_FQ_192_KHZ << RT5616_CP_FQ3_SFT);
		snd_soc_write(codec, RT5616_PR_BASE +
			      RT5616_MAMP_INT_REG2, 0xfc00);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_SMT_TRIG_MASK, RT5616_SMT_TRIG_EN);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_RSTN_MASK, RT5616_RSTN_EN);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_RSTN_MASK | RT5616_HP_L_SMT_MASK |
				    RT5616_HP_R_SMT_MASK, RT5616_RSTN_DIS |
				    RT5616_HP_L_SMT_EN | RT5616_HP_R_SMT_EN);
		snd_soc_update_bits(codec, RT5616_HP_VOL,
				    RT5616_L_MUTE | RT5616_R_MUTE, 0);
		msleep(100);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_HP_SG_MASK | RT5616_HP_L_SMT_MASK |
				    RT5616_HP_R_SMT_MASK, RT5616_HP_SG_DIS |
				    RT5616_HP_L_SMT_DIS | RT5616_HP_R_SMT_DIS);
		msleep(20);
		snd_soc_update_bits(codec, RT5616_HP_CALIB_AMP_DET,
				    RT5616_HPD_PS_MASK, RT5616_HPD_PS_EN);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		/* headphone mute sequence */
		snd_soc_update_bits(codec, RT5616_DEPOP_M3,
				    RT5616_CP_FQ1_MASK | RT5616_CP_FQ2_MASK |
				    RT5616_CP_FQ3_MASK,
				    RT5616_CP_FQ_96_KHZ << RT5616_CP_FQ1_SFT |
				    RT5616_CP_FQ_12_KHZ << RT5616_CP_FQ2_SFT |
				    RT5616_CP_FQ_96_KHZ << RT5616_CP_FQ3_SFT);
		snd_soc_write(codec, RT5616_PR_BASE +
			      RT5616_MAMP_INT_REG2, 0xfc00);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_HP_SG_MASK, RT5616_HP_SG_EN);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_RSTP_MASK, RT5616_RSTP_EN);
		snd_soc_update_bits(codec, RT5616_DEPOP_M1,
				    RT5616_RSTP_MASK | RT5616_HP_L_SMT_MASK |
				    RT5616_HP_R_SMT_MASK, RT5616_RSTP_DIS |
				    RT5616_HP_L_SMT_EN | RT5616_HP_R_SMT_EN);
		snd_soc_update_bits(codec, RT5616_HP_CALIB_AMP_DET,
				    RT5616_HPD_PS_MASK, RT5616_HPD_PS_DIS);
		msleep(90);
		snd_soc_update_bits(codec, RT5616_HP_VOL,
				    RT5616_L_MUTE | RT5616_R_MUTE,
				    RT5616_L_MUTE | RT5616_R_MUTE);
		msleep(30);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5616_lout_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
				    RT5616_PWR_LM, RT5616_PWR_LM);
		snd_soc_update_bits(codec, RT5616_LOUT_CTRL1,
				    RT5616_L_MUTE | RT5616_R_MUTE, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5616_LOUT_CTRL1,
				    RT5616_L_MUTE | RT5616_R_MUTE,
				    RT5616_L_MUTE | RT5616_R_MUTE);
		snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
				    RT5616_PWR_LM, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5616_bst1_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5616_PWR_ANLG2,
				    RT5616_PWR_BST1_OP2, RT5616_PWR_BST1_OP2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5616_PWR_ANLG2,
				    RT5616_PWR_BST1_OP2, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5616_bst2_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec, RT5616_PWR_ANLG2,
				    RT5616_PWR_BST2_OP2, RT5616_PWR_BST2_OP2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec, RT5616_PWR_ANLG2,
				    RT5616_PWR_BST2_OP2, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5616_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("PLL1", RT5616_PWR_ANLG2,
			    RT5616_PWR_PLL_BIT, 0, NULL, 0),
	/* Input Side */
	/* micbias */
	SND_SOC_DAPM_SUPPLY("LDO", RT5616_PWR_ANLG1,
			    RT5616_PWR_LDO_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("micbias1", RT5616_PWR_ANLG2,
			    RT5616_PWR_MB1_BIT, 0, NULL, 0),

	/* Input Lines */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),

	/* Boost */
	SND_SOC_DAPM_PGA_E("BST1", RT5616_PWR_ANLG2,
			   RT5616_PWR_BST1_BIT, 0, NULL, 0, rt5616_bst1_event,
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("BST2", RT5616_PWR_ANLG2,
			   RT5616_PWR_BST2_BIT, 0, NULL, 0, rt5616_bst2_event,
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	/* Input Volume */
	SND_SOC_DAPM_PGA("INL1 VOL", RT5616_PWR_VOL,
			 RT5616_PWR_IN1_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR1 VOL", RT5616_PWR_VOL,
			 RT5616_PWR_IN1_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INL2 VOL", RT5616_PWR_VOL,
			 RT5616_PWR_IN2_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR2 VOL", RT5616_PWR_VOL,
			 RT5616_PWR_IN2_R_BIT, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL", RT5616_PWR_MIXER, RT5616_PWR_RM_L_BIT, 0,
			   rt5616_rec_l_mix, ARRAY_SIZE(rt5616_rec_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIXR", RT5616_PWR_MIXER, RT5616_PWR_RM_R_BIT, 0,
			   rt5616_rec_r_mix, ARRAY_SIZE(rt5616_rec_r_mix)),
	/* ADCs */
	SND_SOC_DAPM_ADC_E("ADC L", NULL, RT5616_PWR_DIG1,
			   RT5616_PWR_ADC_L_BIT, 0, rt5616_adc_event,
			   SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_ADC_E("ADC R", NULL, RT5616_PWR_DIG1,
			   RT5616_PWR_ADC_R_BIT, 0, rt5616_adc_event,
			   SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("stereo1 filter", RT5616_PWR_DIG2,
			    RT5616_PWR_ADC_STO1_F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5616_sto1_adc_l_mix,
			   ARRAY_SIZE(rt5616_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5616_sto1_adc_r_mix,
			   ARRAY_SIZE(rt5616_sto1_adc_r_mix)),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5616_PWR_DIG1,
			    RT5616_PWR_I2S1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Digital Interface Select */

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Audio DSP */
	SND_SOC_DAPM_PGA("Audio DSP", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5616_dac_l_mix, ARRAY_SIZE(rt5616_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5616_dac_r_mix, ARRAY_SIZE(rt5616_dac_r_mix)),

	SND_SOC_DAPM_SUPPLY("Stero1 DAC Power", RT5616_PWR_DIG2,
			    RT5616_PWR_DAC_STO1_F_BIT, 0, NULL, 0),

	/* DAC Mixer */
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5616_sto_dac_l_mix,
			   ARRAY_SIZE(rt5616_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5616_sto_dac_r_mix,
			   ARRAY_SIZE(rt5616_sto_dac_r_mix)),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, RT5616_PWR_DIG1,
			 RT5616_PWR_DAC_L1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, RT5616_PWR_DIG1,
			 RT5616_PWR_DAC_R1_BIT, 0),
	/* OUT Mixer */
	SND_SOC_DAPM_MIXER("OUT MIXL", RT5616_PWR_MIXER, RT5616_PWR_OM_L_BIT,
			   0, rt5616_out_l_mix, ARRAY_SIZE(rt5616_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5616_PWR_MIXER, RT5616_PWR_OM_R_BIT,
			   0, rt5616_out_r_mix, ARRAY_SIZE(rt5616_out_r_mix)),
	/* Output Volume */
	SND_SOC_DAPM_PGA("OUTVOL L", RT5616_PWR_VOL,
			 RT5616_PWR_OV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("OUTVOL R", RT5616_PWR_VOL,
			 RT5616_PWR_OV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL L", RT5616_PWR_VOL,
			 RT5616_PWR_HV_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL R", RT5616_PWR_VOL,
			 RT5616_PWR_HV_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC 1", SND_SOC_NOPM,
			 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC 2", SND_SOC_NOPM,
			 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPOVOL", SND_SOC_NOPM,
			 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INL1", RT5616_PWR_VOL,
			 RT5616_PWR_IN1_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR1", RT5616_PWR_VOL,
			 RT5616_PWR_IN1_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INL2", RT5616_PWR_VOL,
			 RT5616_PWR_IN2_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR2", RT5616_PWR_VOL,
			 RT5616_PWR_IN2_R_BIT, 0, NULL, 0),
	/* HPO/LOUT/Mono Mixer */
	SND_SOC_DAPM_MIXER("HPO MIX", SND_SOC_NOPM, 0, 0,
			   rt5616_hpo_mix, ARRAY_SIZE(rt5616_hpo_mix)),
	SND_SOC_DAPM_MIXER("LOUT MIX", SND_SOC_NOPM, 0, 0,
			   rt5616_lout_mix, ARRAY_SIZE(rt5616_lout_mix)),

	SND_SOC_DAPM_PGA_S("HP amp", 1, SND_SOC_NOPM, 0, 0,
			   rt5616_hp_event, SND_SOC_DAPM_PRE_PMD |
			   SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("LOUT amp", 1, SND_SOC_NOPM, 0, 0,
			   rt5616_lout_event, SND_SOC_DAPM_PRE_PMD |
			   SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_SUPPLY_S("Charge Pump", 1, SND_SOC_NOPM, 0, 0,
			      rt5616_charge_pump_event, SND_SOC_DAPM_POST_PMU |
			      SND_SOC_DAPM_PRE_PMD),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
};

static const struct snd_soc_dapm_route rt5616_dapm_routes[] = {
	{"IN1P", NULL, "LDO"},
	{"IN2P", NULL, "LDO"},

	{"IN1P", NULL, "MIC1"},
	{"IN2P", NULL, "MIC2"},
	{"IN2N", NULL, "MIC2"},

	{"BST1", NULL, "IN1P"},
	{"BST2", NULL, "IN2P"},
	{"BST2", NULL, "IN2N"},
	{"BST1", NULL, "micbias1"},
	{"BST2", NULL, "micbias1"},

	{"INL1 VOL", NULL, "IN2P"},
	{"INR1 VOL", NULL, "IN2N"},

	{"RECMIXL", "INL1 Switch", "INL1 VOL"},
	{"RECMIXL", "BST2 Switch", "BST2"},
	{"RECMIXL", "BST1 Switch", "BST1"},

	{"RECMIXR", "INR1 Switch", "INR1 VOL"},
	{"RECMIXR", "BST2 Switch", "BST2"},
	{"RECMIXR", "BST1 Switch", "BST1"},

	{"ADC L", NULL, "RECMIXL"},
	{"ADC R", NULL, "RECMIXR"},

	{"Stereo1 ADC MIXL", "ADC1 Switch", "ADC L"},
	{"Stereo1 ADC MIXL", NULL, "stereo1 filter"},
	{"stereo1 filter", NULL, "PLL1", is_sys_clk_from_pll},

	{"Stereo1 ADC MIXR", "ADC1 Switch", "ADC R"},
	{"Stereo1 ADC MIXR", NULL, "stereo1 filter"},
	{"stereo1 filter", NULL, "PLL1", is_sys_clk_from_pll},

	{"IF1 ADC1", NULL, "Stereo1 ADC MIXL"},
	{"IF1 ADC1", NULL, "Stereo1 ADC MIXR"},
	{"IF1 ADC1", NULL, "I2S1"},

	{"AIF1TX", NULL, "IF1 ADC1"},

	{"IF1 DAC", NULL, "AIF1RX"},
	{"IF1 DAC", NULL, "I2S1"},

	{"IF1 DAC1 L", NULL, "IF1 DAC"},
	{"IF1 DAC1 R", NULL, "IF1 DAC"},

	{"DAC MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL"},
	{"DAC MIXL", "INF1 Switch", "IF1 DAC1 L"},
	{"DAC MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR"},
	{"DAC MIXR", "INF1 Switch", "IF1 DAC1 R"},

	{"Audio DSP", NULL, "DAC MIXL"},
	{"Audio DSP", NULL, "DAC MIXR"},

	{"Stereo DAC MIXL", "DAC L1 Switch", "Audio DSP"},
	{"Stereo DAC MIXL", "DAC R1 Switch", "DAC MIXR"},
	{"Stereo DAC MIXL", NULL, "Stero1 DAC Power"},
	{"Stereo DAC MIXR", "DAC R1 Switch", "Audio DSP"},
	{"Stereo DAC MIXR", "DAC L1 Switch", "DAC MIXL"},
	{"Stereo DAC MIXR", NULL, "Stero1 DAC Power"},

	{"DAC L1", NULL, "Stereo DAC MIXL"},
	{"DAC L1", NULL, "PLL1", is_sys_clk_from_pll},
	{"DAC R1", NULL, "Stereo DAC MIXR"},
	{"DAC R1", NULL, "PLL1", is_sys_clk_from_pll},

	{"OUT MIXL", "BST1 Switch", "BST1"},
	{"OUT MIXL", "BST2 Switch", "BST2"},
	{"OUT MIXL", "INL1 Switch", "INL1 VOL"},
	{"OUT MIXL", "REC MIXL Switch", "RECMIXL"},
	{"OUT MIXL", "DAC L1 Switch", "DAC L1"},

	{"OUT MIXR", "BST2 Switch", "BST2"},
	{"OUT MIXR", "BST1 Switch", "BST1"},
	{"OUT MIXR", "INR1 Switch", "INR1 VOL"},
	{"OUT MIXR", "REC MIXR Switch", "RECMIXR"},
	{"OUT MIXR", "DAC R1 Switch", "DAC R1"},

	{"HPOVOL L", NULL, "OUT MIXL"},
	{"HPOVOL R", NULL, "OUT MIXR"},
	{"OUTVOL L", NULL, "OUT MIXL"},
	{"OUTVOL R", NULL, "OUT MIXR"},

	{"DAC 1", NULL, "DAC L1"},
	{"DAC 1", NULL, "DAC R1"},
	{"HPOVOL", NULL, "HPOVOL L"},
	{"HPOVOL", NULL, "HPOVOL R"},
	{"HPO MIX", "DAC1 Switch", "DAC 1"},
	{"HPO MIX", "HPVOL Switch", "HPOVOL"},

	{"LOUT MIX", "DAC L1 Switch", "DAC L1"},
	{"LOUT MIX", "DAC R1 Switch", "DAC R1"},
	{"LOUT MIX", "OUTVOL L Switch", "OUTVOL L"},
	{"LOUT MIX", "OUTVOL R Switch", "OUTVOL R"},

	{"HP amp", NULL, "HPO MIX"},
	{"HP amp", NULL, "Charge Pump"},
	{"HPOL", NULL, "HP amp"},
	{"HPOR", NULL, "HP amp"},

	{"LOUT amp", NULL, "LOUT MIX"},
	{"LOUT amp", NULL, "Charge Pump"},
	{"LOUTL", NULL, "LOUT amp"},
	{"LOUTR", NULL, "LOUT amp"},

};

static int rt5616_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt5616->lrck[dai->id] = params_rate(params);

	pre_div = rl6231_get_clk_info(rt5616->sysclk, rt5616->lrck[dai->id]);

	if (pre_div < 0) {
		dev_err(codec->dev, "Unsupported clock setting\n");
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(codec->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}
	bclk_ms = frame_size > 32 ? 1 : 0;
	rt5616->bclk[dai->id] = rt5616->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5616->bclk[dai->id], rt5616->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
		bclk_ms, pre_div, dai->id);

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val_len |= RT5616_I2S_DL_20;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val_len |= RT5616_I2S_DL_24;
		break;
	case SNDRV_PCM_FORMAT_S8:
		val_len |= RT5616_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	mask_clk = RT5616_I2S_PD1_MASK;
	val_clk = pre_div << RT5616_I2S_PD1_SFT;
	snd_soc_update_bits(codec, RT5616_I2S1_SDP,
			    RT5616_I2S_DL_MASK, val_len);
	snd_soc_update_bits(codec, RT5616_ADDA_CLK1, mask_clk, val_clk);

	return 0;
}

static int rt5616_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5616->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5616_I2S_MS_S;
		rt5616->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5616_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5616_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5616_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5616_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RT5616_I2S1_SDP,
			    RT5616_I2S_MS_MASK | RT5616_I2S_BP_MASK |
			    RT5616_I2S_DF_MASK, reg_val);

	return 0;
}

static int rt5616_set_dai_sysclk(struct snd_soc_dai *dai,
				 int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);
	unsigned int reg_val = 0;

	if (freq == rt5616->sysclk && clk_id == rt5616->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5616_SCLK_S_MCLK:
		reg_val |= RT5616_SCLK_SRC_MCLK;
		break;
	case RT5616_SCLK_S_PLL1:
		reg_val |= RT5616_SCLK_SRC_PLL1;
		break;
	default:
		dev_err(codec->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	snd_soc_update_bits(codec, RT5616_GLB_CLK,
			    RT5616_SCLK_SRC_MASK, reg_val);
	rt5616->sysclk = freq;
	rt5616->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

static int rt5616_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			      unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt5616->pll_src && freq_in == rt5616->pll_in &&
	    freq_out == rt5616->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(codec->dev, "PLL disabled\n");

		rt5616->pll_in = 0;
		rt5616->pll_out = 0;
		snd_soc_update_bits(codec, RT5616_GLB_CLK,
				    RT5616_SCLK_SRC_MASK,
				    RT5616_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5616_PLL1_S_MCLK:
		snd_soc_update_bits(codec, RT5616_GLB_CLK,
				    RT5616_PLL1_SRC_MASK,
				    RT5616_PLL1_SRC_MCLK);
		break;
	case RT5616_PLL1_S_BCLK1:
	case RT5616_PLL1_S_BCLK2:
		snd_soc_update_bits(codec, RT5616_GLB_CLK,
				    RT5616_PLL1_SRC_MASK,
				    RT5616_PLL1_SRC_BCLK1);
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

	snd_soc_write(codec, RT5616_PLL_CTRL1,
		      pll_code.n_code << RT5616_PLL_N_SFT | pll_code.k_code);
	snd_soc_write(codec, RT5616_PLL_CTRL2,
		      (pll_code.m_bp ? 0 : pll_code.m_code) <<
		      RT5616_PLL_M_SFT |
		      pll_code.m_bp << RT5616_PLL_M_BP_SFT);

	rt5616->pll_in = freq_in;
	rt5616->pll_out = freq_out;
	rt5616->pll_src = source;

	return 0;
}

static int rt5616_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);
	int ret;

	switch (level) {

	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		/*
		 * SND_SOC_BIAS_PREPARE is called while preparing for a
		 * transition to ON or away from ON. If current bias_level
		 * is SND_SOC_BIAS_ON, then it is preparing for a transition
		 * away from ON. Disable the clock in that case, otherwise
		 * enable it.
		 */
		if (IS_ERR(rt5616->mclk))
			break;

		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_ON) {
			clk_disable_unprepare(rt5616->mclk);
		} else {
			ret = clk_prepare_enable(rt5616->mclk);
			if (ret)
				return ret;
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_codec_get_bias_level(codec) == SND_SOC_BIAS_OFF) {
			snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
					    RT5616_PWR_VREF1 | RT5616_PWR_MB |
					    RT5616_PWR_BG | RT5616_PWR_VREF2,
					    RT5616_PWR_VREF1 | RT5616_PWR_MB |
					    RT5616_PWR_BG | RT5616_PWR_VREF2);
			mdelay(10);
			snd_soc_update_bits(codec, RT5616_PWR_ANLG1,
					    RT5616_PWR_FV1 | RT5616_PWR_FV2,
					    RT5616_PWR_FV1 | RT5616_PWR_FV2);
			snd_soc_update_bits(codec, RT5616_D_MISC,
					    RT5616_D_GATE_EN,
					    RT5616_D_GATE_EN);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, RT5616_D_MISC, RT5616_D_GATE_EN, 0);
		snd_soc_write(codec, RT5616_PWR_DIG1, 0x0000);
		snd_soc_write(codec, RT5616_PWR_DIG2, 0x0000);
		snd_soc_write(codec, RT5616_PWR_VOL, 0x0000);
		snd_soc_write(codec, RT5616_PWR_MIXER, 0x0000);
		snd_soc_write(codec, RT5616_PWR_ANLG1, 0x0000);
		snd_soc_write(codec, RT5616_PWR_ANLG2, 0x0000);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5616_probe(struct snd_soc_codec *codec)
{
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);

	/* Check if MCLK provided */
	rt5616->mclk = devm_clk_get(codec->dev, "mclk");
	if (PTR_ERR(rt5616->mclk) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	rt5616->codec = codec;

	return 0;
}

#ifdef CONFIG_PM
static int rt5616_suspend(struct snd_soc_codec *codec)
{
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5616->regmap, true);
	regcache_mark_dirty(rt5616->regmap);

	return 0;
}

static int rt5616_resume(struct snd_soc_codec *codec)
{
	struct rt5616_priv *rt5616 = snd_soc_codec_get_drvdata(codec);

	regcache_cache_only(rt5616->regmap, false);
	regcache_sync(rt5616->regmap);
	return 0;
}
#else
#define rt5616_suspend NULL
#define rt5616_resume NULL
#endif

#define RT5616_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5616_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static struct snd_soc_dai_ops rt5616_aif_dai_ops = {
	.hw_params = rt5616_hw_params,
	.set_fmt = rt5616_set_dai_fmt,
	.set_sysclk = rt5616_set_dai_sysclk,
	.set_pll = rt5616_set_dai_pll,
};

static struct snd_soc_dai_driver rt5616_dai[] = {
	{
		.name = "rt5616-aif1",
		.id = RT5616_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5616_STEREO_RATES,
			.formats = RT5616_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5616_STEREO_RATES,
			.formats = RT5616_FORMATS,
		},
		.ops = &rt5616_aif_dai_ops,
	},
};

static struct snd_soc_codec_driver soc_codec_dev_rt5616 = {
	.probe = rt5616_probe,
	.suspend = rt5616_suspend,
	.resume = rt5616_resume,
	.set_bias_level = rt5616_set_bias_level,
	.idle_bias_off = true,
	.controls = rt5616_snd_controls,
	.num_controls = ARRAY_SIZE(rt5616_snd_controls),
	.dapm_widgets = rt5616_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt5616_dapm_widgets),
	.dapm_routes = rt5616_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt5616_dapm_routes),
};

static const struct regmap_config rt5616_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.use_single_rw = true,
	.max_register = RT5616_DEVICE_ID + 1 + (ARRAY_SIZE(rt5616_ranges) *
					       RT5616_PR_SPACING),
	.volatile_reg = rt5616_volatile_register,
	.readable_reg = rt5616_readable_register,
	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5616_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5616_reg),
	.ranges = rt5616_ranges,
	.num_ranges = ARRAY_SIZE(rt5616_ranges),
};

static const struct i2c_device_id rt5616_i2c_id[] = {
	{ "rt5616", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5616_i2c_id);

#if defined(CONFIG_OF)
static const struct of_device_id rt5616_of_match[] = {
	{ .compatible = "realtek,rt5616", },
	{},
};
MODULE_DEVICE_TABLE(of, rt5616_of_match);
#endif

static int rt5616_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct rt5616_priv *rt5616;
	unsigned int val;
	int ret;

	rt5616 = devm_kzalloc(&i2c->dev, sizeof(struct rt5616_priv),
			      GFP_KERNEL);
	if (!rt5616)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5616);

	rt5616->regmap = devm_regmap_init_i2c(i2c, &rt5616_regmap);
	if (IS_ERR(rt5616->regmap)) {
		ret = PTR_ERR(rt5616->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt5616->regmap, RT5616_DEVICE_ID, &val);
	if (val != 0x6281) {
		dev_err(&i2c->dev,
			"Device with ID register %#x is not rt5616\n",
			val);
		return -ENODEV;
	}
	regmap_write(rt5616->regmap, RT5616_RESET, 0);
	regmap_update_bits(rt5616->regmap, RT5616_PWR_ANLG1,
			   RT5616_PWR_VREF1 | RT5616_PWR_MB |
			   RT5616_PWR_BG | RT5616_PWR_VREF2,
			   RT5616_PWR_VREF1 | RT5616_PWR_MB |
			   RT5616_PWR_BG | RT5616_PWR_VREF2);
	mdelay(10);
	regmap_update_bits(rt5616->regmap, RT5616_PWR_ANLG1,
			   RT5616_PWR_FV1 | RT5616_PWR_FV2,
			   RT5616_PWR_FV1 | RT5616_PWR_FV2);

	ret = regmap_register_patch(rt5616->regmap, init_list,
				    ARRAY_SIZE(init_list));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	regmap_update_bits(rt5616->regmap, RT5616_PWR_ANLG1,
			   RT5616_PWR_LDO_DVO_MASK, RT5616_PWR_LDO_DVO_1_2V);

	return snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt5616,
				      rt5616_dai, ARRAY_SIZE(rt5616_dai));
}

static int rt5616_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}

static void rt5616_i2c_shutdown(struct i2c_client *client)
{
	struct rt5616_priv *rt5616 = i2c_get_clientdata(client);

	regmap_write(rt5616->regmap, RT5616_HP_VOL, 0xc8c8);
	regmap_write(rt5616->regmap, RT5616_LOUT_CTRL1, 0xc8c8);
}

static struct i2c_driver rt5616_i2c_driver = {
	.driver = {
		.name = "rt5616",
		.of_match_table = of_match_ptr(rt5616_of_match),
	},
	.probe = rt5616_i2c_probe,
	.remove = rt5616_i2c_remove,
	.shutdown = rt5616_i2c_shutdown,
	.id_table = rt5616_i2c_id,
};
module_i2c_driver(rt5616_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5616 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL");
