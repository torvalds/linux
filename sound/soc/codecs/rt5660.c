/*
 * rt5660.c  --  RT5660 ALSA SoC audio codec driver
 *
 * Copyright 2016 Realtek Semiconductor Corp.
 * Author: Oder Chiou <oder_chiou@realtek.com>
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
#include "rt5660.h"

#define RT5660_DEVICE_ID 0x6338

#define RT5660_PR_RANGE_BASE (0xff + 1)
#define RT5660_PR_SPACING 0x100

#define RT5660_PR_BASE (RT5660_PR_RANGE_BASE + (0 * RT5660_PR_SPACING))

static const struct regmap_range_cfg rt5660_ranges[] = {
	{ .name = "PR", .range_min = RT5660_PR_BASE,
	  .range_max = RT5660_PR_BASE + 0xf3,
	  .selector_reg = RT5660_PRIV_INDEX,
	  .selector_mask = 0xff,
	  .selector_shift = 0x0,
	  .window_start = RT5660_PRIV_DATA,
	  .window_len = 0x1, },
};

static const struct reg_sequence rt5660_patch[] = {
	{ RT5660_ALC_PGA_CTRL2,		0x44c3 },
	{ RT5660_PR_BASE + 0x3d,	0x2600 },
};

static const struct reg_default rt5660_reg[] = {
	{ 0x00, 0x0000 },
	{ 0x01, 0xc800 },
	{ 0x02, 0xc8c8 },
	{ 0x0d, 0x1010 },
	{ 0x0e, 0x1010 },
	{ 0x19, 0xafaf },
	{ 0x1c, 0x2f2f },
	{ 0x1e, 0x0000 },
	{ 0x27, 0x6060 },
	{ 0x29, 0x8080 },
	{ 0x2a, 0x4242 },
	{ 0x2f, 0x0000 },
	{ 0x3b, 0x0000 },
	{ 0x3c, 0x007f },
	{ 0x3d, 0x0000 },
	{ 0x3e, 0x007f },
	{ 0x45, 0xe000 },
	{ 0x46, 0x003e },
	{ 0x48, 0xf800 },
	{ 0x4a, 0x0004 },
	{ 0x4d, 0x0000 },
	{ 0x4e, 0x0000 },
	{ 0x4f, 0x01ff },
	{ 0x50, 0x0000 },
	{ 0x51, 0x0000 },
	{ 0x52, 0x01ff },
	{ 0x61, 0x0000 },
	{ 0x62, 0x0000 },
	{ 0x63, 0x00c0 },
	{ 0x64, 0x0000 },
	{ 0x65, 0x0000 },
	{ 0x66, 0x0000 },
	{ 0x70, 0x8000 },
	{ 0x73, 0x7000 },
	{ 0x74, 0x3c00 },
	{ 0x75, 0x2800 },
	{ 0x80, 0x0000 },
	{ 0x81, 0x0000 },
	{ 0x82, 0x0000 },
	{ 0x8c, 0x0228 },
	{ 0x8d, 0xa000 },
	{ 0x8e, 0x0000 },
	{ 0x92, 0x0000 },
	{ 0x93, 0x3000 },
	{ 0xa1, 0x0059 },
	{ 0xa2, 0x0001 },
	{ 0xa3, 0x5c80 },
	{ 0xa4, 0x0146 },
	{ 0xa5, 0x1f1f },
	{ 0xa6, 0x78c6 },
	{ 0xa7, 0xe5ec },
	{ 0xa8, 0xba61 },
	{ 0xa9, 0x3c78 },
	{ 0xaa, 0x8ae2 },
	{ 0xab, 0xe5ec },
	{ 0xac, 0xc600 },
	{ 0xad, 0xba61 },
	{ 0xae, 0x17ed },
	{ 0xb0, 0x2080 },
	{ 0xb1, 0x0000 },
	{ 0xb3, 0x001f },
	{ 0xb4, 0x020c },
	{ 0xb5, 0x1f00 },
	{ 0xb6, 0x0000 },
	{ 0xb7, 0x4000 },
	{ 0xbb, 0x0000 },
	{ 0xbd, 0x0000 },
	{ 0xbe, 0x0000 },
	{ 0xbf, 0x0100 },
	{ 0xc0, 0x0000 },
	{ 0xc2, 0x0000 },
	{ 0xd3, 0xa220 },
	{ 0xd9, 0x0809 },
	{ 0xda, 0x0000 },
	{ 0xe0, 0x8000 },
	{ 0xe1, 0x0200 },
	{ 0xe2, 0x8000 },
	{ 0xe3, 0x0200 },
	{ 0xe4, 0x0f20 },
	{ 0xe5, 0x001f },
	{ 0xe6, 0x020c },
	{ 0xe7, 0x1f00 },
	{ 0xe8, 0x0000 },
	{ 0xe9, 0x4000 },
	{ 0xea, 0x00a6 },
	{ 0xeb, 0x04c3 },
	{ 0xec, 0x27c8 },
	{ 0xed, 0x7418 },
	{ 0xee, 0xbf50 },
	{ 0xef, 0x0045 },
	{ 0xf0, 0x0007 },
	{ 0xfa, 0x0000 },
	{ 0xfd, 0x0000 },
	{ 0xfe, 0x10ec },
	{ 0xff, 0x6338 },
};

static bool rt5660_volatile_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5660_ranges); i++)
		if ((reg >= rt5660_ranges[i].window_start &&
		     reg <= rt5660_ranges[i].window_start +
		     rt5660_ranges[i].window_len) ||
		    (reg >= rt5660_ranges[i].range_min &&
		     reg <= rt5660_ranges[i].range_max))
			return true;

	switch (reg) {
	case RT5660_RESET:
	case RT5660_PRIV_DATA:
	case RT5660_EQ_CTRL1:
	case RT5660_IRQ_CTRL2:
	case RT5660_INT_IRQ_ST:
	case RT5660_VENDOR_ID:
	case RT5660_VENDOR_ID1:
	case RT5660_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static bool rt5660_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5660_ranges); i++)
		if ((reg >= rt5660_ranges[i].window_start &&
		     reg <= rt5660_ranges[i].window_start +
		     rt5660_ranges[i].window_len) ||
		    (reg >= rt5660_ranges[i].range_min &&
		     reg <= rt5660_ranges[i].range_max))
			return true;

	switch (reg) {
	case RT5660_RESET:
	case RT5660_SPK_VOL:
	case RT5660_LOUT_VOL:
	case RT5660_IN1_IN2:
	case RT5660_IN3_IN4:
	case RT5660_DAC1_DIG_VOL:
	case RT5660_STO1_ADC_DIG_VOL:
	case RT5660_ADC_BST_VOL1:
	case RT5660_STO1_ADC_MIXER:
	case RT5660_AD_DA_MIXER:
	case RT5660_STO_DAC_MIXER:
	case RT5660_DIG_INF1_DATA:
	case RT5660_REC_L1_MIXER:
	case RT5660_REC_L2_MIXER:
	case RT5660_REC_R1_MIXER:
	case RT5660_REC_R2_MIXER:
	case RT5660_LOUT_MIXER:
	case RT5660_SPK_MIXER:
	case RT5660_SPO_MIXER:
	case RT5660_SPO_CLSD_RATIO:
	case RT5660_OUT_L_GAIN1:
	case RT5660_OUT_L_GAIN2:
	case RT5660_OUT_L1_MIXER:
	case RT5660_OUT_R_GAIN1:
	case RT5660_OUT_R_GAIN2:
	case RT5660_OUT_R1_MIXER:
	case RT5660_PWR_DIG1:
	case RT5660_PWR_DIG2:
	case RT5660_PWR_ANLG1:
	case RT5660_PWR_ANLG2:
	case RT5660_PWR_MIXER:
	case RT5660_PWR_VOL:
	case RT5660_PRIV_INDEX:
	case RT5660_PRIV_DATA:
	case RT5660_I2S1_SDP:
	case RT5660_ADDA_CLK1:
	case RT5660_ADDA_CLK2:
	case RT5660_DMIC_CTRL1:
	case RT5660_GLB_CLK:
	case RT5660_PLL_CTRL1:
	case RT5660_PLL_CTRL2:
	case RT5660_CLSD_AMP_OC_CTRL:
	case RT5660_CLSD_AMP_CTRL:
	case RT5660_LOUT_AMP_CTRL:
	case RT5660_SPK_AMP_SPKVDD:
	case RT5660_MICBIAS:
	case RT5660_CLSD_OUT_CTRL1:
	case RT5660_CLSD_OUT_CTRL2:
	case RT5660_DIPOLE_MIC_CTRL1:
	case RT5660_DIPOLE_MIC_CTRL2:
	case RT5660_DIPOLE_MIC_CTRL3:
	case RT5660_DIPOLE_MIC_CTRL4:
	case RT5660_DIPOLE_MIC_CTRL5:
	case RT5660_DIPOLE_MIC_CTRL6:
	case RT5660_DIPOLE_MIC_CTRL7:
	case RT5660_DIPOLE_MIC_CTRL8:
	case RT5660_DIPOLE_MIC_CTRL9:
	case RT5660_DIPOLE_MIC_CTRL10:
	case RT5660_DIPOLE_MIC_CTRL11:
	case RT5660_DIPOLE_MIC_CTRL12:
	case RT5660_EQ_CTRL1:
	case RT5660_EQ_CTRL2:
	case RT5660_DRC_AGC_CTRL1:
	case RT5660_DRC_AGC_CTRL2:
	case RT5660_DRC_AGC_CTRL3:
	case RT5660_DRC_AGC_CTRL4:
	case RT5660_DRC_AGC_CTRL5:
	case RT5660_JD_CTRL:
	case RT5660_IRQ_CTRL1:
	case RT5660_IRQ_CTRL2:
	case RT5660_INT_IRQ_ST:
	case RT5660_GPIO_CTRL1:
	case RT5660_GPIO_CTRL2:
	case RT5660_WIND_FILTER_CTRL1:
	case RT5660_SV_ZCD1:
	case RT5660_SV_ZCD2:
	case RT5660_DRC1_LM_CTRL1:
	case RT5660_DRC1_LM_CTRL2:
	case RT5660_DRC2_LM_CTRL1:
	case RT5660_DRC2_LM_CTRL2:
	case RT5660_MULTI_DRC_CTRL:
	case RT5660_DRC2_CTRL1:
	case RT5660_DRC2_CTRL2:
	case RT5660_DRC2_CTRL3:
	case RT5660_DRC2_CTRL4:
	case RT5660_DRC2_CTRL5:
	case RT5660_ALC_PGA_CTRL1:
	case RT5660_ALC_PGA_CTRL2:
	case RT5660_ALC_PGA_CTRL3:
	case RT5660_ALC_PGA_CTRL4:
	case RT5660_ALC_PGA_CTRL5:
	case RT5660_ALC_PGA_CTRL6:
	case RT5660_ALC_PGA_CTRL7:
	case RT5660_GEN_CTRL1:
	case RT5660_GEN_CTRL2:
	case RT5660_GEN_CTRL3:
	case RT5660_VENDOR_ID:
	case RT5660_VENDOR_ID1:
	case RT5660_VENDOR_ID2:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(rt5660_out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(rt5660_dac_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(rt5660_adc_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(rt5660_adc_bst_tlv, 0, 1200, 0);
static const DECLARE_TLV_DB_SCALE(rt5660_bst_tlv, -1200, 75, 0);

static const struct snd_kcontrol_new rt5660_snd_controls[] = {
	/* Speaker Output Volume */
	SOC_SINGLE("Speaker Playback Switch", RT5660_SPK_VOL, RT5660_L_MUTE_SFT,
		1, 1),
	SOC_SINGLE_TLV("Speaker Playback Volume", RT5660_SPK_VOL,
		RT5660_L_VOL_SFT, 39, 1, rt5660_out_vol_tlv),

	/* OUTPUT Control */
	SOC_DOUBLE("OUT Playback Switch", RT5660_LOUT_VOL, RT5660_L_MUTE_SFT,
		RT5660_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5660_LOUT_VOL, RT5660_L_VOL_SFT,
		RT5660_R_VOL_SFT, 39, 1, rt5660_out_vol_tlv),

	/* DAC Digital Volume */
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5660_DAC1_DIG_VOL,
		RT5660_DAC_L1_VOL_SFT, RT5660_DAC_R1_VOL_SFT, 87, 0,
		rt5660_dac_vol_tlv),

	/* IN1/IN2/IN3 Control */
	SOC_SINGLE_TLV("IN1 Boost Volume", RT5660_IN1_IN2, RT5660_BST_SFT1, 69,
		0, rt5660_bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost Volume", RT5660_IN1_IN2, RT5660_BST_SFT2, 69,
		0, rt5660_bst_tlv),
	SOC_SINGLE_TLV("IN3 Boost Volume", RT5660_IN3_IN4, RT5660_BST_SFT3, 69,
		0, rt5660_bst_tlv),

	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT5660_STO1_ADC_DIG_VOL,
		RT5660_L_MUTE_SFT, RT5660_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT5660_STO1_ADC_DIG_VOL,
		RT5660_ADC_L_VOL_SFT, RT5660_ADC_R_VOL_SFT, 63, 0,
		rt5660_adc_vol_tlv),

	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("STO1 ADC Boost Gain Volume", RT5660_ADC_BST_VOL1,
		RT5660_STO1_ADC_L_BST_SFT, RT5660_STO1_ADC_R_BST_SFT, 3, 0,
		rt5660_adc_bst_tlv),
};

