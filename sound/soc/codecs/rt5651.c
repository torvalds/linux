// SPDX-License-Identifier: GPL-2.0-only
/*
 * rt5651.c  --  RT5651 ALSA SoC audio codec driver
 *
 * Copyright 2014 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
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
#include <sound/jack.h>

#include "rl6231.h"
#include "rt5651.h"

#define RT5651_DEVICE_ID_VALUE 0x6281

#define RT5651_PR_RANGE_BASE (0xff + 1)
#define RT5651_PR_SPACING 0x100

#define RT5651_PR_BASE (RT5651_PR_RANGE_BASE + (0 * RT5651_PR_SPACING))

static const struct regmap_range_cfg rt5651_ranges[] = {
	{ .name = "PR", .range_min = RT5651_PR_BASE,
	  .range_max = RT5651_PR_BASE + 0xb4,
	  .selector_reg = RT5651_PRIV_INDEX,
	  .selector_mask = 0xff,
	  .selector_shift = 0x0,
	  .window_start = RT5651_PRIV_DATA,
	  .window_len = 0x1, },
};

static const struct reg_sequence init_list[] = {
	{RT5651_PR_BASE + 0x3d,	0x3e00},
};

static const struct reg_default rt5651_reg[] = {
	{ 0x00, 0x0000 },
	{ 0x02, 0xc8c8 },
	{ 0x03, 0xc8c8 },
	{ 0x05, 0x0000 },
	{ 0x0d, 0x0000 },
	{ 0x0e, 0x0000 },
	{ 0x0f, 0x0808 },
	{ 0x10, 0x0808 },
	{ 0x19, 0xafaf },
	{ 0x1a, 0xafaf },
	{ 0x1b, 0x0c00 },
	{ 0x1c, 0x2f2f },
	{ 0x1d, 0x2f2f },
	{ 0x1e, 0x0000 },
	{ 0x27, 0x7860 },
	{ 0x28, 0x7070 },
	{ 0x29, 0x8080 },
	{ 0x2a, 0x5252 },
	{ 0x2b, 0x5454 },
	{ 0x2f, 0x0000 },
	{ 0x30, 0x5000 },
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
	{ 0x71, 0x8000 },
	{ 0x73, 0x1104 },
	{ 0x74, 0x0c00 },
	{ 0x75, 0x1400 },
	{ 0x77, 0x0c00 },
	{ 0x78, 0x4000 },
	{ 0x79, 0x0123 },
	{ 0x80, 0x0000 },
	{ 0x81, 0x0000 },
	{ 0x82, 0x0000 },
	{ 0x83, 0x0800 },
	{ 0x84, 0x0000 },
	{ 0x85, 0x0008 },
	{ 0x89, 0x0000 },
	{ 0x8e, 0x0004 },
	{ 0x8f, 0x1100 },
	{ 0x90, 0x0000 },
	{ 0x93, 0x2000 },
	{ 0x94, 0x0200 },
	{ 0xb0, 0x2080 },
	{ 0xb1, 0x0000 },
	{ 0xb4, 0x2206 },
	{ 0xb5, 0x1f00 },
	{ 0xb6, 0x0000 },
	{ 0xbb, 0x0000 },
	{ 0xbc, 0x0000 },
	{ 0xbd, 0x0000 },
	{ 0xbe, 0x0000 },
	{ 0xbf, 0x0000 },
	{ 0xc0, 0x0400 },
	{ 0xc1, 0x0000 },
	{ 0xc2, 0x0000 },
	{ 0xcf, 0x0013 },
	{ 0xd0, 0x0680 },
	{ 0xd1, 0x1c17 },
	{ 0xd3, 0xb320 },
	{ 0xd9, 0x0809 },
	{ 0xfa, 0x0010 },
	{ 0xfe, 0x10ec },
	{ 0xff, 0x6281 },
};

static bool rt5651_volatile_register(struct device *dev,  unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5651_ranges); i++) {
		if ((reg >= rt5651_ranges[i].window_start &&
		     reg <= rt5651_ranges[i].window_start +
		     rt5651_ranges[i].window_len) ||
		    (reg >= rt5651_ranges[i].range_min &&
		     reg <= rt5651_ranges[i].range_max)) {
			return true;
		}
	}

	switch (reg) {
	case RT5651_RESET:
	case RT5651_PRIV_DATA:
	case RT5651_EQ_CTRL1:
	case RT5651_ALC_1:
	case RT5651_IRQ_CTRL2:
	case RT5651_INT_IRQ_ST:
	case RT5651_PGM_REG_ARR1:
	case RT5651_PGM_REG_ARR3:
	case RT5651_VENDOR_ID:
	case RT5651_DEVICE_ID:
		return true;
	default:
		return false;
	}
}

static bool rt5651_readable_register(struct device *dev, unsigned int reg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rt5651_ranges); i++) {
		if ((reg >= rt5651_ranges[i].window_start &&
		     reg <= rt5651_ranges[i].window_start +
		     rt5651_ranges[i].window_len) ||
		    (reg >= rt5651_ranges[i].range_min &&
		     reg <= rt5651_ranges[i].range_max)) {
			return true;
		}
	}

	switch (reg) {
	case RT5651_RESET:
	case RT5651_VERSION_ID:
	case RT5651_VENDOR_ID:
	case RT5651_DEVICE_ID:
	case RT5651_HP_VOL:
	case RT5651_LOUT_CTRL1:
	case RT5651_LOUT_CTRL2:
	case RT5651_IN1_IN2:
	case RT5651_IN3:
	case RT5651_INL1_INR1_VOL:
	case RT5651_INL2_INR2_VOL:
	case RT5651_DAC1_DIG_VOL:
	case RT5651_DAC2_DIG_VOL:
	case RT5651_DAC2_CTRL:
	case RT5651_ADC_DIG_VOL:
	case RT5651_ADC_DATA:
	case RT5651_ADC_BST_VOL:
	case RT5651_STO1_ADC_MIXER:
	case RT5651_STO2_ADC_MIXER:
	case RT5651_AD_DA_MIXER:
	case RT5651_STO_DAC_MIXER:
	case RT5651_DD_MIXER:
	case RT5651_DIG_INF_DATA:
	case RT5651_PDM_CTL:
	case RT5651_REC_L1_MIXER:
	case RT5651_REC_L2_MIXER:
	case RT5651_REC_R1_MIXER:
	case RT5651_REC_R2_MIXER:
	case RT5651_HPO_MIXER:
	case RT5651_OUT_L1_MIXER:
	case RT5651_OUT_L2_MIXER:
	case RT5651_OUT_L3_MIXER:
	case RT5651_OUT_R1_MIXER:
	case RT5651_OUT_R2_MIXER:
	case RT5651_OUT_R3_MIXER:
	case RT5651_LOUT_MIXER:
	case RT5651_PWR_DIG1:
	case RT5651_PWR_DIG2:
	case RT5651_PWR_ANLG1:
	case RT5651_PWR_ANLG2:
	case RT5651_PWR_MIXER:
	case RT5651_PWR_VOL:
	case RT5651_PRIV_INDEX:
	case RT5651_PRIV_DATA:
	case RT5651_I2S1_SDP:
	case RT5651_I2S2_SDP:
	case RT5651_ADDA_CLK1:
	case RT5651_ADDA_CLK2:
	case RT5651_DMIC:
	case RT5651_TDM_CTL_1:
	case RT5651_TDM_CTL_2:
	case RT5651_TDM_CTL_3:
	case RT5651_GLB_CLK:
	case RT5651_PLL_CTRL1:
	case RT5651_PLL_CTRL2:
	case RT5651_PLL_MODE_1:
	case RT5651_PLL_MODE_2:
	case RT5651_PLL_MODE_3:
	case RT5651_PLL_MODE_4:
	case RT5651_PLL_MODE_5:
	case RT5651_PLL_MODE_6:
	case RT5651_PLL_MODE_7:
	case RT5651_DEPOP_M1:
	case RT5651_DEPOP_M2:
	case RT5651_DEPOP_M3:
	case RT5651_CHARGE_PUMP:
	case RT5651_MICBIAS:
	case RT5651_A_JD_CTL1:
	case RT5651_EQ_CTRL1:
	case RT5651_EQ_CTRL2:
	case RT5651_ALC_1:
	case RT5651_ALC_2:
	case RT5651_ALC_3:
	case RT5651_JD_CTRL1:
	case RT5651_JD_CTRL2:
	case RT5651_IRQ_CTRL1:
	case RT5651_IRQ_CTRL2:
	case RT5651_INT_IRQ_ST:
	case RT5651_GPIO_CTRL1:
	case RT5651_GPIO_CTRL2:
	case RT5651_GPIO_CTRL3:
	case RT5651_PGM_REG_ARR1:
	case RT5651_PGM_REG_ARR2:
	case RT5651_PGM_REG_ARR3:
	case RT5651_PGM_REG_ARR4:
	case RT5651_PGM_REG_ARR5:
	case RT5651_SCB_FUNC:
	case RT5651_SCB_CTRL:
	case RT5651_BASE_BACK:
	case RT5651_MP3_PLUS1:
	case RT5651_MP3_PLUS2:
	case RT5651_ADJ_HPF_CTRL1:
	case RT5651_ADJ_HPF_CTRL2:
	case RT5651_HP_CALIB_AMP_DET:
	case RT5651_HP_CALIB2:
	case RT5651_SV_ZCD1:
	case RT5651_SV_ZCD2:
	case RT5651_D_MISC:
	case RT5651_DUMMY2:
	case RT5651_DUMMY3:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -4650, 150, 0);
static const DECLARE_TLV_DB_MINMAX(dac_vol_tlv, -6562, 0);
static const DECLARE_TLV_DB_SCALE(in_vol_tlv, -3450, 150, 0);
static const DECLARE_TLV_DB_MINMAX(adc_vol_tlv, -1762, 3000);
static const DECLARE_TLV_DB_SCALE(adc_bst_tlv, 0, 1200, 0);

/* {0, +20, +24, +30, +35, +40, +44, +50, +52} dB */
static const DECLARE_TLV_DB_RANGE(bst_tlv,
	0, 0, TLV_DB_SCALE_ITEM(0, 0, 0),
	1, 1, TLV_DB_SCALE_ITEM(2000, 0, 0),
	2, 2, TLV_DB_SCALE_ITEM(2400, 0, 0),
	3, 5, TLV_DB_SCALE_ITEM(3000, 500, 0),
	6, 6, TLV_DB_SCALE_ITEM(4400, 0, 0),
	7, 7, TLV_DB_SCALE_ITEM(5000, 0, 0),
	8, 8, TLV_DB_SCALE_ITEM(5200, 0, 0)
);

/* Interface data select */
static const char * const rt5651_data_select[] = {
	"Normal", "Swap", "left copy to right", "right copy to left"};

static SOC_ENUM_SINGLE_DECL(rt5651_if2_dac_enum, RT5651_DIG_INF_DATA,
				RT5651_IF2_DAC_SEL_SFT, rt5651_data_select);

static SOC_ENUM_SINGLE_DECL(rt5651_if2_adc_enum, RT5651_DIG_INF_DATA,
				RT5651_IF2_ADC_SEL_SFT, rt5651_data_select);