/**
 * rt5660_set_dmic_clk - Set parameter of dmic.
 *
 * @w: DAPM widget.
 * @kcontrol: The kcontrol of this widget.
 * @event: Event id.
 *
 */
static int rt5660_set_dmic_clk(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);
	int idx, rate;

	rate = rt5660->sysclk / rl6231_get_pre_div(rt5660->regmap,
		RT5660_ADDA_CLK1, RT5660_I2S_PD1_SFT);
	idx = rl6231_calc_dmic_clk(rate);
	if (idx < 0)
		dev_err(component->dev, "Failed to set DMIC clock\n");
	else
		snd_soc_component_update_bits(component, RT5660_DMIC_CTRL1,
			RT5660_DMIC_CLK_MASK, idx << RT5660_DMIC_CLK_SFT);

	return idx;
}

static int rt5660_is_sys_clk_from_pll(struct snd_soc_dapm_widget *source,
			 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(source->dapm);
	unsigned int val;

	val = snd_soc_component_read32(component, RT5660_GLB_CLK);
	val &= RT5660_SCLK_SRC_MASK;
	if (val == RT5660_SCLK_SRC_PLL1)
		return 1;
	else
		return 0;
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5660_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5660_STO1_ADC_MIXER,
			RT5660_M_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5660_STO1_ADC_MIXER,
			RT5660_M_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5660_STO1_ADC_MIXER,
			RT5660_M_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5660_STO1_ADC_MIXER,
			RT5660_M_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5660_AD_DA_MIXER,
			RT5660_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5660_AD_DA_MIXER,
			RT5660_M_DAC1_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5660_AD_DA_MIXER,
			RT5660_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC1 Switch", RT5660_AD_DA_MIXER,
			RT5660_M_DAC1_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5660_STO_DAC_MIXER,
			RT5660_M_DAC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5660_STO_DAC_MIXER,
			RT5660_M_DAC_R1_STO_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5660_STO_DAC_MIXER,
			RT5660_M_DAC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5660_STO_DAC_MIXER,
			RT5660_M_DAC_L1_STO_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5660_rec_l_mix[] = {
	SOC_DAPM_SINGLE("BST3 Switch", RT5660_REC_L2_MIXER,
			RT5660_M_BST3_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5660_REC_L2_MIXER,
			RT5660_M_BST2_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5660_REC_L2_MIXER,
			RT5660_M_BST1_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXL Switch", RT5660_REC_L2_MIXER,
			RT5660_M_OM_L_RM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_rec_r_mix[] = {
	SOC_DAPM_SINGLE("BST3 Switch", RT5660_REC_R2_MIXER,
			RT5660_M_BST3_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5660_REC_R2_MIXER,
			RT5660_M_BST2_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5660_REC_R2_MIXER,
			RT5660_M_BST1_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUT MIXR Switch", RT5660_REC_R2_MIXER,
			RT5660_M_OM_R_RM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_spk_mix[] = {
	SOC_DAPM_SINGLE("BST3 Switch", RT5660_SPK_MIXER,
			RT5660_M_BST3_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5660_SPK_MIXER,
			RT5660_M_BST1_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACL Switch", RT5660_SPK_MIXER,
			RT5660_M_DACL_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACR Switch", RT5660_SPK_MIXER,
			RT5660_M_DACR_SM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIXL Switch", RT5660_SPK_MIXER,
			RT5660_M_OM_L_SM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_out_l_mix[] = {
	SOC_DAPM_SINGLE("BST3 Switch", RT5660_OUT_L1_MIXER,
			RT5660_M_BST3_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5660_OUT_L1_MIXER,
			RT5660_M_BST2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5660_OUT_L1_MIXER,
			RT5660_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("RECMIXL Switch", RT5660_OUT_L1_MIXER,
			RT5660_M_RM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACR Switch", RT5660_OUT_L1_MIXER,
			RT5660_M_DAC_R_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACL Switch", RT5660_OUT_L1_MIXER,
			RT5660_M_DAC_L_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_out_r_mix[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5660_OUT_R1_MIXER,
			RT5660_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5660_OUT_R1_MIXER,
			RT5660_M_BST1_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("RECMIXR Switch", RT5660_OUT_R1_MIXER,
			RT5660_M_RM_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACR Switch", RT5660_OUT_R1_MIXER,
			RT5660_M_DAC_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACL Switch", RT5660_OUT_R1_MIXER,
			RT5660_M_DAC_L_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_spo_mix[] = {
	SOC_DAPM_SINGLE("DACR Switch", RT5660_SPO_MIXER,
			RT5660_M_DAC_R_SPM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DACL Switch", RT5660_SPO_MIXER,
			RT5660_M_DAC_L_SPM_SFT, 1, 1),
	SOC_DAPM_SINGLE("SPKVOL Switch", RT5660_SPO_MIXER,
			RT5660_M_SV_SPM_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5660_SPO_MIXER,
			RT5660_M_BST1_SPM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5660_lout_mix[] = {
	SOC_DAPM_SINGLE("DAC Switch", RT5660_LOUT_MIXER,
			RT5660_M_DAC1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTMIX Switch", RT5660_LOUT_MIXER,
			RT5660_M_LOVOL_LM_SFT, 1, 1),
};

static const struct snd_kcontrol_new spk_vol_control =
	SOC_DAPM_SINGLE("Switch", RT5660_SPK_VOL,
		RT5660_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new lout_l_vol_control =
	SOC_DAPM_SINGLE("Switch", RT5660_LOUT_VOL,
		RT5660_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new lout_r_vol_control =
	SOC_DAPM_SINGLE("Switch", RT5660_LOUT_VOL,
		RT5660_VOL_R_SFT, 1, 1);

/* Interface data select */
static const char * const rt5660_data_select[] = {
	"L/R", "R/L", "L/L", "R/R"
};

static SOC_ENUM_SINGLE_DECL(rt5660_if1_dac_enum,
	RT5660_DIG_INF1_DATA, RT5660_IF1_DAC_IN_SFT, rt5660_data_select);

static SOC_ENUM_SINGLE_DECL(rt5660_if1_adc_enum,
	RT5660_DIG_INF1_DATA, RT5660_IF1_ADC_IN_SFT, rt5660_data_select);

static const struct snd_kcontrol_new rt5660_if1_dac_swap_mux =
	SOC_DAPM_ENUM("IF1 DAC Swap Source", rt5660_if1_dac_enum);

static const struct snd_kcontrol_new rt5660_if1_adc_swap_mux =
	SOC_DAPM_ENUM("IF1 ADC Swap Source", rt5660_if1_adc_enum);

static int rt5660_lout_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT5660_LOUT_AMP_CTRL,
			RT5660_LOUT_CO_MASK | RT5660_LOUT_CB_MASK,
			RT5660_LOUT_CO_EN | RT5660_LOUT_CB_PU);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT5660_LOUT_AMP_CTRL,
			RT5660_LOUT_CO_MASK | RT5660_LOUT_CB_MASK,
			RT5660_LOUT_CO_DIS | RT5660_LOUT_CB_PD);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5660_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("LDO2", RT5660_PWR_ANLG1,
		RT5660_PWR_LDO2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("PLL1", RT5660_PWR_ANLG2,
		RT5660_PWR_PLL_BIT, 0, NULL, 0),

	/* MICBIAS */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", RT5660_PWR_ANLG2,
			RT5660_PWR_MB1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", RT5660_PWR_ANLG2,
			RT5660_PWR_MB2_BIT, 0, NULL, 0),

	/* Input Side */
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN1N"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN3P"),
	SND_SOC_DAPM_INPUT("IN3N"),

	SND_SOC_DAPM_SUPPLY("DMIC CLK", SND_SOC_NOPM, 0, 0,
		rt5660_set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC Power", RT5660_DMIC_CTRL1,
		RT5660_DMIC_1_EN_SFT, 0, NULL, 0),

	/* Boost */
	SND_SOC_DAPM_PGA("BST1", RT5660_PWR_ANLG2, RT5660_PWR_BST1_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA("BST2", RT5660_PWR_ANLG2, RT5660_PWR_BST2_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA("BST3", RT5660_PWR_ANLG2, RT5660_PWR_BST3_BIT, 0,
		NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL", RT5660_PWR_MIXER, RT5660_PWR_RM_L_BIT,
			0, rt5660_rec_l_mix, ARRAY_SIZE(rt5660_rec_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIXR", RT5660_PWR_MIXER, RT5660_PWR_RM_R_BIT,
			0, rt5660_rec_r_mix, ARRAY_SIZE(rt5660_rec_r_mix)),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC R", NULL, SND_SOC_NOPM, 0, 0),

	SND_SOC_DAPM_SUPPLY("ADC L power", RT5660_PWR_DIG1,
			RT5660_PWR_ADC_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC R power", RT5660_PWR_DIG1,
			RT5660_PWR_ADC_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC clock", RT5660_PR_BASE + RT5660_CHOP_DAC_ADC,
			12, 0, NULL, 0),

	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("adc stereo1 filter", RT5660_PWR_DIG2,
		RT5660_PWR_ADC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Sto1 ADC MIXL", SND_SOC_NOPM, 0, 0,
		rt5660_sto1_adc_l_mix, ARRAY_SIZE(rt5660_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Sto1 ADC MIXR", SND_SOC_NOPM, 0, 0,
		rt5660_sto1_adc_r_mix, ARRAY_SIZE(rt5660_sto1_adc_r_mix)),

	/* ADC */
	SND_SOC_DAPM_ADC("Stereo1 ADC MIXL", NULL, RT5660_STO1_ADC_DIG_VOL,
		RT5660_L_MUTE_SFT, 1),
	SND_SOC_DAPM_ADC("Stereo1 ADC MIXR", NULL, RT5660_STO1_ADC_DIG_VOL,
		RT5660_R_MUTE_SFT, 1),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5660_PWR_DIG1, RT5660_PWR_I2S1_BIT, 0,
		NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX("IF1 DAC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5660_if1_dac_swap_mux),
	SND_SOC_DAPM_PGA("IF1 ADC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX("IF1 ADC Swap Mux", SND_SOC_NOPM, 0, 0,
			&rt5660_if1_adc_swap_mux),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC1 MIXL", SND_SOC_NOPM, 0, 0, rt5660_dac_l_mix,
		ARRAY_SIZE(rt5660_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC1 MIXR", SND_SOC_NOPM, 0, 0, rt5660_dac_r_mix,
		ARRAY_SIZE(rt5660_dac_r_mix)),

	/* DAC Mixer */
	SND_SOC_DAPM_SUPPLY("dac stereo1 filter", RT5660_PWR_DIG2,
		RT5660_PWR_DAC_S1F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
		rt5660_sto_dac_l_mix, ARRAY_SIZE(rt5660_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
		rt5660_sto_dac_r_mix, ARRAY_SIZE(rt5660_sto_dac_r_mix)),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, RT5660_PWR_DIG1,
			RT5660_PWR_DAC_L1_BIT, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, RT5660_PWR_DIG1,
			RT5660_PWR_DAC_R1_BIT, 0),

	/* OUT Mixer */
	SND_SOC_DAPM_MIXER("SPK MIX", RT5660_PWR_MIXER, RT5660_PWR_SM_BIT,
		0, rt5660_spk_mix, ARRAY_SIZE(rt5660_spk_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXL", RT5660_PWR_MIXER, RT5660_PWR_OM_L_BIT,
		0, rt5660_out_l_mix, ARRAY_SIZE(rt5660_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5660_PWR_MIXER, RT5660_PWR_OM_R_BIT,
		0, rt5660_out_r_mix, ARRAY_SIZE(rt5660_out_r_mix)),

	/* Output Volume */
	SND_SOC_DAPM_SWITCH("SPKVOL", RT5660_PWR_VOL,
		RT5660_PWR_SV_BIT, 0, &spk_vol_control),
	SND_SOC_DAPM_PGA("DAC 1", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("LOUTVOL", SND_SOC_NOPM,
		0, 0, NULL, 0),
	SND_SOC_DAPM_SWITCH("LOUTVOL L", SND_SOC_NOPM,
		RT5660_PWR_LV_L_BIT, 0, &lout_l_vol_control),
	SND_SOC_DAPM_SWITCH("LOUTVOL R", SND_SOC_NOPM,
		RT5660_PWR_LV_R_BIT, 0, &lout_r_vol_control),

	/* HPO/LOUT/Mono Mixer */
	SND_SOC_DAPM_MIXER("SPO MIX", SND_SOC_NOPM, 0,
		0, rt5660_spo_mix, ARRAY_SIZE(rt5660_spo_mix)),
	SND_SOC_DAPM_MIXER("LOUT MIX", SND_SOC_NOPM, 0, 0,
		rt5660_lout_mix, ARRAY_SIZE(rt5660_lout_mix)),
	SND_SOC_DAPM_SUPPLY("VREF HP", RT5660_GEN_CTRL1,
		RT5660_PWR_VREF_HP_SFT, 0, NULL, 0),
	SND_SOC_DAPM_PGA_S("LOUT amp", 1, RT5660_PWR_ANLG1,
		RT5660_PWR_HA_BIT, 0, rt5660_lout_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("SPK amp", 1, RT5660_PWR_DIG1,
		RT5660_PWR_CLS_D_BIT, 0, NULL, 0),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
	SND_SOC_DAPM_OUTPUT("SPO"),
};

static const struct snd_soc_dapm_route rt5660_dapm_routes[] = {
	{ "MICBIAS1", NULL, "LDO2" },
	{ "MICBIAS2", NULL, "LDO2" },

	{ "BST1", NULL, "IN1P" },
	{ "BST1", NULL, "IN1N" },
	{ "BST2", NULL, "IN2P" },
	{ "BST3", NULL, "IN3P" },
	{ "BST3", NULL, "IN3N" },

	{ "RECMIXL", "BST3 Switch", "BST3" },
	{ "RECMIXL", "BST2 Switch", "BST2" },
	{ "RECMIXL", "BST1 Switch", "BST1" },
	{ "RECMIXL", "OUT MIXL Switch", "OUT MIXL" },

	{ "RECMIXR", "BST3 Switch", "BST3" },
	{ "RECMIXR", "BST2 Switch", "BST2" },
	{ "RECMIXR", "BST1 Switch", "BST1" },
	{ "RECMIXR", "OUT MIXR Switch", "OUT MIXR" },

	{ "ADC L", NULL, "RECMIXL" },
	{ "ADC L", NULL, "ADC L power" },
	{ "ADC L", NULL, "ADC clock" },
	{ "ADC R", NULL, "RECMIXR" },
	{ "ADC R", NULL, "ADC R power" },
	{ "ADC R", NULL, "ADC clock" },

	{"DMIC L1", NULL, "DMIC CLK"},
	{"DMIC L1", NULL, "DMIC Power"},
	{"DMIC R1", NULL, "DMIC CLK"},
	{"DMIC R1", NULL, "DMIC Power"},

	{ "Sto1 ADC MIXL", "ADC1 Switch", "ADC L" },
	{ "Sto1 ADC MIXL", "ADC2 Switch", "DMIC L1" },
	{ "Sto1 ADC MIXR", "ADC1 Switch", "ADC R" },
	{ "Sto1 ADC MIXR", "ADC2 Switch", "DMIC R1" },

	{ "Stereo1 ADC MIXL", NULL, "Sto1 ADC MIXL" },
	{ "Stereo1 ADC MIXL", NULL, "adc stereo1 filter" },
	{ "adc stereo1 filter", NULL, "PLL1", rt5660_is_sys_clk_from_pll },

	{ "Stereo1 ADC MIXR", NULL, "Sto1 ADC MIXR" },
	{ "Stereo1 ADC MIXR", NULL, "adc stereo1 filter" },
	{ "adc stereo1 filter", NULL, "PLL1", rt5660_is_sys_clk_from_pll },

	{ "IF1 ADC", NULL, "Stereo1 ADC MIXL" },
	{ "IF1 ADC", NULL, "Stereo1 ADC MIXR" },
	{ "IF1 ADC", NULL, "I2S1" },

	{ "IF1 ADC Swap Mux", "L/R", "IF1 ADC" },
	{ "IF1 ADC Swap Mux", "R/L", "IF1 ADC" },
	{ "IF1 ADC Swap Mux", "L/L", "IF1 ADC" },
	{ "IF1 ADC Swap Mux", "R/R", "IF1 ADC" },
	{ "AIF1TX", NULL, "IF1 ADC Swap Mux" },

	{ "IF1 DAC", NULL, "AIF1RX" },
	{ "IF1 DAC", NULL, "I2S1" },

	{ "IF1 DAC Swap Mux", "L/R", "IF1 DAC" },
	{ "IF1 DAC Swap Mux", "R/L", "IF1 DAC" },
	{ "IF1 DAC Swap Mux", "L/L", "IF1 DAC" },
	{ "IF1 DAC Swap Mux", "R/R", "IF1 DAC" },

	{ "IF1 DAC L", NULL, "IF1 DAC Swap Mux" },
	{ "IF1 DAC R", NULL, "IF1 DAC Swap Mux" },

	{ "DAC1 MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL" },
	{ "DAC1 MIXL", "DAC1 Switch", "IF1 DAC L" },
	{ "DAC1 MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR" },
	{ "DAC1 MIXR", "DAC1 Switch", "IF1 DAC R" },

	{ "Stereo DAC MIXL", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXL", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXL", NULL, "dac stereo1 filter" },
	{ "dac stereo1 filter", NULL, "PLL1", rt5660_is_sys_clk_from_pll },
	{ "Stereo DAC MIXR", "DAC R1 Switch", "DAC1 MIXR" },
	{ "Stereo DAC MIXR", "DAC L1 Switch", "DAC1 MIXL" },
	{ "Stereo DAC MIXR", NULL, "dac stereo1 filter" },
	{ "dac stereo1 filter", NULL, "PLL1", rt5660_is_sys_clk_from_pll },

	{ "DAC L1", NULL, "Stereo DAC MIXL" },
	{ "DAC R1", NULL, "Stereo DAC MIXR" },

	{ "SPK MIX", "BST3 Switch", "BST3" },
	{ "SPK MIX", "BST1 Switch", "BST1" },
	{ "SPK MIX", "DACL Switch", "DAC L1" },
	{ "SPK MIX", "DACR Switch", "DAC R1" },
	{ "SPK MIX", "OUTMIXL Switch", "OUT MIXL" },

	{ "OUT MIXL", "BST3 Switch", "BST3" },
	{ "OUT MIXL", "BST2 Switch", "BST2" },
	{ "OUT MIXL", "BST1 Switch", "BST1" },
	{ "OUT MIXL", "RECMIXL Switch", "RECMIXL" },
	{ "OUT MIXL", "DACR Switch", "DAC R1" },
	{ "OUT MIXL", "DACL Switch", "DAC L1" },

	{ "OUT MIXR", "BST2 Switch", "BST2" },
	{ "OUT MIXR", "BST1 Switch", "BST1" },
	{ "OUT MIXR", "RECMIXR Switch", "RECMIXR" },
	{ "OUT MIXR", "DACR Switch", "DAC R1" },
	{ "OUT MIXR", "DACL Switch", "DAC L1" },

	{ "SPO MIX", "DACR Switch", "DAC R1" },
	{ "SPO MIX", "DACL Switch", "DAC L1" },
	{ "SPO MIX", "SPKVOL Switch", "SPKVOL" },
	{ "SPO MIX", "BST1 Switch", "BST1" },

	{ "SPKVOL", "Switch", "SPK MIX" },
	{ "LOUTVOL L", "Switch", "OUT MIXL" },
	{ "LOUTVOL R", "Switch", "OUT MIXR" },

	{ "LOUTVOL", NULL, "LOUTVOL L" },
	{ "LOUTVOL", NULL, "LOUTVOL R" },

	{ "DAC 1", NULL, "DAC L1" },
	{ "DAC 1", NULL, "DAC R1" },

	{ "LOUT MIX", "DAC Switch", "DAC 1" },
	{ "LOUT MIX", "OUTMIX Switch", "LOUTVOL" },

	{ "LOUT amp", NULL, "LOUT MIX" },
	{ "LOUT amp", NULL, "VREF HP" },
	{ "LOUTL", NULL, "LOUT amp" },
	{ "LOUTR", NULL, "LOUT amp" },

	{ "SPK amp", NULL, "SPO MIX" },
	{ "SPO", NULL, "SPK amp" },
};

static int rt5660_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt5660->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5660->sysclk, rt5660->lrck[dai->id]);
	if (pre_div < 0) {
		dev_err(component->dev, "Unsupported clock setting %d for DAI %d\n",
			rt5660->lrck[dai->id], dai->id);
		return -EINVAL;
	}

	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n", frame_size);
		return frame_size;
	}

	if (frame_size > 32)
		bclk_ms = 1;
	else
		bclk_ms = 0;

	rt5660->bclk[dai->id] = rt5660->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5660->bclk[dai->id], rt5660->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		val_len |= RT5660_I2S_DL_20;
		break;
	case 24:
		val_len |= RT5660_I2S_DL_24;
		break;
	case 8:
		val_len |= RT5660_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5660_AIF1:
		mask_clk = RT5660_I2S_BCLK_MS1_MASK | RT5660_I2S_PD1_MASK;
		val_clk = bclk_ms << RT5660_I2S_BCLK_MS1_SFT |
			pre_div << RT5660_I2S_PD1_SFT;
		snd_soc_component_update_bits(component, RT5660_I2S1_SDP, RT5660_I2S_DL_MASK,
			val_len);
		snd_soc_component_update_bits(component, RT5660_ADDA_CLK1, mask_clk, val_clk);
		break;

	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	return 0;
}

static int rt5660_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5660->master[dai->id] = 1;
		break;

	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5660_I2S_MS_S;
		rt5660->master[dai->id] = 0;
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;

	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5660_I2S_BP_INV;
		break;

	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;

	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5660_I2S_DF_LEFT;
		break;

	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5660_I2S_DF_PCM_A;
		break;

	case SND_SOC_DAIFMT_DSP_B:
		reg_val  |= RT5660_I2S_DF_PCM_B;
		break;

	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5660_AIF1:
		snd_soc_component_update_bits(component, RT5660_I2S1_SDP,
			RT5660_I2S_MS_MASK | RT5660_I2S_BP_MASK |
			RT5660_I2S_DF_MASK, reg_val);
		break;

	default:
		dev_err(component->dev, "Invalid dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	return 0;
}

static int rt5660_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	if (freq == rt5660->sysclk && clk_id == rt5660->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5660_SCLK_S_MCLK:
		reg_val |= RT5660_SCLK_SRC_MCLK;
		break;

	case RT5660_SCLK_S_PLL1:
		reg_val |= RT5660_SCLK_SRC_PLL1;
		break;

	case RT5660_SCLK_S_RCCLK:
		reg_val |= RT5660_SCLK_SRC_RCCLK;
		break;

	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, RT5660_GLB_CLK, RT5660_SCLK_SRC_MASK,
		reg_val);

	rt5660->sysclk = freq;
	rt5660->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

static int rt5660_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = dai->component;
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt5660->pll_src && freq_in == rt5660->pll_in &&
		freq_out == rt5660->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt5660->pll_in = 0;
		rt5660->pll_out = 0;
		snd_soc_component_update_bits(component, RT5660_GLB_CLK,
			RT5660_SCLK_SRC_MASK, RT5660_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5660_PLL1_S_MCLK:
		snd_soc_component_update_bits(component, RT5660_GLB_CLK,
			RT5660_PLL1_SRC_MASK, RT5660_PLL1_SRC_MCLK);
		break;

	case RT5660_PLL1_S_BCLK:
		snd_soc_component_update_bits(component, RT5660_GLB_CLK,
			RT5660_PLL1_SRC_MASK, RT5660_PLL1_SRC_BCLK1);
		break;

	default:
		dev_err(component->dev, "Unknown PLL source %d\n", source);
		return -EINVAL;
	}

	ret = rl6231_pll_calc(freq_in, freq_out, &pll_code);
	if (ret < 0) {
		dev_err(component->dev, "Unsupport input clock %d\n", freq_in);
		return ret;
	}

	dev_dbg(component->dev, "bypass=%d m=%d n=%d k=%d\n",
		pll_code.m_bp, (pll_code.m_bp ? 0 : pll_code.m_code),
		pll_code.n_code, pll_code.k_code);

	snd_soc_component_write(component, RT5660_PLL_CTRL1,
		pll_code.n_code << RT5660_PLL_N_SFT | pll_code.k_code);
	snd_soc_component_write(component, RT5660_PLL_CTRL2,
		(pll_code.m_bp ? 0 : pll_code.m_code) << RT5660_PLL_M_SFT |
		pll_code.m_bp << RT5660_PLL_M_BP_SFT);

	rt5660->pll_in = freq_in;
	rt5660->pll_out = freq_out;
	rt5660->pll_src = source;

	return 0;
}

static int rt5660_set_bias_level(struct snd_soc_component *component,
			enum snd_soc_bias_level level)
{
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);
	int ret;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		snd_soc_component_update_bits(component, RT5660_GEN_CTRL1,
			RT5660_DIG_GATE_CTRL, RT5660_DIG_GATE_CTRL);

		if (IS_ERR(rt5660->mclk))
			break;

		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_ON) {
			clk_disable_unprepare(rt5660->mclk);
		} else {
			ret = clk_prepare_enable(rt5660->mclk);
			if (ret)
				return ret;
		}
		break;

	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			snd_soc_component_update_bits(component, RT5660_PWR_ANLG1,
				RT5660_PWR_VREF1 | RT5660_PWR_MB |
				RT5660_PWR_BG | RT5660_PWR_VREF2,
				RT5660_PWR_VREF1 | RT5660_PWR_MB |
				RT5660_PWR_BG | RT5660_PWR_VREF2);
			usleep_range(10000, 15000);
			snd_soc_component_update_bits(component, RT5660_PWR_ANLG1,
				RT5660_PWR_FV1 | RT5660_PWR_FV2,
				RT5660_PWR_FV1 | RT5660_PWR_FV2);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_update_bits(component, RT5660_GEN_CTRL1,
			RT5660_DIG_GATE_CTRL, 0);
		break;

	default:
		break;
	}

	return 0;
}

static int rt5660_probe(struct snd_soc_component *component)
{
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);

	rt5660->component = component;

	return 0;
}

static void rt5660_remove(struct snd_soc_component *component)
{
	snd_soc_component_write(component, RT5660_RESET, 0);
}

#ifdef CONFIG_PM
static int rt5660_suspend(struct snd_soc_component *component)
{
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt5660->regmap, true);
	regcache_mark_dirty(rt5660->regmap);

	return 0;
}

static int rt5660_resume(struct snd_soc_component *component)
{
	struct rt5660_priv *rt5660 = snd_soc_component_get_drvdata(component);

	if (rt5660->pdata.poweroff_codec_in_suspend)
		msleep(350);

	regcache_cache_only(rt5660->regmap, false);
	regcache_sync(rt5660->regmap);

	return 0;
}
#else
#define rt5660_suspend NULL
#define rt5660_resume NULL
#endif

#define RT5660_STEREO_RATES SNDRV_PCM_RATE_8000_192000
#define RT5660_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt5660_aif_dai_ops = {
	.hw_params = rt5660_hw_params,
	.set_fmt = rt5660_set_dai_fmt,
	.set_sysclk = rt5660_set_dai_sysclk,
	.set_pll = rt5660_set_dai_pll,
};

static struct snd_soc_dai_driver rt5660_dai[] = {
	{
		.name = "rt5660-aif1",
		.id = RT5660_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5660_STEREO_RATES,
			.formats = RT5660_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5660_STEREO_RATES,
			.formats = RT5660_FORMATS,
		},
		.ops = &rt5660_aif_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_component_dev_rt5660 = {
	.probe			= rt5660_probe,
	.remove			= rt5660_remove,
	.suspend		= rt5660_suspend,
	.resume			= rt5660_resume,
	.set_bias_level		= rt5660_set_bias_level,
	.controls		= rt5660_snd_controls,
	.num_controls		= ARRAY_SIZE(rt5660_snd_controls),
	.dapm_widgets		= rt5660_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rt5660_dapm_widgets),
	.dapm_routes		= rt5660_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rt5660_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config rt5660_regmap = {
	.reg_bits = 8,
	.val_bits = 16,
	.use_single_rw = true,

	.max_register = RT5660_VENDOR_ID2 + 1 + (ARRAY_SIZE(rt5660_ranges) *
					       RT5660_PR_SPACING),
	.volatile_reg = rt5660_volatile_register,
	.readable_reg = rt5660_readable_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5660_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5660_reg),
	.ranges = rt5660_ranges,
	.num_ranges = ARRAY_SIZE(rt5660_ranges),
};

static const struct i2c_device_id rt5660_i2c_id[] = {
	{ "rt5660", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5660_i2c_id);

static const struct of_device_id rt5660_of_match[] = {
	{ .compatible = "realtek,rt5660", },
	{},
};
MODULE_DEVICE_TABLE(of, rt5660_of_match);

static const struct acpi_device_id rt5660_acpi_match[] = {
	{ "10EC5660", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, rt5660_acpi_match);

static int rt5660_parse_dt(struct rt5660_priv *rt5660, struct device *dev)
{
	rt5660->pdata.in1_diff = device_property_read_bool(dev,
					"realtek,in1-differential");
	rt5660->pdata.in3_diff = device_property_read_bool(dev,
					"realtek,in3-differential");
	rt5660->pdata.poweroff_codec_in_suspend = device_property_read_bool(dev,
					"realtek,poweroff-in-suspend");
	device_property_read_u32(dev, "realtek,dmic1-data-pin",
		&rt5660->pdata.dmic1_data_pin);

	return 0;
}

static int rt5660_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5660_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt5660_priv *rt5660;
	int ret;
	unsigned int val;

	rt5660 = devm_kzalloc(&i2c->dev, sizeof(struct rt5660_priv),
		GFP_KERNEL);

	if (rt5660 == NULL)
		return -ENOMEM;

	/* Check if MCLK provided */
	rt5660->mclk = devm_clk_get(&i2c->dev, "mclk");
	if (PTR_ERR(rt5660->mclk) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	i2c_set_clientdata(i2c, rt5660);

	if (pdata)
		rt5660->pdata = *pdata;
	else if (i2c->dev.of_node)
		rt5660_parse_dt(rt5660, &i2c->dev);

	rt5660->regmap = devm_regmap_init_i2c(i2c, &rt5660_regmap);
	if (IS_ERR(rt5660->regmap)) {
		ret = PTR_ERR(rt5660->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	regmap_read(rt5660->regmap, RT5660_VENDOR_ID2, &val);
	if (val != RT5660_DEVICE_ID) {
		dev_err(&i2c->dev,
			"Device with ID register %#x is not rt5660\n", val);
		return -ENODEV;
	}

	regmap_write(rt5660->regmap, RT5660_RESET, 0);

	ret = regmap_register_patch(rt5660->regmap, rt5660_patch,
				    ARRAY_SIZE(rt5660_patch));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	regmap_update_bits(rt5660->regmap, RT5660_GEN_CTRL1,
		RT5660_AUTO_DIS_AMP | RT5660_MCLK_DET | RT5660_POW_CLKDET,
		RT5660_AUTO_DIS_AMP | RT5660_MCLK_DET | RT5660_POW_CLKDET);

	if (rt5660->pdata.dmic1_data_pin) {
		regmap_update_bits(rt5660->regmap, RT5660_GPIO_CTRL1,
			RT5660_GP1_PIN_MASK, RT5660_GP1_PIN_DMIC1_SCL);

		if (rt5660->pdata.dmic1_data_pin == RT5660_DMIC1_DATA_GPIO2)
			regmap_update_bits(rt5660->regmap, RT5660_DMIC_CTRL1,
				RT5660_SEL_DMIC_DATA_MASK,
				RT5660_SEL_DMIC_DATA_GPIO2);
		else if (rt5660->pdata.dmic1_data_pin == RT5660_DMIC1_DATA_IN1P)
			regmap_update_bits(rt5660->regmap, RT5660_DMIC_CTRL1,
				RT5660_SEL_DMIC_DATA_MASK,
				RT5660_SEL_DMIC_DATA_IN1P);
	}

	return devm_snd_soc_register_component(&i2c->dev,
				      &soc_component_dev_rt5660,
				      rt5660_dai, ARRAY_SIZE(rt5660_dai));
}

static struct i2c_driver rt5660_i2c_driver = {
	.driver = {
		.name = "rt5660",
		.acpi_match_table = ACPI_PTR(rt5660_acpi_match),
		.of_match_table = of_match_ptr(rt5660_of_match),
	},
	.probe = rt5660_i2c_probe,
	.id_table = rt5660_i2c_id,
};
module_i2c_driver(rt5660_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5660 driver");
MODULE_AUTHOR("Oder Chiou <oder_chiou@realtek.com>");
MODULE_LICENSE("GPL v2");