static const struct snd_kcontrol_new rt5651_snd_controls[] = {
	/* Headphone Output Volume */
	SOC_DOUBLE_TLV("HP Playback Volume", RT5651_HP_VOL,
		RT5651_L_VOL_SFT, RT5651_R_VOL_SFT, 39, 1, out_vol_tlv),
	/* OUTPUT Control */
	SOC_DOUBLE_TLV("OUT Playback Volume", RT5651_LOUT_CTRL1,
		RT5651_L_VOL_SFT, RT5651_R_VOL_SFT, 39, 1, out_vol_tlv),

	/* DAC Digital Volume */
	SOC_DOUBLE("DAC2 Playback Switch", RT5651_DAC2_CTRL,
		RT5651_M_DAC_L2_VOL_SFT, RT5651_M_DAC_R2_VOL_SFT, 1, 1),
	SOC_DOUBLE_TLV("DAC1 Playback Volume", RT5651_DAC1_DIG_VOL,
			RT5651_L_VOL_SFT, RT5651_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	SOC_DOUBLE_TLV("Mono DAC Playback Volume", RT5651_DAC2_DIG_VOL,
			RT5651_L_VOL_SFT, RT5651_R_VOL_SFT,
			175, 0, dac_vol_tlv),
	/* IN1/IN2/IN3 Control */
	SOC_SINGLE_TLV("IN1 Boost", RT5651_IN1_IN2,
		RT5651_BST_SFT1, 8, 0, bst_tlv),
	SOC_SINGLE_TLV("IN2 Boost", RT5651_IN1_IN2,
		RT5651_BST_SFT2, 8, 0, bst_tlv),
	SOC_SINGLE_TLV("IN3 Boost", RT5651_IN3,
		RT5651_BST_SFT1, 8, 0, bst_tlv),
	/* INL/INR Volume Control */
	SOC_DOUBLE_TLV("IN Capture Volume", RT5651_INL1_INR1_VOL,
			RT5651_INL_VOL_SFT, RT5651_INR_VOL_SFT,
			31, 1, in_vol_tlv),
	/* ADC Digital Volume Control */
	SOC_DOUBLE("ADC Capture Switch", RT5651_ADC_DIG_VOL,
		RT5651_L_MUTE_SFT, RT5651_R_MUTE_SFT, 1, 1),
	SOC_DOUBLE_TLV("ADC Capture Volume", RT5651_ADC_DIG_VOL,
			RT5651_L_VOL_SFT, RT5651_R_VOL_SFT,
			127, 0, adc_vol_tlv),
	SOC_DOUBLE_TLV("Mono ADC Capture Volume", RT5651_ADC_DATA,
			RT5651_L_VOL_SFT, RT5651_R_VOL_SFT,
			127, 0, adc_vol_tlv),
	/* ADC Boost Volume Control */
	SOC_DOUBLE_TLV("ADC Boost Gain", RT5651_ADC_BST_VOL,
			RT5651_ADC_L_BST_SFT, RT5651_ADC_R_BST_SFT,
			3, 0, adc_bst_tlv),

	/* ASRC */
	SOC_SINGLE("IF1 ASRC Switch", RT5651_PLL_MODE_1,
		RT5651_STO1_T_SFT, 1, 0),
	SOC_SINGLE("IF2 ASRC Switch", RT5651_PLL_MODE_1,
		RT5651_STO2_T_SFT, 1, 0),
	SOC_SINGLE("DMIC ASRC Switch", RT5651_PLL_MODE_1,
		RT5651_DMIC_1_M_SFT, 1, 0),

	SOC_ENUM("ADC IF2 Data Switch", rt5651_if2_adc_enum),
	SOC_ENUM("DAC IF2 Data Switch", rt5651_if2_dac_enum),
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
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	int idx, rate;

	rate = rt5651->sysclk / rl6231_get_pre_div(rt5651->regmap,
		RT5651_ADDA_CLK1, RT5651_I2S_PD1_SFT);
	idx = rl6231_calc_dmic_clk(rate);
	if (idx < 0)
		dev_err(component->dev, "Failed to set DMIC clock\n");
	else
		snd_soc_component_update_bits(component, RT5651_DMIC, RT5651_DMIC_CLK_MASK,
					idx << RT5651_DMIC_CLK_SFT);

	return idx;
}

/* Digital Mixer */
static const struct snd_kcontrol_new rt5651_sto1_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5651_STO1_ADC_MIXER,
			RT5651_M_STO1_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5651_STO1_ADC_MIXER,
			RT5651_M_STO1_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_sto1_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5651_STO1_ADC_MIXER,
			RT5651_M_STO1_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5651_STO1_ADC_MIXER,
			RT5651_M_STO1_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_sto2_adc_l_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5651_STO2_ADC_MIXER,
			RT5651_M_STO2_ADC_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5651_STO2_ADC_MIXER,
			RT5651_M_STO2_ADC_L2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_sto2_adc_r_mix[] = {
	SOC_DAPM_SINGLE("ADC1 Switch", RT5651_STO2_ADC_MIXER,
			RT5651_M_STO2_ADC_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("ADC2 Switch", RT5651_STO2_ADC_MIXER,
			RT5651_M_STO2_ADC_R2_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_dac_l_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5651_AD_DA_MIXER,
			RT5651_M_ADCMIX_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT5651_AD_DA_MIXER,
			RT5651_M_IF1_DAC_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_dac_r_mix[] = {
	SOC_DAPM_SINGLE("Stereo ADC Switch", RT5651_AD_DA_MIXER,
			RT5651_M_ADCMIX_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INF1 Switch", RT5651_AD_DA_MIXER,
			RT5651_M_IF1_DAC_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_sto_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5651_STO_DAC_MIXER,
			RT5651_M_DAC_L1_MIXL_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5651_STO_DAC_MIXER,
			RT5651_M_DAC_L2_MIXL_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5651_STO_DAC_MIXER,
			RT5651_M_DAC_R1_MIXL_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_sto_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5651_STO_DAC_MIXER,
			RT5651_M_DAC_R1_MIXR_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5651_STO_DAC_MIXER,
			RT5651_M_DAC_R2_MIXR_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5651_STO_DAC_MIXER,
			RT5651_M_DAC_L1_MIXR_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_dd_dac_l_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5651_DD_MIXER,
			RT5651_M_STO_DD_L1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5651_DD_MIXER,
			RT5651_M_STO_DD_L2_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5651_DD_MIXER,
			RT5651_M_STO_DD_R2_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_dd_dac_r_mix[] = {
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5651_DD_MIXER,
			RT5651_M_STO_DD_R1_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R2 Switch", RT5651_DD_MIXER,
			RT5651_M_STO_DD_R2_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L2 Switch", RT5651_DD_MIXER,
			RT5651_M_STO_DD_L2_R_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt5651_rec_l_mix[] = {
	SOC_DAPM_SINGLE("INL1 Switch", RT5651_REC_L2_MIXER,
			RT5651_M_IN1_L_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5651_REC_L2_MIXER,
			RT5651_M_BST3_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5651_REC_L2_MIXER,
			RT5651_M_BST2_RM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5651_REC_L2_MIXER,
			RT5651_M_BST1_RM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_rec_r_mix[] = {
	SOC_DAPM_SINGLE("INR1 Switch", RT5651_REC_R2_MIXER,
			RT5651_M_IN1_R_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST3 Switch", RT5651_REC_R2_MIXER,
			RT5651_M_BST3_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5651_REC_R2_MIXER,
			RT5651_M_BST2_RM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5651_REC_R2_MIXER,
			RT5651_M_BST1_RM_R_SFT, 1, 1),
};

/* Analog Output Mixer */

static const struct snd_kcontrol_new rt5651_out_l_mix[] = {
	SOC_DAPM_SINGLE("BST1 Switch", RT5651_OUT_L3_MIXER,
			RT5651_M_BST1_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST2 Switch", RT5651_OUT_L3_MIXER,
			RT5651_M_BST2_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("INL1 Switch", RT5651_OUT_L3_MIXER,
			RT5651_M_IN1_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXL Switch", RT5651_OUT_L3_MIXER,
			RT5651_M_RM_L_OM_L_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5651_OUT_L3_MIXER,
			RT5651_M_DAC_L1_OM_L_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_out_r_mix[] = {
	SOC_DAPM_SINGLE("BST2 Switch", RT5651_OUT_R3_MIXER,
			RT5651_M_BST2_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("BST1 Switch", RT5651_OUT_R3_MIXER,
			RT5651_M_BST1_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("INR1 Switch", RT5651_OUT_R3_MIXER,
			RT5651_M_IN1_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("REC MIXR Switch", RT5651_OUT_R3_MIXER,
			RT5651_M_RM_R_OM_R_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5651_OUT_R3_MIXER,
			RT5651_M_DAC_R1_OM_R_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_hpo_mix[] = {
	SOC_DAPM_SINGLE("HPO MIX DAC1 Switch", RT5651_HPO_MIXER,
			RT5651_M_DAC1_HM_SFT, 1, 1),
	SOC_DAPM_SINGLE("HPO MIX HPVOL Switch", RT5651_HPO_MIXER,
			RT5651_M_HPVOL_HM_SFT, 1, 1),
};

static const struct snd_kcontrol_new rt5651_lout_mix[] = {
	SOC_DAPM_SINGLE("DAC L1 Switch", RT5651_LOUT_MIXER,
			RT5651_M_DAC_L1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("DAC R1 Switch", RT5651_LOUT_MIXER,
			RT5651_M_DAC_R1_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL L Switch", RT5651_LOUT_MIXER,
			RT5651_M_OV_L_LM_SFT, 1, 1),
	SOC_DAPM_SINGLE("OUTVOL R Switch", RT5651_LOUT_MIXER,
			RT5651_M_OV_R_LM_SFT, 1, 1),
};

static const struct snd_kcontrol_new outvol_l_control =
	SOC_DAPM_SINGLE("Switch", RT5651_LOUT_CTRL1,
			RT5651_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new outvol_r_control =
	SOC_DAPM_SINGLE("Switch", RT5651_LOUT_CTRL1,
			RT5651_VOL_R_SFT, 1, 1);

static const struct snd_kcontrol_new lout_l_mute_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5651_LOUT_CTRL1,
				    RT5651_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new lout_r_mute_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5651_LOUT_CTRL1,
				    RT5651_R_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hpovol_l_control =
	SOC_DAPM_SINGLE("Switch", RT5651_HP_VOL,
			RT5651_VOL_L_SFT, 1, 1);

static const struct snd_kcontrol_new hpovol_r_control =
	SOC_DAPM_SINGLE("Switch", RT5651_HP_VOL,
			RT5651_VOL_R_SFT, 1, 1);

static const struct snd_kcontrol_new hpo_l_mute_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5651_HP_VOL,
				    RT5651_L_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hpo_r_mute_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT5651_HP_VOL,
				    RT5651_R_MUTE_SFT, 1, 1);

/* Stereo ADC source */
static const char * const rt5651_stereo1_adc1_src[] = {"DD MIX", "ADC"};

static SOC_ENUM_SINGLE_DECL(
	rt5651_stereo1_adc1_enum, RT5651_STO1_ADC_MIXER,
	RT5651_STO1_ADC_1_SRC_SFT, rt5651_stereo1_adc1_src);

static const struct snd_kcontrol_new rt5651_sto1_adc_l1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC L1 source", rt5651_stereo1_adc1_enum);

static const struct snd_kcontrol_new rt5651_sto1_adc_r1_mux =
	SOC_DAPM_ENUM("Stereo1 ADC R1 source", rt5651_stereo1_adc1_enum);

static const char * const rt5651_stereo1_adc2_src[] = {"DMIC", "DD MIX"};

static SOC_ENUM_SINGLE_DECL(
	rt5651_stereo1_adc2_enum, RT5651_STO1_ADC_MIXER,
	RT5651_STO1_ADC_2_SRC_SFT, rt5651_stereo1_adc2_src);

static const struct snd_kcontrol_new rt5651_sto1_adc_l2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC L2 source", rt5651_stereo1_adc2_enum);

static const struct snd_kcontrol_new rt5651_sto1_adc_r2_mux =
	SOC_DAPM_ENUM("Stereo1 ADC R2 source", rt5651_stereo1_adc2_enum);

/* Mono ADC source */
static const char * const rt5651_sto2_adc_l1_src[] = {"DD MIXL", "ADCL"};

static SOC_ENUM_SINGLE_DECL(
	rt5651_sto2_adc_l1_enum, RT5651_STO1_ADC_MIXER,
	RT5651_STO2_ADC_L1_SRC_SFT, rt5651_sto2_adc_l1_src);

static const struct snd_kcontrol_new rt5651_sto2_adc_l1_mux =
	SOC_DAPM_ENUM("Stereo2 ADC1 left source", rt5651_sto2_adc_l1_enum);

static const char * const rt5651_sto2_adc_l2_src[] = {"DMIC L", "DD MIXL"};

static SOC_ENUM_SINGLE_DECL(
	rt5651_sto2_adc_l2_enum, RT5651_STO1_ADC_MIXER,
	RT5651_STO2_ADC_L2_SRC_SFT, rt5651_sto2_adc_l2_src);

static const struct snd_kcontrol_new rt5651_sto2_adc_l2_mux =
	SOC_DAPM_ENUM("Stereo2 ADC2 left source", rt5651_sto2_adc_l2_enum);

static const char * const rt5651_sto2_adc_r1_src[] = {"DD MIXR", "ADCR"};

static SOC_ENUM_SINGLE_DECL(
	rt5651_sto2_adc_r1_enum, RT5651_STO1_ADC_MIXER,
	RT5651_STO2_ADC_R1_SRC_SFT, rt5651_sto2_adc_r1_src);

static const struct snd_kcontrol_new rt5651_sto2_adc_r1_mux =
	SOC_DAPM_ENUM("Stereo2 ADC1 right source", rt5651_sto2_adc_r1_enum);

static const char * const rt5651_sto2_adc_r2_src[] = {"DMIC R", "DD MIXR"};

static SOC_ENUM_SINGLE_DECL(
	rt5651_sto2_adc_r2_enum, RT5651_STO1_ADC_MIXER,
	RT5651_STO2_ADC_R2_SRC_SFT, rt5651_sto2_adc_r2_src);

static const struct snd_kcontrol_new rt5651_sto2_adc_r2_mux =
	SOC_DAPM_ENUM("Stereo2 ADC2 right source", rt5651_sto2_adc_r2_enum);

/* DAC2 channel source */

static const char * const rt5651_dac_src[] = {"IF1", "IF2"};

static SOC_ENUM_SINGLE_DECL(rt5651_dac_l2_enum, RT5651_DAC2_CTRL,
				RT5651_SEL_DAC_L2_SFT, rt5651_dac_src);

static const struct snd_kcontrol_new rt5651_dac_l2_mux =
	SOC_DAPM_ENUM("DAC2 left channel source", rt5651_dac_l2_enum);

static SOC_ENUM_SINGLE_DECL(
	rt5651_dac_r2_enum, RT5651_DAC2_CTRL,
	RT5651_SEL_DAC_R2_SFT, rt5651_dac_src);

static const struct snd_kcontrol_new rt5651_dac_r2_mux =
	SOC_DAPM_ENUM("DAC2 right channel source", rt5651_dac_r2_enum);

/* IF2_ADC channel source */

static const char * const rt5651_adc_src[] = {"IF1 ADC1", "IF1 ADC2"};

static SOC_ENUM_SINGLE_DECL(rt5651_if2_adc_src_enum, RT5651_DIG_INF_DATA,
				RT5651_IF2_ADC_SRC_SFT, rt5651_adc_src);

static const struct snd_kcontrol_new rt5651_if2_adc_src_mux =
	SOC_DAPM_ENUM("IF2 ADC channel source", rt5651_if2_adc_src_enum);

/* PDM select */
static const char * const rt5651_pdm_sel[] = {"DD MIX", "Stereo DAC MIX"};

static SOC_ENUM_SINGLE_DECL(
	rt5651_pdm_l_sel_enum, RT5651_PDM_CTL,
	RT5651_PDM_L_SEL_SFT, rt5651_pdm_sel);

static SOC_ENUM_SINGLE_DECL(
	rt5651_pdm_r_sel_enum, RT5651_PDM_CTL,
	RT5651_PDM_R_SEL_SFT, rt5651_pdm_sel);

static const struct snd_kcontrol_new rt5651_pdm_l_mux =
	SOC_DAPM_ENUM("PDM L select", rt5651_pdm_l_sel_enum);

static const struct snd_kcontrol_new rt5651_pdm_r_mux =
	SOC_DAPM_ENUM("PDM R select", rt5651_pdm_r_sel_enum);

static int rt5651_amp_power_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* depop parameters */
		regmap_update_bits(rt5651->regmap, RT5651_PR_BASE +
			RT5651_CHPUMP_INT_REG1, 0x0700, 0x0200);
		regmap_update_bits(rt5651->regmap, RT5651_DEPOP_M2,
			RT5651_DEPOP_MASK, RT5651_DEPOP_MAN);
		regmap_update_bits(rt5651->regmap, RT5651_DEPOP_M1,
			RT5651_HP_CP_MASK | RT5651_HP_SG_MASK |
			RT5651_HP_CB_MASK, RT5651_HP_CP_PU |
			RT5651_HP_SG_DIS | RT5651_HP_CB_PU);
		regmap_write(rt5651->regmap, RT5651_PR_BASE +
				RT5651_HP_DCC_INT1, 0x9f00);
		/* headphone amp power on */
		regmap_update_bits(rt5651->regmap, RT5651_PWR_ANLG1,
			RT5651_PWR_FV1 | RT5651_PWR_FV2, 0);
		regmap_update_bits(rt5651->regmap, RT5651_PWR_ANLG1,
			RT5651_PWR_HA,
			RT5651_PWR_HA);
		usleep_range(10000, 15000);
		regmap_update_bits(rt5651->regmap, RT5651_PWR_ANLG1,
			RT5651_PWR_FV1 | RT5651_PWR_FV2 ,
			RT5651_PWR_FV1 | RT5651_PWR_FV2);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5651_hp_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* headphone unmute sequence */
		regmap_update_bits(rt5651->regmap, RT5651_DEPOP_M2,
			RT5651_DEPOP_MASK | RT5651_DIG_DP_MASK,
			RT5651_DEPOP_AUTO | RT5651_DIG_DP_EN);
		regmap_update_bits(rt5651->regmap, RT5651_CHARGE_PUMP,
			RT5651_PM_HP_MASK, RT5651_PM_HP_HV);

		regmap_update_bits(rt5651->regmap, RT5651_DEPOP_M3,
			RT5651_CP_FQ1_MASK | RT5651_CP_FQ2_MASK |
			RT5651_CP_FQ3_MASK,
			(RT5651_CP_FQ_192_KHZ << RT5651_CP_FQ1_SFT) |
			(RT5651_CP_FQ_12_KHZ << RT5651_CP_FQ2_SFT) |
			(RT5651_CP_FQ_192_KHZ << RT5651_CP_FQ3_SFT));

		regmap_write(rt5651->regmap, RT5651_PR_BASE +
			RT5651_MAMP_INT_REG2, 0x1c00);
		regmap_update_bits(rt5651->regmap, RT5651_DEPOP_M1,
			RT5651_HP_CP_MASK | RT5651_HP_SG_MASK,
			RT5651_HP_CP_PD | RT5651_HP_SG_EN);
		regmap_update_bits(rt5651->regmap, RT5651_PR_BASE +
			RT5651_CHPUMP_INT_REG1, 0x0700, 0x0400);
		rt5651->hp_mute = false;
		break;

	case SND_SOC_DAPM_PRE_PMD:
		rt5651->hp_mute = true;
		usleep_range(70000, 75000);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5651_hp_post_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{

	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (!rt5651->hp_mute)
			usleep_range(80000, 85000);

		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5651_bst1_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
			RT5651_PWR_BST1_OP2, RT5651_PWR_BST1_OP2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
			RT5651_PWR_BST1_OP2, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5651_bst2_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
			RT5651_PWR_BST2_OP2, RT5651_PWR_BST2_OP2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
			RT5651_PWR_BST2_OP2, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt5651_bst3_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
			RT5651_PWR_BST3_OP2, RT5651_PWR_BST3_OP2);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
			RT5651_PWR_BST3_OP2, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt5651_dapm_widgets[] = {
	/* ASRC */
	SND_SOC_DAPM_SUPPLY_S("I2S1 ASRC", 1, RT5651_PLL_MODE_2,
			      15, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S2 ASRC", 1, RT5651_PLL_MODE_2,
			      14, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("STO1 DAC ASRC", 1, RT5651_PLL_MODE_2,
			      13, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("STO2 DAC ASRC", 1, RT5651_PLL_MODE_2,
			      12, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADC ASRC", 1, RT5651_PLL_MODE_2,
			      11, 0, NULL, 0),

	/* micbias */
	SND_SOC_DAPM_SUPPLY("LDO", RT5651_PWR_ANLG1,
			RT5651_PWR_LDO_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("micbias1", RT5651_PWR_ANLG2,
			RT5651_PWR_MB1_BIT, 0, NULL, 0),
	/* Input Lines */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("MIC3"),

	SND_SOC_DAPM_INPUT("IN1P"),
	SND_SOC_DAPM_INPUT("IN2P"),
	SND_SOC_DAPM_INPUT("IN2N"),
	SND_SOC_DAPM_INPUT("IN3P"),
	SND_SOC_DAPM_INPUT("DMIC L1"),
	SND_SOC_DAPM_INPUT("DMIC R1"),
	SND_SOC_DAPM_SUPPLY("DMIC CLK", RT5651_DMIC, RT5651_DMIC_1_EN_SFT,
			    0, set_dmic_clk, SND_SOC_DAPM_PRE_PMU),
	/* Boost */
	SND_SOC_DAPM_PGA_E("BST1", RT5651_PWR_ANLG2,
		RT5651_PWR_BST1_BIT, 0, NULL, 0, rt5651_bst1_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("BST2", RT5651_PWR_ANLG2,
		RT5651_PWR_BST2_BIT, 0, NULL, 0, rt5651_bst2_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("BST3", RT5651_PWR_ANLG2,
		RT5651_PWR_BST3_BIT, 0, NULL, 0, rt5651_bst3_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	/* Input Volume */
	SND_SOC_DAPM_PGA("INL1 VOL", RT5651_PWR_VOL,
			 RT5651_PWR_IN1_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR1 VOL", RT5651_PWR_VOL,
			 RT5651_PWR_IN1_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INL2 VOL", RT5651_PWR_VOL,
			 RT5651_PWR_IN2_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR2 VOL", RT5651_PWR_VOL,
			 RT5651_PWR_IN2_R_BIT, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIXL", RT5651_PWR_MIXER, RT5651_PWR_RM_L_BIT, 0,
			   rt5651_rec_l_mix, ARRAY_SIZE(rt5651_rec_l_mix)),
	SND_SOC_DAPM_MIXER("RECMIXR", RT5651_PWR_MIXER, RT5651_PWR_RM_R_BIT, 0,
			   rt5651_rec_r_mix, ARRAY_SIZE(rt5651_rec_r_mix)),
	/* ADCs */
	SND_SOC_DAPM_ADC("ADC L", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC R", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("ADC L Power", RT5651_PWR_DIG1,
			    RT5651_PWR_ADC_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ADC R Power", RT5651_PWR_DIG1,
			    RT5651_PWR_ADC_R_BIT, 0, NULL, 0),
	/* ADC Mux */
	SND_SOC_DAPM_MUX("Stereo1 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto1_adc_l2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto1_adc_r2_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto1_adc_l1_mux),
	SND_SOC_DAPM_MUX("Stereo1 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto1_adc_r1_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC L2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto2_adc_l2_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC L1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto2_adc_l1_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC R1 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto2_adc_r1_mux),
	SND_SOC_DAPM_MUX("Stereo2 ADC R2 Mux", SND_SOC_NOPM, 0, 0,
			 &rt5651_sto2_adc_r2_mux),
	/* ADC Mixer */
	SND_SOC_DAPM_SUPPLY("Stereo1 Filter", RT5651_PWR_DIG2,
			    RT5651_PWR_ADC_STO1_F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Stereo2 Filter", RT5651_PWR_DIG2,
			    RT5651_PWR_ADC_STO2_F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5651_sto1_adc_l_mix,
			   ARRAY_SIZE(rt5651_sto1_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo1 ADC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5651_sto1_adc_r_mix,
			   ARRAY_SIZE(rt5651_sto1_adc_r_mix)),
	SND_SOC_DAPM_MIXER("Stereo2 ADC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5651_sto2_adc_l_mix,
			   ARRAY_SIZE(rt5651_sto2_adc_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo2 ADC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5651_sto2_adc_r_mix,
			   ARRAY_SIZE(rt5651_sto2_adc_r_mix)),

	/* Digital Interface */
	SND_SOC_DAPM_SUPPLY("I2S1", RT5651_PWR_DIG1,
			    RT5651_PWR_I2S1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC1 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC1", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 DAC2 R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF1 ADC2", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("I2S2", RT5651_PWR_DIG1,
			    RT5651_PWR_I2S2_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("IF2 DAC R", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MUX("IF2 ADC", SND_SOC_NOPM, 0, 0,
			 &rt5651_if2_adc_src_mux),

	/* Digital Interface Select */

	SND_SOC_DAPM_MUX("PDM L Mux", RT5651_PDM_CTL,
			 RT5651_M_PDM_L_SFT, 1, &rt5651_pdm_l_mux),
	SND_SOC_DAPM_MUX("PDM R Mux", RT5651_PDM_CTL,
			 RT5651_M_PDM_R_SFT, 1, &rt5651_pdm_r_mux),
	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Audio DSP */
	SND_SOC_DAPM_PGA("Audio DSP", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Output Side */
	/* DAC mixer before sound effect  */
	SND_SOC_DAPM_MIXER("DAC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5651_dac_l_mix, ARRAY_SIZE(rt5651_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DAC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5651_dac_r_mix, ARRAY_SIZE(rt5651_dac_r_mix)),

	/* DAC2 channel Mux */
	SND_SOC_DAPM_MUX("DAC L2 Mux", SND_SOC_NOPM, 0, 0, &rt5651_dac_l2_mux),
	SND_SOC_DAPM_MUX("DAC R2 Mux", SND_SOC_NOPM, 0, 0, &rt5651_dac_r2_mux),
	SND_SOC_DAPM_PGA("DAC L2 Volume", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_PGA("DAC R2 Volume", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Stero1 DAC Power", RT5651_PWR_DIG2,
			    RT5651_PWR_DAC_STO1_F_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Stero2 DAC Power", RT5651_PWR_DIG2,
			    RT5651_PWR_DAC_STO2_F_BIT, 0, NULL, 0),
	/* DAC Mixer */
	SND_SOC_DAPM_MIXER("Stereo DAC MIXL", SND_SOC_NOPM, 0, 0,
			   rt5651_sto_dac_l_mix,
			   ARRAY_SIZE(rt5651_sto_dac_l_mix)),
	SND_SOC_DAPM_MIXER("Stereo DAC MIXR", SND_SOC_NOPM, 0, 0,
			   rt5651_sto_dac_r_mix,
			   ARRAY_SIZE(rt5651_sto_dac_r_mix)),
	SND_SOC_DAPM_MIXER("DD MIXL", SND_SOC_NOPM, 0, 0,
			   rt5651_dd_dac_l_mix,
			   ARRAY_SIZE(rt5651_dd_dac_l_mix)),
	SND_SOC_DAPM_MIXER("DD MIXR", SND_SOC_NOPM, 0, 0,
			   rt5651_dd_dac_r_mix,
			   ARRAY_SIZE(rt5651_dd_dac_r_mix)),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC L1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC R1", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_SUPPLY("DAC L1 Power", RT5651_PWR_DIG1,
			    RT5651_PWR_DAC_L1_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC R1 Power", RT5651_PWR_DIG1,
			    RT5651_PWR_DAC_R1_BIT, 0, NULL, 0),
	/* OUT Mixer */
	SND_SOC_DAPM_MIXER("OUT MIXL", RT5651_PWR_MIXER, RT5651_PWR_OM_L_BIT,
			   0, rt5651_out_l_mix, ARRAY_SIZE(rt5651_out_l_mix)),
	SND_SOC_DAPM_MIXER("OUT MIXR", RT5651_PWR_MIXER, RT5651_PWR_OM_R_BIT,
			   0, rt5651_out_r_mix, ARRAY_SIZE(rt5651_out_r_mix)),
	/* Ouput Volume */
	SND_SOC_DAPM_SWITCH("OUTVOL L", RT5651_PWR_VOL,
			    RT5651_PWR_OV_L_BIT, 0, &outvol_l_control),
	SND_SOC_DAPM_SWITCH("OUTVOL R", RT5651_PWR_VOL,
			    RT5651_PWR_OV_R_BIT, 0, &outvol_r_control),
	SND_SOC_DAPM_SWITCH("HPOVOL L", RT5651_PWR_VOL,
			    RT5651_PWR_HV_L_BIT, 0, &hpovol_l_control),
	SND_SOC_DAPM_SWITCH("HPOVOL R", RT5651_PWR_VOL,
			    RT5651_PWR_HV_R_BIT, 0, &hpovol_r_control),
	SND_SOC_DAPM_PGA("INL1", RT5651_PWR_VOL,
			 RT5651_PWR_IN1_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR1", RT5651_PWR_VOL,
			 RT5651_PWR_IN1_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INL2", RT5651_PWR_VOL,
			 RT5651_PWR_IN2_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_PGA("INR2", RT5651_PWR_VOL,
			 RT5651_PWR_IN2_R_BIT, 0, NULL, 0),
	/* HPO/LOUT/Mono Mixer */
	SND_SOC_DAPM_MIXER("HPOL MIX", SND_SOC_NOPM, 0, 0,
			   rt5651_hpo_mix, ARRAY_SIZE(rt5651_hpo_mix)),
	SND_SOC_DAPM_MIXER("HPOR MIX", SND_SOC_NOPM, 0, 0,
			   rt5651_hpo_mix, ARRAY_SIZE(rt5651_hpo_mix)),
	SND_SOC_DAPM_SUPPLY("HP L Amp", RT5651_PWR_ANLG1,
			    RT5651_PWR_HP_L_BIT, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("HP R Amp", RT5651_PWR_ANLG1,
			    RT5651_PWR_HP_R_BIT, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("LOUT MIX", RT5651_PWR_ANLG1, RT5651_PWR_LM_BIT, 0,
			   rt5651_lout_mix, ARRAY_SIZE(rt5651_lout_mix)),

	SND_SOC_DAPM_SUPPLY("Amp Power", RT5651_PWR_ANLG1,
			    RT5651_PWR_HA_BIT, 0, rt5651_amp_power_event,
			    SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_S("HP Amp", 1, SND_SOC_NOPM, 0, 0, rt5651_hp_event,
			   SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SWITCH("HPO L Playback", SND_SOC_NOPM, 0, 0,
			    &hpo_l_mute_control),
	SND_SOC_DAPM_SWITCH("HPO R Playback", SND_SOC_NOPM, 0, 0,
			    &hpo_r_mute_control),
	SND_SOC_DAPM_SWITCH("LOUT L Playback", SND_SOC_NOPM, 0, 0,
			    &lout_l_mute_control),
	SND_SOC_DAPM_SWITCH("LOUT R Playback", SND_SOC_NOPM, 0, 0,
			    &lout_r_mute_control),
	SND_SOC_DAPM_POST("HP Post", rt5651_hp_post_event),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("HPOL"),
	SND_SOC_DAPM_OUTPUT("HPOR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
	SND_SOC_DAPM_OUTPUT("PDML"),
	SND_SOC_DAPM_OUTPUT("PDMR"),
};

static const struct snd_soc_dapm_route rt5651_dapm_routes[] = {
	{"Stero1 DAC Power", NULL, "STO1 DAC ASRC"},
	{"Stero2 DAC Power", NULL, "STO2 DAC ASRC"},
	{"I2S1", NULL, "I2S1 ASRC"},
	{"I2S2", NULL, "I2S2 ASRC"},

	{"IN1P", NULL, "LDO"},
	{"IN2P", NULL, "LDO"},
	{"IN3P", NULL, "LDO"},

	{"IN1P", NULL, "MIC1"},
	{"IN2P", NULL, "MIC2"},
	{"IN2N", NULL, "MIC2"},
	{"IN3P", NULL, "MIC3"},

	{"BST1", NULL, "IN1P"},
	{"BST2", NULL, "IN2P"},
	{"BST2", NULL, "IN2N"},
	{"BST3", NULL, "IN3P"},

	{"INL1 VOL", NULL, "IN2P"},
	{"INR1 VOL", NULL, "IN2N"},

	{"RECMIXL", "INL1 Switch", "INL1 VOL"},
	{"RECMIXL", "BST3 Switch", "BST3"},
	{"RECMIXL", "BST2 Switch", "BST2"},
	{"RECMIXL", "BST1 Switch", "BST1"},

	{"RECMIXR", "INR1 Switch", "INR1 VOL"},
	{"RECMIXR", "BST3 Switch", "BST3"},
	{"RECMIXR", "BST2 Switch", "BST2"},
	{"RECMIXR", "BST1 Switch", "BST1"},

	{"ADC L", NULL, "RECMIXL"},
	{"ADC L", NULL, "ADC L Power"},
	{"ADC R", NULL, "RECMIXR"},
	{"ADC R", NULL, "ADC R Power"},

	{"DMIC L1", NULL, "DMIC CLK"},
	{"DMIC R1", NULL, "DMIC CLK"},

	{"Stereo1 ADC L2 Mux", "DMIC", "DMIC L1"},
	{"Stereo1 ADC L2 Mux", "DD MIX", "DD MIXL"},
	{"Stereo1 ADC L1 Mux", "ADC", "ADC L"},
	{"Stereo1 ADC L1 Mux", "DD MIX", "DD MIXL"},

	{"Stereo1 ADC R1 Mux", "ADC", "ADC R"},
	{"Stereo1 ADC R1 Mux", "DD MIX", "DD MIXR"},
	{"Stereo1 ADC R2 Mux", "DMIC", "DMIC R1"},
	{"Stereo1 ADC R2 Mux", "DD MIX", "DD MIXR"},

	{"Stereo2 ADC L2 Mux", "DMIC L", "DMIC L1"},
	{"Stereo2 ADC L2 Mux", "DD MIXL", "DD MIXL"},
	{"Stereo2 ADC L1 Mux", "DD MIXL", "DD MIXL"},
	{"Stereo2 ADC L1 Mux", "ADCL", "ADC L"},

	{"Stereo2 ADC R1 Mux", "DD MIXR", "DD MIXR"},
	{"Stereo2 ADC R1 Mux", "ADCR", "ADC R"},
	{"Stereo2 ADC R2 Mux", "DMIC R", "DMIC R1"},
	{"Stereo2 ADC R2 Mux", "DD MIXR", "DD MIXR"},

	{"Stereo1 ADC MIXL", "ADC1 Switch", "Stereo1 ADC L1 Mux"},
	{"Stereo1 ADC MIXL", "ADC2 Switch", "Stereo1 ADC L2 Mux"},
	{"Stereo1 ADC MIXL", NULL, "Stereo1 Filter"},
	{"Stereo1 Filter", NULL, "ADC ASRC"},

	{"Stereo1 ADC MIXR", "ADC1 Switch", "Stereo1 ADC R1 Mux"},
	{"Stereo1 ADC MIXR", "ADC2 Switch", "Stereo1 ADC R2 Mux"},
	{"Stereo1 ADC MIXR", NULL, "Stereo1 Filter"},

	{"Stereo2 ADC MIXL", "ADC1 Switch", "Stereo2 ADC L1 Mux"},
	{"Stereo2 ADC MIXL", "ADC2 Switch", "Stereo2 ADC L2 Mux"},
	{"Stereo2 ADC MIXL", NULL, "Stereo2 Filter"},
	{"Stereo2 Filter", NULL, "ADC ASRC"},

	{"Stereo2 ADC MIXR", "ADC1 Switch", "Stereo2 ADC R1 Mux"},
	{"Stereo2 ADC MIXR", "ADC2 Switch", "Stereo2 ADC R2 Mux"},
	{"Stereo2 ADC MIXR", NULL, "Stereo2 Filter"},

	{"IF1 ADC2", NULL, "Stereo2 ADC MIXL"},
	{"IF1 ADC2", NULL, "Stereo2 ADC MIXR"},
	{"IF1 ADC1", NULL, "Stereo1 ADC MIXL"},
	{"IF1 ADC1", NULL, "Stereo1 ADC MIXR"},

	{"IF1 ADC1", NULL, "I2S1"},

	{"IF2 ADC", "IF1 ADC1", "IF1 ADC1"},
	{"IF2 ADC", "IF1 ADC2", "IF1 ADC2"},
	{"IF2 ADC", NULL, "I2S2"},

	{"AIF1TX", NULL, "IF1 ADC1"},
	{"AIF1TX", NULL, "IF1 ADC2"},
	{"AIF2TX", NULL, "IF2 ADC"},

	{"IF1 DAC", NULL, "AIF1RX"},
	{"IF1 DAC", NULL, "I2S1"},
	{"IF2 DAC", NULL, "AIF2RX"},
	{"IF2 DAC", NULL, "I2S2"},

	{"IF1 DAC1 L", NULL, "IF1 DAC"},
	{"IF1 DAC1 R", NULL, "IF1 DAC"},
	{"IF1 DAC2 L", NULL, "IF1 DAC"},
	{"IF1 DAC2 R", NULL, "IF1 DAC"},
	{"IF2 DAC L", NULL, "IF2 DAC"},
	{"IF2 DAC R", NULL, "IF2 DAC"},

	{"DAC MIXL", "Stereo ADC Switch", "Stereo1 ADC MIXL"},
	{"DAC MIXL", "INF1 Switch", "IF1 DAC1 L"},
	{"DAC MIXR", "Stereo ADC Switch", "Stereo1 ADC MIXR"},
	{"DAC MIXR", "INF1 Switch", "IF1 DAC1 R"},

	{"Audio DSP", NULL, "DAC MIXL"},
	{"Audio DSP", NULL, "DAC MIXR"},

	{"DAC L2 Mux", "IF1", "IF1 DAC2 L"},
	{"DAC L2 Mux", "IF2", "IF2 DAC L"},
	{"DAC L2 Volume", NULL, "DAC L2 Mux"},

	{"DAC R2 Mux", "IF1", "IF1 DAC2 R"},
	{"DAC R2 Mux", "IF2", "IF2 DAC R"},
	{"DAC R2 Volume", NULL, "DAC R2 Mux"},

	{"Stereo DAC MIXL", "DAC L1 Switch", "Audio DSP"},
	{"Stereo DAC MIXL", "DAC L2 Switch", "DAC L2 Volume"},
	{"Stereo DAC MIXL", "DAC R1 Switch", "DAC MIXR"},
	{"Stereo DAC MIXL", NULL, "Stero1 DAC Power"},
	{"Stereo DAC MIXL", NULL, "Stero2 DAC Power"},
	{"Stereo DAC MIXR", "DAC R1 Switch", "Audio DSP"},
	{"Stereo DAC MIXR", "DAC R2 Switch", "DAC R2 Volume"},
	{"Stereo DAC MIXR", "DAC L1 Switch", "DAC MIXL"},
	{"Stereo DAC MIXR", NULL, "Stero1 DAC Power"},
	{"Stereo DAC MIXR", NULL, "Stero2 DAC Power"},

	{"PDM L Mux", "Stereo DAC MIX", "Stereo DAC MIXL"},
	{"PDM L Mux", "DD MIX", "DAC MIXL"},
	{"PDM R Mux", "Stereo DAC MIX", "Stereo DAC MIXR"},
	{"PDM R Mux", "DD MIX", "DAC MIXR"},

	{"DAC L1", NULL, "Stereo DAC MIXL"},
	{"DAC L1", NULL, "DAC L1 Power"},
	{"DAC R1", NULL, "Stereo DAC MIXR"},
	{"DAC R1", NULL, "DAC R1 Power"},

	{"DD MIXL", "DAC L1 Switch", "DAC MIXL"},
	{"DD MIXL", "DAC L2 Switch", "DAC L2 Volume"},
	{"DD MIXL", "DAC R2 Switch", "DAC R2 Volume"},
	{"DD MIXL", NULL, "Stero2 DAC Power"},

	{"DD MIXR", "DAC R1 Switch", "DAC MIXR"},
	{"DD MIXR", "DAC R2 Switch", "DAC R2 Volume"},
	{"DD MIXR", "DAC L2 Switch", "DAC L2 Volume"},
	{"DD MIXR", NULL, "Stero2 DAC Power"},

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

	{"HPOVOL L", "Switch", "OUT MIXL"},
	{"HPOVOL R", "Switch", "OUT MIXR"},
	{"OUTVOL L", "Switch", "OUT MIXL"},
	{"OUTVOL R", "Switch", "OUT MIXR"},

	{"HPOL MIX", "HPO MIX DAC1 Switch", "DAC L1"},
	{"HPOL MIX", "HPO MIX HPVOL Switch", "HPOVOL L"},
	{"HPOL MIX", NULL, "HP L Amp"},
	{"HPOR MIX", "HPO MIX DAC1 Switch", "DAC R1"},
	{"HPOR MIX", "HPO MIX HPVOL Switch", "HPOVOL R"},
	{"HPOR MIX", NULL, "HP R Amp"},

	{"LOUT MIX", "DAC L1 Switch", "DAC L1"},
	{"LOUT MIX", "DAC R1 Switch", "DAC R1"},
	{"LOUT MIX", "OUTVOL L Switch", "OUTVOL L"},
	{"LOUT MIX", "OUTVOL R Switch", "OUTVOL R"},

	{"HP Amp", NULL, "HPOL MIX"},
	{"HP Amp", NULL, "HPOR MIX"},
	{"HP Amp", NULL, "Amp Power"},
	{"HPO L Playback", "Switch", "HP Amp"},
	{"HPO R Playback", "Switch", "HP Amp"},
	{"HPOL", NULL, "HPO L Playback"},
	{"HPOR", NULL, "HPO R Playback"},

	{"LOUT L Playback", "Switch", "LOUT MIX"},
	{"LOUT R Playback", "Switch", "LOUT MIX"},
	{"LOUTL", NULL, "LOUT L Playback"},
	{"LOUTL", NULL, "Amp Power"},
	{"LOUTR", NULL, "LOUT R Playback"},
	{"LOUTR", NULL, "Amp Power"},

	{"PDML", NULL, "PDM L Mux"},
	{"PDMR", NULL, "PDM R Mux"},
};

static int rt5651_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	unsigned int val_len = 0, val_clk, mask_clk;
	int pre_div, bclk_ms, frame_size;

	rt5651->lrck[dai->id] = params_rate(params);
	pre_div = rl6231_get_clk_info(rt5651->sysclk, rt5651->lrck[dai->id]);

	if (pre_div < 0) {
		dev_err(component->dev, "Unsupported clock setting\n");
		return -EINVAL;
	}
	frame_size = snd_soc_params_to_frame_size(params);
	if (frame_size < 0) {
		dev_err(component->dev, "Unsupported frame size: %d\n", frame_size);
		return -EINVAL;
	}
	bclk_ms = frame_size > 32 ? 1 : 0;
	rt5651->bclk[dai->id] = rt5651->lrck[dai->id] * (32 << bclk_ms);

	dev_dbg(dai->dev, "bclk is %dHz and lrck is %dHz\n",
		rt5651->bclk[dai->id], rt5651->lrck[dai->id]);
	dev_dbg(dai->dev, "bclk_ms is %d and pre_div is %d for iis %d\n",
				bclk_ms, pre_div, dai->id);

	switch (params_width(params)) {
	case 16:
		break;
	case 20:
		val_len |= RT5651_I2S_DL_20;
		break;
	case 24:
		val_len |= RT5651_I2S_DL_24;
		break;
	case 8:
		val_len |= RT5651_I2S_DL_8;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5651_AIF1:
		mask_clk = RT5651_I2S_PD1_MASK;
		val_clk = pre_div << RT5651_I2S_PD1_SFT;
		snd_soc_component_update_bits(component, RT5651_I2S1_SDP,
			RT5651_I2S_DL_MASK, val_len);
		snd_soc_component_update_bits(component, RT5651_ADDA_CLK1, mask_clk, val_clk);
		break;
	case RT5651_AIF2:
		mask_clk = RT5651_I2S_BCLK_MS2_MASK | RT5651_I2S_PD2_MASK;
		val_clk = pre_div << RT5651_I2S_PD2_SFT;
		snd_soc_component_update_bits(component, RT5651_I2S2_SDP,
			RT5651_I2S_DL_MASK, val_len);
		snd_soc_component_update_bits(component, RT5651_ADDA_CLK1, mask_clk, val_clk);
		break;
	default:
		dev_err(component->dev, "Wrong dai->id: %d\n", dai->id);
		return -EINVAL;
	}

	return 0;
}

static int rt5651_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt5651->master[dai->id] = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		reg_val |= RT5651_I2S_MS_S;
		rt5651->master[dai->id] = 0;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		reg_val |= RT5651_I2S_BP_INV;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		reg_val |= RT5651_I2S_DF_LEFT;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		reg_val |= RT5651_I2S_DF_PCM_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		reg_val |= RT5651_I2S_DF_PCM_B;
		break;
	default:
		return -EINVAL;
	}

	switch (dai->id) {
	case RT5651_AIF1:
		snd_soc_component_update_bits(component, RT5651_I2S1_SDP,
			RT5651_I2S_MS_MASK | RT5651_I2S_BP_MASK |
			RT5651_I2S_DF_MASK, reg_val);
		break;
	case RT5651_AIF2:
		snd_soc_component_update_bits(component, RT5651_I2S2_SDP,
			RT5651_I2S_MS_MASK | RT5651_I2S_BP_MASK |
			RT5651_I2S_DF_MASK, reg_val);
		break;
	default:
		dev_err(component->dev, "Wrong dai->id: %d\n", dai->id);
		return -EINVAL;
	}
	return 0;
}

static int rt5651_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	unsigned int reg_val = 0;
	unsigned int pll_bit = 0;

	if (freq == rt5651->sysclk && clk_id == rt5651->sysclk_src)
		return 0;

	switch (clk_id) {
	case RT5651_SCLK_S_MCLK:
		reg_val |= RT5651_SCLK_SRC_MCLK;
		break;
	case RT5651_SCLK_S_PLL1:
		reg_val |= RT5651_SCLK_SRC_PLL1;
		pll_bit |= RT5651_PWR_PLL;
		break;
	case RT5651_SCLK_S_RCCLK:
		reg_val |= RT5651_SCLK_SRC_RCCLK;
		break;
	default:
		dev_err(component->dev, "Invalid clock id (%d)\n", clk_id);
		return -EINVAL;
	}
	snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
		RT5651_PWR_PLL, pll_bit);
	snd_soc_component_update_bits(component, RT5651_GLB_CLK,
		RT5651_SCLK_SRC_MASK, reg_val);
	rt5651->sysclk = freq;
	rt5651->sysclk_src = clk_id;

	dev_dbg(dai->dev, "Sysclk is %dHz and clock id is %d\n", freq, clk_id);

	return 0;
}

static int rt5651_set_dai_pll(struct snd_soc_dai *dai, int pll_id, int source,
			unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_component *component = dai->component;
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	struct rl6231_pll_code pll_code;
	int ret;

	if (source == rt5651->pll_src && freq_in == rt5651->pll_in &&
	    freq_out == rt5651->pll_out)
		return 0;

	if (!freq_in || !freq_out) {
		dev_dbg(component->dev, "PLL disabled\n");

		rt5651->pll_in = 0;
		rt5651->pll_out = 0;
		snd_soc_component_update_bits(component, RT5651_GLB_CLK,
			RT5651_SCLK_SRC_MASK, RT5651_SCLK_SRC_MCLK);
		return 0;
	}

	switch (source) {
	case RT5651_PLL1_S_MCLK:
		snd_soc_component_update_bits(component, RT5651_GLB_CLK,
			RT5651_PLL1_SRC_MASK, RT5651_PLL1_SRC_MCLK);
		break;
	case RT5651_PLL1_S_BCLK1:
		snd_soc_component_update_bits(component, RT5651_GLB_CLK,
				RT5651_PLL1_SRC_MASK, RT5651_PLL1_SRC_BCLK1);
		break;
	case RT5651_PLL1_S_BCLK2:
			snd_soc_component_update_bits(component, RT5651_GLB_CLK,
				RT5651_PLL1_SRC_MASK, RT5651_PLL1_SRC_BCLK2);
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

	snd_soc_component_write(component, RT5651_PLL_CTRL1,
		pll_code.n_code << RT5651_PLL_N_SFT | pll_code.k_code);
	snd_soc_component_write(component, RT5651_PLL_CTRL2,
		((pll_code.m_bp ? 0 : pll_code.m_code) << RT5651_PLL_M_SFT) |
		(pll_code.m_bp << RT5651_PLL_M_BP_SFT));

	rt5651->pll_in = freq_in;
	rt5651->pll_out = freq_out;
	rt5651->pll_src = source;

	return 0;
}

static int rt5651_set_bias_level(struct snd_soc_component *component,
			enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (SND_SOC_BIAS_STANDBY == snd_soc_component_get_bias_level(component)) {
			if (snd_soc_component_read(component, RT5651_PLL_MODE_1) & 0x9200)
				snd_soc_component_update_bits(component, RT5651_D_MISC,
						    0xc00, 0xc00);
		}
		break;
	case SND_SOC_BIAS_STANDBY:
		if (SND_SOC_BIAS_OFF == snd_soc_component_get_bias_level(component)) {
			snd_soc_component_update_bits(component, RT5651_PWR_ANLG1,
				RT5651_PWR_VREF1 | RT5651_PWR_MB |
				RT5651_PWR_BG | RT5651_PWR_VREF2,
				RT5651_PWR_VREF1 | RT5651_PWR_MB |
				RT5651_PWR_BG | RT5651_PWR_VREF2);
			usleep_range(10000, 15000);
			snd_soc_component_update_bits(component, RT5651_PWR_ANLG1,
				RT5651_PWR_FV1 | RT5651_PWR_FV2,
				RT5651_PWR_FV1 | RT5651_PWR_FV2);
			snd_soc_component_update_bits(component, RT5651_D_MISC, 0x1, 0x1);
		}
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_component_write(component, RT5651_D_MISC, 0x0010);
		snd_soc_component_write(component, RT5651_PWR_DIG1, 0x0000);
		snd_soc_component_write(component, RT5651_PWR_DIG2, 0x0000);
		snd_soc_component_write(component, RT5651_PWR_VOL, 0x0000);
		snd_soc_component_write(component, RT5651_PWR_MIXER, 0x0000);
		/* Do not touch the LDO voltage select bits on bias-off */
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG1,
			~RT5651_PWR_LDO_DVO_MASK, 0);
		/* Leave PLL1 and jack-detect power as is, all others off */
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
				    ~(RT5651_PWR_PLL | RT5651_PWR_JD_M), 0);
		break;

	default:
		break;
	}

	return 0;
}

static void rt5651_enable_micbias1_for_ovcd(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	snd_soc_dapm_mutex_lock(dapm);
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "LDO");
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "micbias1");
	/* OVCD is unreliable when used with RCCLK as sysclk-source */
	snd_soc_dapm_force_enable_pin_unlocked(dapm, "Platform Clock");
	snd_soc_dapm_sync_unlocked(dapm);
	snd_soc_dapm_mutex_unlock(dapm);
}

static void rt5651_disable_micbias1_for_ovcd(struct snd_soc_component *component)
{
	struct snd_soc_dapm_context *dapm = snd_soc_component_get_dapm(component);

	snd_soc_dapm_mutex_lock(dapm);
	snd_soc_dapm_disable_pin_unlocked(dapm, "Platform Clock");
	snd_soc_dapm_disable_pin_unlocked(dapm, "micbias1");
	snd_soc_dapm_disable_pin_unlocked(dapm, "LDO");
	snd_soc_dapm_sync_unlocked(dapm);
	snd_soc_dapm_mutex_unlock(dapm);
}

static void rt5651_enable_micbias1_ovcd_irq(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	snd_soc_component_update_bits(component, RT5651_IRQ_CTRL2,
		RT5651_IRQ_MB1_OC_MASK, RT5651_IRQ_MB1_OC_NOR);
	rt5651->ovcd_irq_enabled = true;
}

static void rt5651_disable_micbias1_ovcd_irq(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	snd_soc_component_update_bits(component, RT5651_IRQ_CTRL2,
		RT5651_IRQ_MB1_OC_MASK, RT5651_IRQ_MB1_OC_BP);
	rt5651->ovcd_irq_enabled = false;
}

static void rt5651_clear_micbias1_ovcd(struct snd_soc_component *component)
{
	snd_soc_component_update_bits(component, RT5651_IRQ_CTRL2,
		RT5651_MB1_OC_CLR, 0);
}

static bool rt5651_micbias1_ovcd(struct snd_soc_component *component)
{
	int val;

	val = snd_soc_component_read(component, RT5651_IRQ_CTRL2);
	dev_dbg(component->dev, "irq ctrl2 %#04x\n", val);

	return (val & RT5651_MB1_OC_CLR);
}

static bool rt5651_jack_inserted(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	int val;

	if (rt5651->gpiod_hp_det) {
		val = gpiod_get_value_cansleep(rt5651->gpiod_hp_det);
		dev_dbg(component->dev, "jack-detect gpio %d\n", val);
		return val;
	}

	val = snd_soc_component_read(component, RT5651_INT_IRQ_ST);
	dev_dbg(component->dev, "irq status %#04x\n", val);

	switch (rt5651->jd_src) {
	case RT5651_JD1_1:
		val &= 0x1000;
		break;
	case RT5651_JD1_2:
		val &= 0x2000;
		break;
	case RT5651_JD2:
		val &= 0x4000;
		break;
	default:
		break;
	}

	if (rt5651->jd_active_high)
		return val != 0;
	else
		return val == 0;
}

/* Jack detect and button-press timings */
#define JACK_SETTLE_TIME	100 /* milli seconds */
#define JACK_DETECT_COUNT	5
#define JACK_DETECT_MAXCOUNT	20  /* Aprox. 2 seconds worth of tries */
#define JACK_UNPLUG_TIME	80  /* milli seconds */
#define BP_POLL_TIME		10  /* milli seconds */
#define BP_POLL_MAXCOUNT	200 /* assume something is wrong after this */
#define BP_THRESHOLD		3

static void rt5651_start_button_press_work(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	rt5651->poll_count = 0;
	rt5651->press_count = 0;
	rt5651->release_count = 0;
	rt5651->pressed = false;
	rt5651->press_reported = false;
	rt5651_clear_micbias1_ovcd(component);
	schedule_delayed_work(&rt5651->bp_work, msecs_to_jiffies(BP_POLL_TIME));
}

static void rt5651_button_press_work(struct work_struct *work)
{
	struct rt5651_priv *rt5651 =
		container_of(work, struct rt5651_priv, bp_work.work);
	struct snd_soc_component *component = rt5651->component;

	/* Check the jack was not removed underneath us */
	if (!rt5651_jack_inserted(component))
		return;

	if (rt5651_micbias1_ovcd(component)) {
		rt5651->release_count = 0;
		rt5651->press_count++;
		/* Remember till after JACK_UNPLUG_TIME wait */
		if (rt5651->press_count >= BP_THRESHOLD)
			rt5651->pressed = true;
		rt5651_clear_micbias1_ovcd(component);
	} else {
		rt5651->press_count = 0;
		rt5651->release_count++;
	}

	/*
	 * The pins get temporarily shorted on jack unplug, so we poll for
	 * at least JACK_UNPLUG_TIME milli-seconds before reporting a press.
	 */
	rt5651->poll_count++;
	if (rt5651->poll_count < (JACK_UNPLUG_TIME / BP_POLL_TIME)) {
		schedule_delayed_work(&rt5651->bp_work,
				      msecs_to_jiffies(BP_POLL_TIME));
		return;
	}

	if (rt5651->pressed && !rt5651->press_reported) {
		dev_dbg(component->dev, "headset button press\n");
		snd_soc_jack_report(rt5651->hp_jack, SND_JACK_BTN_0,
				    SND_JACK_BTN_0);
		rt5651->press_reported = true;
	}

	if (rt5651->release_count >= BP_THRESHOLD) {
		if (rt5651->press_reported) {
			dev_dbg(component->dev, "headset button release\n");
			snd_soc_jack_report(rt5651->hp_jack, 0, SND_JACK_BTN_0);
		}
		/* Re-enable OVCD IRQ to detect next press */
		rt5651_enable_micbias1_ovcd_irq(component);
		return; /* Stop polling */
	}

	schedule_delayed_work(&rt5651->bp_work, msecs_to_jiffies(BP_POLL_TIME));
}

static int rt5651_detect_headset(struct snd_soc_component *component)
{
	int i, headset_count = 0, headphone_count = 0;

	/*
	 * We get the insertion event before the jack is fully inserted at which
	 * point the second ring on a TRRS connector may short the 2nd ring and
	 * sleeve contacts, also the overcurrent detection is not entirely
	 * reliable. So we try several times with a wait in between until we
	 * detect the same type JACK_DETECT_COUNT times in a row.
	 */
	for (i = 0; i < JACK_DETECT_MAXCOUNT; i++) {
		/* Clear any previous over-current status flag */
		rt5651_clear_micbias1_ovcd(component);

		msleep(JACK_SETTLE_TIME);

		/* Check the jack is still connected before checking ovcd */
		if (!rt5651_jack_inserted(component))
			return 0;

		if (rt5651_micbias1_ovcd(component)) {
			/*
			 * Over current detected, there is a short between the
			 * 2nd ring contact and the ground, so a TRS connector
			 * without a mic contact and thus plain headphones.
			 */
			dev_dbg(component->dev, "mic-gnd shorted\n");
			headset_count = 0;
			headphone_count++;
			if (headphone_count == JACK_DETECT_COUNT)
				return SND_JACK_HEADPHONE;
		} else {
			dev_dbg(component->dev, "mic-gnd open\n");
			headphone_count = 0;
			headset_count++;
			if (headset_count == JACK_DETECT_COUNT)
				return SND_JACK_HEADSET;
		}
	}

	dev_err(component->dev, "Error detecting headset vs headphones, bad contact?, assuming headphones\n");
	return SND_JACK_HEADPHONE;
}

static bool rt5651_support_button_press(struct rt5651_priv *rt5651)
{
	if (!rt5651->hp_jack)
		return false;

	/* Button press support only works with internal jack-detection */
	return (rt5651->hp_jack->status & SND_JACK_MICROPHONE) &&
		rt5651->gpiod_hp_det == NULL;
}

static void rt5651_jack_detect_work(struct work_struct *work)
{
	struct rt5651_priv *rt5651 =
		container_of(work, struct rt5651_priv, jack_detect_work);
	struct snd_soc_component *component = rt5651->component;
	int report;

	if (!rt5651_jack_inserted(component)) {
		/* Jack removed, or spurious IRQ? */
		if (rt5651->hp_jack->status & SND_JACK_HEADPHONE) {
			if (rt5651->hp_jack->status & SND_JACK_MICROPHONE) {
				cancel_delayed_work_sync(&rt5651->bp_work);
				rt5651_disable_micbias1_ovcd_irq(component);
				rt5651_disable_micbias1_for_ovcd(component);
			}
			snd_soc_jack_report(rt5651->hp_jack, 0,
					    SND_JACK_HEADSET | SND_JACK_BTN_0);
			dev_dbg(component->dev, "jack unplugged\n");
		}
	} else if (!(rt5651->hp_jack->status & SND_JACK_HEADPHONE)) {
		/* Jack inserted */
		WARN_ON(rt5651->ovcd_irq_enabled);
		rt5651_enable_micbias1_for_ovcd(component);
		report = rt5651_detect_headset(component);
		dev_dbg(component->dev, "detect report %#02x\n", report);
		snd_soc_jack_report(rt5651->hp_jack, report, SND_JACK_HEADSET);
		if (rt5651_support_button_press(rt5651)) {
			/* Enable ovcd IRQ for button press detect. */
			rt5651_enable_micbias1_ovcd_irq(component);
		} else {
			/* No more need for overcurrent detect. */
			rt5651_disable_micbias1_for_ovcd(component);
		}
	} else if (rt5651->ovcd_irq_enabled && rt5651_micbias1_ovcd(component)) {
		dev_dbg(component->dev, "OVCD IRQ\n");

		/*
		 * The ovcd IRQ keeps firing while the button is pressed, so
		 * we disable it and start polling the button until released.
		 *
		 * The disable will make the IRQ pin 0 again and since we get
		 * IRQs on both edges (so as to detect both jack plugin and
		 * unplug) this means we will immediately get another IRQ.
		 * The ovcd_irq_enabled check above makes the 2ND IRQ a NOP.
		 */
		rt5651_disable_micbias1_ovcd_irq(component);
		rt5651_start_button_press_work(component);

		/*
		 * If the jack-detect IRQ flag goes high (unplug) after our
		 * above rt5651_jack_inserted() check and before we have
		 * disabled the OVCD IRQ, the IRQ pin will stay high and as
		 * we react to edges, we miss the unplug event -> recheck.
		 */
		queue_work(system_long_wq, &rt5651->jack_detect_work);
	}
}

static irqreturn_t rt5651_irq(int irq, void *data)
{
	struct rt5651_priv *rt5651 = data;

	queue_work(system_power_efficient_wq, &rt5651->jack_detect_work);

	return IRQ_HANDLED;
}

static void rt5651_cancel_work(void *data)
{
	struct rt5651_priv *rt5651 = data;

	cancel_work_sync(&rt5651->jack_detect_work);
	cancel_delayed_work_sync(&rt5651->bp_work);
}

static void rt5651_enable_jack_detect(struct snd_soc_component *component,
				      struct snd_soc_jack *hp_jack,
				      struct gpio_desc *gpiod_hp_det)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	bool using_internal_jack_detect = true;

	/* Select jack detect source */
	switch (rt5651->jd_src) {
	case RT5651_JD_NULL:
		rt5651->gpiod_hp_det = gpiod_hp_det;
		if (!rt5651->gpiod_hp_det)
			return; /* No jack detect */
		using_internal_jack_detect = false;
		break;
	case RT5651_JD1_1:
		snd_soc_component_update_bits(component, RT5651_JD_CTRL2,
			RT5651_JD_TRG_SEL_MASK, RT5651_JD_TRG_SEL_JD1_1);
		/* active-low is normal, set inv flag for active-high */
		if (rt5651->jd_active_high)
			snd_soc_component_update_bits(component,
				RT5651_IRQ_CTRL1,
				RT5651_JD1_1_IRQ_EN | RT5651_JD1_1_INV,
				RT5651_JD1_1_IRQ_EN | RT5651_JD1_1_INV);
		else
			snd_soc_component_update_bits(component,
				RT5651_IRQ_CTRL1,
				RT5651_JD1_1_IRQ_EN | RT5651_JD1_1_INV,
				RT5651_JD1_1_IRQ_EN);
		break;
	case RT5651_JD1_2:
		snd_soc_component_update_bits(component, RT5651_JD_CTRL2,
			RT5651_JD_TRG_SEL_MASK, RT5651_JD_TRG_SEL_JD1_2);
		/* active-low is normal, set inv flag for active-high */
		if (rt5651->jd_active_high)
			snd_soc_component_update_bits(component,
				RT5651_IRQ_CTRL1,
				RT5651_JD1_2_IRQ_EN | RT5651_JD1_2_INV,
				RT5651_JD1_2_IRQ_EN | RT5651_JD1_2_INV);
		else
			snd_soc_component_update_bits(component,
				RT5651_IRQ_CTRL1,
				RT5651_JD1_2_IRQ_EN | RT5651_JD1_2_INV,
				RT5651_JD1_2_IRQ_EN);
		break;
	case RT5651_JD2:
		snd_soc_component_update_bits(component, RT5651_JD_CTRL2,
			RT5651_JD_TRG_SEL_MASK, RT5651_JD_TRG_SEL_JD2);
		/* active-low is normal, set inv flag for active-high */
		if (rt5651->jd_active_high)
			snd_soc_component_update_bits(component,
				RT5651_IRQ_CTRL1,
				RT5651_JD2_IRQ_EN | RT5651_JD2_INV,
				RT5651_JD2_IRQ_EN | RT5651_JD2_INV);
		else
			snd_soc_component_update_bits(component,
				RT5651_IRQ_CTRL1,
				RT5651_JD2_IRQ_EN | RT5651_JD2_INV,
				RT5651_JD2_IRQ_EN);
		break;
	default:
		dev_err(component->dev, "Currently only JD1_1 / JD1_2 / JD2 are supported\n");
		return;
	}

	if (using_internal_jack_detect) {
		/* IRQ output on GPIO1 */
		snd_soc_component_update_bits(component, RT5651_GPIO_CTRL1,
			RT5651_GP1_PIN_MASK, RT5651_GP1_PIN_IRQ);

		/* Enable jack detect power */
		snd_soc_component_update_bits(component, RT5651_PWR_ANLG2,
			RT5651_PWR_JD_M, RT5651_PWR_JD_M);
	}

	/* Set OVCD threshold current and scale-factor */
	snd_soc_component_write(component, RT5651_PR_BASE + RT5651_BIAS_CUR4,
				0xa800 | rt5651->ovcd_sf);

	snd_soc_component_update_bits(component, RT5651_MICBIAS,
				      RT5651_MIC1_OVCD_MASK |
				      RT5651_MIC1_OVTH_MASK |
				      RT5651_PWR_CLK12M_MASK |
				      RT5651_PWR_MB_MASK,
				      RT5651_MIC1_OVCD_EN |
				      rt5651->ovcd_th |
				      RT5651_PWR_MB_PU |
				      RT5651_PWR_CLK12M_PU);

	/*
	 * The over-current-detect is only reliable in detecting the absence
	 * of over-current, when the mic-contact in the jack is short-circuited,
	 * the hardware periodically retries if it can apply the bias-current
	 * leading to the ovcd status flip-flopping 1-0-1 with it being 0 about
	 * 10% of the time, as we poll the ovcd status bit we might hit that
	 * 10%, so we enable sticky mode and when checking OVCD we clear the
	 * status, msleep() a bit and then check to get a reliable reading.
	 */
	snd_soc_component_update_bits(component, RT5651_IRQ_CTRL2,
		RT5651_MB1_OC_STKY_MASK, RT5651_MB1_OC_STKY_EN);

	rt5651->hp_jack = hp_jack;
	if (rt5651_support_button_press(rt5651)) {
		rt5651_enable_micbias1_for_ovcd(component);
		rt5651_enable_micbias1_ovcd_irq(component);
	}

	enable_irq(rt5651->irq);
	/* sync initial jack state */
	queue_work(system_power_efficient_wq, &rt5651->jack_detect_work);
}

static void rt5651_disable_jack_detect(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	disable_irq(rt5651->irq);
	rt5651_cancel_work(rt5651);

	if (rt5651_support_button_press(rt5651)) {
		rt5651_disable_micbias1_ovcd_irq(component);
		rt5651_disable_micbias1_for_ovcd(component);
		snd_soc_jack_report(rt5651->hp_jack, 0, SND_JACK_BTN_0);
	}

	rt5651->hp_jack = NULL;
}

static int rt5651_set_jack(struct snd_soc_component *component,
			   struct snd_soc_jack *jack, void *data)
{
	if (jack)
		rt5651_enable_jack_detect(component, jack, data);
	else
		rt5651_disable_jack_detect(component);

	return 0;
}

/*
 * Note on some platforms the platform code may need to add device-properties,
 * rather then relying only on properties set by the firmware. Therefor the
 * property parsing MUST be done from the component driver's probe function,
 * rather then from the i2c driver's probe function, so that the platform-code
 * can attach extra properties before calling snd_soc_register_card().
 */
static void rt5651_apply_properties(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);
	u32 val;

	if (device_property_read_bool(component->dev, "realtek,in2-differential"))
		snd_soc_component_update_bits(component, RT5651_IN1_IN2,
				RT5651_IN_DF2, RT5651_IN_DF2);

	if (device_property_read_bool(component->dev, "realtek,dmic-en"))
		snd_soc_component_update_bits(component, RT5651_GPIO_CTRL1,
				RT5651_GP2_PIN_MASK, RT5651_GP2_PIN_DMIC1_SCL);

	if (device_property_read_u32(component->dev,
				     "realtek,jack-detect-source", &val) == 0)
		rt5651->jd_src = val;

	if (device_property_read_bool(component->dev, "realtek,jack-detect-not-inverted"))
		rt5651->jd_active_high = true;

	/*
	 * Testing on various boards has shown that good defaults for the OVCD
	 * threshold and scale-factor are 2000µA and 0.75. For an effective
	 * limit of 1500µA, this seems to be more reliable then 1500µA and 1.0.
	 */
	rt5651->ovcd_th = RT5651_MIC1_OVTH_2000UA;
	rt5651->ovcd_sf = RT5651_MIC_OVCD_SF_0P75;

	if (device_property_read_u32(component->dev,
			"realtek,over-current-threshold-microamp", &val) == 0) {
		switch (val) {
		case 600:
			rt5651->ovcd_th = RT5651_MIC1_OVTH_600UA;
			break;
		case 1500:
			rt5651->ovcd_th = RT5651_MIC1_OVTH_1500UA;
			break;
		case 2000:
			rt5651->ovcd_th = RT5651_MIC1_OVTH_2000UA;
			break;
		default:
			dev_warn(component->dev, "Warning: Invalid over-current-threshold-microamp value: %d, defaulting to 2000uA\n",
				 val);
		}
	}

	if (device_property_read_u32(component->dev,
			"realtek,over-current-scale-factor", &val) == 0) {
		if (val <= RT5651_OVCD_SF_1P5)
			rt5651->ovcd_sf = val << RT5651_MIC_OVCD_SF_SFT;
		else
			dev_warn(component->dev, "Warning: Invalid over-current-scale-factor value: %d, defaulting to 0.75\n",
				 val);
	}
}

static int rt5651_probe(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	rt5651->component = component;

	snd_soc_component_update_bits(component, RT5651_PWR_ANLG1,
		RT5651_PWR_LDO_DVO_MASK, RT5651_PWR_LDO_DVO_1_2V);

	snd_soc_component_force_bias_level(component, SND_SOC_BIAS_OFF);

	rt5651_apply_properties(component);

	return 0;
}

#ifdef CONFIG_PM
static int rt5651_suspend(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt5651->regmap, true);
	regcache_mark_dirty(rt5651->regmap);
	return 0;
}

static int rt5651_resume(struct snd_soc_component *component)
{
	struct rt5651_priv *rt5651 = snd_soc_component_get_drvdata(component);

	regcache_cache_only(rt5651->regmap, false);
	snd_soc_component_cache_sync(component);

	return 0;
}
#else
#define rt5651_suspend NULL
#define rt5651_resume NULL
#endif

#define RT5651_STEREO_RATES SNDRV_PCM_RATE_8000_96000
#define RT5651_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt5651_aif_dai_ops = {
	.hw_params = rt5651_hw_params,
	.set_fmt = rt5651_set_dai_fmt,
	.set_sysclk = rt5651_set_dai_sysclk,
	.set_pll = rt5651_set_dai_pll,
};

static struct snd_soc_dai_driver rt5651_dai[] = {
	{
		.name = "rt5651-aif1",
		.id = RT5651_AIF1,
		.playback = {
			.stream_name = "AIF1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5651_STEREO_RATES,
			.formats = RT5651_FORMATS,
		},
		.capture = {
			.stream_name = "AIF1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5651_STEREO_RATES,
			.formats = RT5651_FORMATS,
		},
		.ops = &rt5651_aif_dai_ops,
	},
	{
		.name = "rt5651-aif2",
		.id = RT5651_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5651_STEREO_RATES,
			.formats = RT5651_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5651_STEREO_RATES,
			.formats = RT5651_FORMATS,
		},
		.ops = &rt5651_aif_dai_ops,
	},
};

static const struct snd_soc_component_driver soc_component_dev_rt5651 = {
	.probe			= rt5651_probe,
	.suspend		= rt5651_suspend,
	.resume			= rt5651_resume,
	.set_bias_level		= rt5651_set_bias_level,
	.set_jack		= rt5651_set_jack,
	.controls		= rt5651_snd_controls,
	.num_controls		= ARRAY_SIZE(rt5651_snd_controls),
	.dapm_widgets		= rt5651_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(rt5651_dapm_widgets),
	.dapm_routes		= rt5651_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(rt5651_dapm_routes),
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config rt5651_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.max_register = RT5651_DEVICE_ID + 1 + (ARRAY_SIZE(rt5651_ranges) *
					       RT5651_PR_SPACING),
	.volatile_reg = rt5651_volatile_register,
	.readable_reg = rt5651_readable_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = rt5651_reg,
	.num_reg_defaults = ARRAY_SIZE(rt5651_reg),
	.ranges = rt5651_ranges,
	.num_ranges = ARRAY_SIZE(rt5651_ranges),
	.use_single_read = true,
	.use_single_write = true,
};

#if defined(CONFIG_OF)
static const struct of_device_id rt5651_of_match[] = {
	{ .compatible = "realtek,rt5651", },
	{},
};
MODULE_DEVICE_TABLE(of, rt5651_of_match);
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id rt5651_acpi_match[] = {
	{ "10EC5651", 0 },
	{ "10EC5640", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, rt5651_acpi_match);
#endif

static const struct i2c_device_id rt5651_i2c_id[] = {
	{ "rt5651", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rt5651_i2c_id);

/*
 * Note this function MUST not look at device-properties, see the comment
 * above rt5651_apply_properties().
 */
static int rt5651_i2c_probe(struct i2c_client *i2c,
		    const struct i2c_device_id *id)
{
	struct rt5651_priv *rt5651;
	int ret;
	int err;

	rt5651 = devm_kzalloc(&i2c->dev, sizeof(*rt5651),
				GFP_KERNEL);
	if (NULL == rt5651)
		return -ENOMEM;

	i2c_set_clientdata(i2c, rt5651);

	rt5651->regmap = devm_regmap_init_i2c(i2c, &rt5651_regmap);
	if (IS_ERR(rt5651->regmap)) {
		ret = PTR_ERR(rt5651->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	err = regmap_read(rt5651->regmap, RT5651_DEVICE_ID, &ret);
	if (err)
		return err;

	if (ret != RT5651_DEVICE_ID_VALUE) {
		dev_err(&i2c->dev,
			"Device with ID register %#x is not rt5651\n", ret);
		return -ENODEV;
	}

	regmap_write(rt5651->regmap, RT5651_RESET, 0);

	ret = regmap_register_patch(rt5651->regmap, init_list,
				    ARRAY_SIZE(init_list));
	if (ret != 0)
		dev_warn(&i2c->dev, "Failed to apply regmap patch: %d\n", ret);

	rt5651->irq = i2c->irq;
	rt5651->hp_mute = true;

	INIT_DELAYED_WORK(&rt5651->bp_work, rt5651_button_press_work);
	INIT_WORK(&rt5651->jack_detect_work, rt5651_jack_detect_work);

	/* Make sure work is stopped on probe-error / remove */
	ret = devm_add_action_or_reset(&i2c->dev, rt5651_cancel_work, rt5651);
	if (ret)
		return ret;

	ret = devm_request_irq(&i2c->dev, rt5651->irq, rt5651_irq,
			       IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING
			       | IRQF_ONESHOT, "rt5651", rt5651);
	if (ret == 0) {
		/* Gets re-enabled by rt5651_set_jack() */
		disable_irq(rt5651->irq);
	} else {
		dev_warn(&i2c->dev, "Failed to reguest IRQ %d: %d\n",
			 rt5651->irq, ret);
		rt5651->irq = -ENXIO;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
				&soc_component_dev_rt5651,
				rt5651_dai, ARRAY_SIZE(rt5651_dai));

	return ret;
}

static struct i2c_driver rt5651_i2c_driver = {
	.driver = {
		.name = "rt5651",
		.acpi_match_table = ACPI_PTR(rt5651_acpi_match),
		.of_match_table = of_match_ptr(rt5651_of_match),
	},
	.probe = rt5651_i2c_probe,
	.id_table = rt5651_i2c_id,
};
module_i2c_driver(rt5651_i2c_driver);

MODULE_DESCRIPTION("ASoC RT5651 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL v2");
