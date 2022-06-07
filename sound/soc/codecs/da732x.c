// SPDX-License-Identifier: GPL-2.0-only
/*
 * da732x.c --- Dialog DA732X ALSA SoC Audio Driver
 *
 * Copyright (C) 2012 Dialog Semiconductor GmbH
 *
 * Author: Michal Hajduk <Michal.Hajduk@diasemi.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/div64.h>

#include "da732x.h"
#include "da732x_reg.h"


struct da732x_priv {
	struct regmap *regmap;

	unsigned int sysclk;
	bool pll_en;
};

/*
 * da732x register cache - default settings
 */
static const struct reg_default da732x_reg_cache[] = {
	{ DA732X_REG_REF1		, 0x02 },
	{ DA732X_REG_BIAS_EN		, 0x80 },
	{ DA732X_REG_BIAS1		, 0x00 },
	{ DA732X_REG_BIAS2		, 0x00 },
	{ DA732X_REG_BIAS3		, 0x00 },
	{ DA732X_REG_BIAS4		, 0x00 },
	{ DA732X_REG_MICBIAS2		, 0x00 },
	{ DA732X_REG_MICBIAS1		, 0x00 },
	{ DA732X_REG_MICDET		, 0x00 },
	{ DA732X_REG_MIC1_PRE		, 0x01 },
	{ DA732X_REG_MIC1		, 0x40 },
	{ DA732X_REG_MIC2_PRE		, 0x01 },
	{ DA732X_REG_MIC2		, 0x40 },
	{ DA732X_REG_AUX1L		, 0x75 },
	{ DA732X_REG_AUX1R		, 0x75 },
	{ DA732X_REG_MIC3_PRE		, 0x01 },
	{ DA732X_REG_MIC3		, 0x40 },
	{ DA732X_REG_INP_PINBIAS	, 0x00 },
	{ DA732X_REG_INP_ZC_EN		, 0x00 },
	{ DA732X_REG_INP_MUX		, 0x50 },
	{ DA732X_REG_HP_DET		, 0x00 },
	{ DA732X_REG_HPL_DAC_OFFSET	, 0x00 },
	{ DA732X_REG_HPL_DAC_OFF_CNTL	, 0x00 },
	{ DA732X_REG_HPL_OUT_OFFSET	, 0x00 },
	{ DA732X_REG_HPL		, 0x40 },
	{ DA732X_REG_HPL_VOL		, 0x0F },
	{ DA732X_REG_HPR_DAC_OFFSET	, 0x00 },
	{ DA732X_REG_HPR_DAC_OFF_CNTL	, 0x00 },
	{ DA732X_REG_HPR_OUT_OFFSET	, 0x00 },
	{ DA732X_REG_HPR		, 0x40 },
	{ DA732X_REG_HPR_VOL		, 0x0F },
	{ DA732X_REG_LIN2		, 0x4F },
	{ DA732X_REG_LIN3		, 0x4F },
	{ DA732X_REG_LIN4		, 0x4F },
	{ DA732X_REG_OUT_ZC_EN		, 0x00 },
	{ DA732X_REG_HP_LIN1_GNDSEL	, 0x00 },
	{ DA732X_REG_CP_HP1		, 0x0C },
	{ DA732X_REG_CP_HP2		, 0x03 },
	{ DA732X_REG_CP_CTRL1		, 0x00 },
	{ DA732X_REG_CP_CTRL2		, 0x99 },
	{ DA732X_REG_CP_CTRL3		, 0x25 },
	{ DA732X_REG_CP_LEVEL_MASK	, 0x3F },
	{ DA732X_REG_CP_DET		, 0x00 },
	{ DA732X_REG_CP_STATUS		, 0x00 },
	{ DA732X_REG_CP_THRESH1		, 0x00 },
	{ DA732X_REG_CP_THRESH2		, 0x00 },
	{ DA732X_REG_CP_THRESH3		, 0x00 },
	{ DA732X_REG_CP_THRESH4		, 0x00 },
	{ DA732X_REG_CP_THRESH5		, 0x00 },
	{ DA732X_REG_CP_THRESH6		, 0x00 },
	{ DA732X_REG_CP_THRESH7		, 0x00 },
	{ DA732X_REG_CP_THRESH8		, 0x00 },
	{ DA732X_REG_PLL_DIV_LO		, 0x00 },
	{ DA732X_REG_PLL_DIV_MID	, 0x00 },
	{ DA732X_REG_PLL_DIV_HI		, 0x00 },
	{ DA732X_REG_PLL_CTRL		, 0x02 },
	{ DA732X_REG_CLK_CTRL		, 0xaa },
	{ DA732X_REG_CLK_DSP		, 0x07 },
	{ DA732X_REG_CLK_EN1		, 0x00 },
	{ DA732X_REG_CLK_EN2		, 0x00 },
	{ DA732X_REG_CLK_EN3		, 0x00 },
	{ DA732X_REG_CLK_EN4		, 0x00 },
	{ DA732X_REG_CLK_EN5		, 0x00 },
	{ DA732X_REG_AIF_MCLK		, 0x00 },
	{ DA732X_REG_AIFA1		, 0x02 },
	{ DA732X_REG_AIFA2		, 0x00 },
	{ DA732X_REG_AIFA3		, 0x08 },
	{ DA732X_REG_AIFB1		, 0x02 },
	{ DA732X_REG_AIFB2		, 0x00 },
	{ DA732X_REG_AIFB3		, 0x08 },
	{ DA732X_REG_PC_CTRL		, 0xC0 },
	{ DA732X_REG_DATA_ROUTE		, 0x00 },
	{ DA732X_REG_DSP_CTRL		, 0x00 },
	{ DA732X_REG_CIF_CTRL2		, 0x00 },
	{ DA732X_REG_HANDSHAKE		, 0x00 },
	{ DA732X_REG_SPARE1_OUT		, 0x00 },
	{ DA732X_REG_SPARE2_OUT		, 0x00 },
	{ DA732X_REG_SPARE1_IN		, 0x00 },
	{ DA732X_REG_ADC1_PD		, 0x00 },
	{ DA732X_REG_ADC1_HPF		, 0x00 },
	{ DA732X_REG_ADC1_SEL		, 0x00 },
	{ DA732X_REG_ADC1_EQ12		, 0x00 },
	{ DA732X_REG_ADC1_EQ34		, 0x00 },
	{ DA732X_REG_ADC1_EQ5		, 0x00 },
	{ DA732X_REG_ADC2_PD		, 0x00 },
	{ DA732X_REG_ADC2_HPF		, 0x00 },
	{ DA732X_REG_ADC2_SEL		, 0x00 },
	{ DA732X_REG_ADC2_EQ12		, 0x00 },
	{ DA732X_REG_ADC2_EQ34		, 0x00 },
	{ DA732X_REG_ADC2_EQ5		, 0x00 },
	{ DA732X_REG_DAC1_HPF		, 0x00 },
	{ DA732X_REG_DAC1_L_VOL		, 0x00 },
	{ DA732X_REG_DAC1_R_VOL		, 0x00 },
	{ DA732X_REG_DAC1_SEL		, 0x00 },
	{ DA732X_REG_DAC1_SOFTMUTE	, 0x00 },
	{ DA732X_REG_DAC1_EQ12		, 0x00 },
	{ DA732X_REG_DAC1_EQ34		, 0x00 },
	{ DA732X_REG_DAC1_EQ5		, 0x00 },
	{ DA732X_REG_DAC2_HPF		, 0x00 },
	{ DA732X_REG_DAC2_L_VOL		, 0x00 },
	{ DA732X_REG_DAC2_R_VOL		, 0x00 },
	{ DA732X_REG_DAC2_SEL		, 0x00 },
	{ DA732X_REG_DAC2_SOFTMUTE	, 0x00 },
	{ DA732X_REG_DAC2_EQ12		, 0x00 },
	{ DA732X_REG_DAC2_EQ34		, 0x00 },
	{ DA732X_REG_DAC2_EQ5		, 0x00 },
	{ DA732X_REG_DAC3_HPF		, 0x00 },
	{ DA732X_REG_DAC3_VOL		, 0x00 },
	{ DA732X_REG_DAC3_SEL		, 0x00 },
	{ DA732X_REG_DAC3_SOFTMUTE	, 0x00 },
	{ DA732X_REG_DAC3_EQ12		, 0x00 },
	{ DA732X_REG_DAC3_EQ34		, 0x00 },
	{ DA732X_REG_DAC3_EQ5		, 0x00 },
	{ DA732X_REG_BIQ_BYP		, 0x00 },
	{ DA732X_REG_DMA_CMD		, 0x00 },
	{ DA732X_REG_DMA_ADDR0		, 0x00 },
	{ DA732X_REG_DMA_ADDR1		, 0x00 },
	{ DA732X_REG_DMA_DATA0		, 0x00 },
	{ DA732X_REG_DMA_DATA1		, 0x00 },
	{ DA732X_REG_DMA_DATA2		, 0x00 },
	{ DA732X_REG_DMA_DATA3		, 0x00 },
	{ DA732X_REG_UNLOCK		, 0x00 },
};

static inline int da732x_get_input_div(struct snd_soc_component *component, int sysclk)
{
	int val;

	if (sysclk < DA732X_MCLK_10MHZ) {
		val = DA732X_MCLK_VAL_0_10MHZ;
	} else if ((sysclk >= DA732X_MCLK_10MHZ) &&
	    (sysclk < DA732X_MCLK_20MHZ)) {
		val = DA732X_MCLK_VAL_10_20MHZ;
	} else if ((sysclk >= DA732X_MCLK_20MHZ) &&
	    (sysclk < DA732X_MCLK_40MHZ)) {
		val = DA732X_MCLK_VAL_20_40MHZ;
	} else if ((sysclk >= DA732X_MCLK_40MHZ) &&
	    (sysclk <= DA732X_MCLK_54MHZ)) {
		val = DA732X_MCLK_VAL_40_54MHZ;
	} else {
		return -EINVAL;
	}

	snd_soc_component_write(component, DA732X_REG_PLL_CTRL, val);

	return val;
}

static void da732x_set_charge_pump(struct snd_soc_component *component, int state)
{
	switch (state) {
	case DA732X_ENABLE_CP:
		snd_soc_component_write(component, DA732X_REG_CLK_EN2, DA732X_CP_CLK_EN);
		snd_soc_component_write(component, DA732X_REG_CP_HP2, DA732X_HP_CP_EN |
			      DA732X_HP_CP_REG | DA732X_HP_CP_PULSESKIP);
		snd_soc_component_write(component, DA732X_REG_CP_CTRL1, DA732X_CP_EN |
			      DA732X_CP_CTRL_CPVDD1);
		snd_soc_component_write(component, DA732X_REG_CP_CTRL2,
			      DA732X_CP_MANAGE_MAGNITUDE | DA732X_CP_BOOST);
		snd_soc_component_write(component, DA732X_REG_CP_CTRL3, DA732X_CP_1MHZ);
		break;
	case DA732X_DISABLE_CP:
		snd_soc_component_write(component, DA732X_REG_CLK_EN2, DA732X_CP_CLK_DIS);
		snd_soc_component_write(component, DA732X_REG_CP_HP2, DA732X_HP_CP_DIS);
		snd_soc_component_write(component, DA732X_REG_CP_CTRL1, DA723X_CP_DIS);
		break;
	default:
		pr_err("Wrong charge pump state\n");
		break;
	}
}

static const DECLARE_TLV_DB_SCALE(mic_boost_tlv, DA732X_MIC_PRE_VOL_DB_MIN,
				  DA732X_MIC_PRE_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(mic_pga_tlv, DA732X_MIC_VOL_DB_MIN,
				  DA732X_MIC_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(aux_pga_tlv, DA732X_AUX_VOL_DB_MIN,
				  DA732X_AUX_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(hp_pga_tlv, DA732X_HP_VOL_DB_MIN,
				  DA732X_AUX_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(lin2_pga_tlv, DA732X_LIN2_VOL_DB_MIN,
				  DA732X_LIN2_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(lin3_pga_tlv, DA732X_LIN3_VOL_DB_MIN,
				  DA732X_LIN3_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(lin4_pga_tlv, DA732X_LIN4_VOL_DB_MIN,
				  DA732X_LIN4_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(adc_pga_tlv, DA732X_ADC_VOL_DB_MIN,
				  DA732X_ADC_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(dac_pga_tlv, DA732X_DAC_VOL_DB_MIN,
				  DA732X_DAC_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(eq_band_pga_tlv, DA732X_EQ_BAND_VOL_DB_MIN,
				  DA732X_EQ_BAND_VOL_DB_INC, 0);

static const DECLARE_TLV_DB_SCALE(eq_overall_tlv, DA732X_EQ_OVERALL_VOL_DB_MIN,
				  DA732X_EQ_OVERALL_VOL_DB_INC, 0);

/* High Pass Filter */
static const char *da732x_hpf_mode[] = {
	"Disable", "Music", "Voice",
};

static const char *da732x_hpf_music[] = {
	"1.8Hz", "3.75Hz", "7.5Hz", "15Hz",
};

static const char *da732x_hpf_voice[] = {
	"2.5Hz", "25Hz", "50Hz", "100Hz",
	"150Hz", "200Hz", "300Hz", "400Hz"
};

static SOC_ENUM_SINGLE_DECL(da732x_dac1_hpf_mode_enum,
			    DA732X_REG_DAC1_HPF, DA732X_HPF_MODE_SHIFT,
			    da732x_hpf_mode);

static SOC_ENUM_SINGLE_DECL(da732x_dac2_hpf_mode_enum,
			    DA732X_REG_DAC2_HPF, DA732X_HPF_MODE_SHIFT,
			    da732x_hpf_mode);

static SOC_ENUM_SINGLE_DECL(da732x_dac3_hpf_mode_enum,
			    DA732X_REG_DAC3_HPF, DA732X_HPF_MODE_SHIFT,
			    da732x_hpf_mode);

static SOC_ENUM_SINGLE_DECL(da732x_adc1_hpf_mode_enum,
			    DA732X_REG_ADC1_HPF, DA732X_HPF_MODE_SHIFT,
			    da732x_hpf_mode);

static SOC_ENUM_SINGLE_DECL(da732x_adc2_hpf_mode_enum,
			    DA732X_REG_ADC2_HPF, DA732X_HPF_MODE_SHIFT,
			    da732x_hpf_mode);

static SOC_ENUM_SINGLE_DECL(da732x_dac1_hp_filter_enum,
			    DA732X_REG_DAC1_HPF, DA732X_HPF_MUSIC_SHIFT,
			    da732x_hpf_music);

static SOC_ENUM_SINGLE_DECL(da732x_dac2_hp_filter_enum,
			    DA732X_REG_DAC2_HPF, DA732X_HPF_MUSIC_SHIFT,
			    da732x_hpf_music);

static SOC_ENUM_SINGLE_DECL(da732x_dac3_hp_filter_enum,
			    DA732X_REG_DAC3_HPF, DA732X_HPF_MUSIC_SHIFT,
			    da732x_hpf_music);

static SOC_ENUM_SINGLE_DECL(da732x_adc1_hp_filter_enum,
			    DA732X_REG_ADC1_HPF, DA732X_HPF_MUSIC_SHIFT,
			    da732x_hpf_music);

static SOC_ENUM_SINGLE_DECL(da732x_adc2_hp_filter_enum,
			    DA732X_REG_ADC2_HPF, DA732X_HPF_MUSIC_SHIFT,
			    da732x_hpf_music);

static SOC_ENUM_SINGLE_DECL(da732x_dac1_voice_filter_enum,
			    DA732X_REG_DAC1_HPF, DA732X_HPF_VOICE_SHIFT,
			    da732x_hpf_voice);

static SOC_ENUM_SINGLE_DECL(da732x_dac2_voice_filter_enum,
			    DA732X_REG_DAC2_HPF, DA732X_HPF_VOICE_SHIFT,
			    da732x_hpf_voice);

static SOC_ENUM_SINGLE_DECL(da732x_dac3_voice_filter_enum,
			    DA732X_REG_DAC3_HPF, DA732X_HPF_VOICE_SHIFT,
			    da732x_hpf_voice);

static SOC_ENUM_SINGLE_DECL(da732x_adc1_voice_filter_enum,
			    DA732X_REG_ADC1_HPF, DA732X_HPF_VOICE_SHIFT,
			    da732x_hpf_voice);

static SOC_ENUM_SINGLE_DECL(da732x_adc2_voice_filter_enum,
			    DA732X_REG_ADC2_HPF, DA732X_HPF_VOICE_SHIFT,
			    da732x_hpf_voice);

static int da732x_hpf_set(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_enum *enum_ctrl = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg = enum_ctrl->reg;
	unsigned int sel = ucontrol->value.enumerated.item[0];
	unsigned int bits;

	switch (sel) {
	case DA732X_HPF_DISABLED:
		bits = DA732X_HPF_DIS;
		break;
	case DA732X_HPF_VOICE:
		bits = DA732X_HPF_VOICE_EN;
		break;
	case DA732X_HPF_MUSIC:
		bits = DA732X_HPF_MUSIC_EN;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, reg, DA732X_HPF_MASK, bits);

	return 0;
}

static int da732x_hpf_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_soc_kcontrol_component(kcontrol);
	struct soc_enum *enum_ctrl = (struct soc_enum *)kcontrol->private_value;
	unsigned int reg = enum_ctrl->reg;
	int val;

	val = snd_soc_component_read(component, reg) & DA732X_HPF_MASK;

	switch (val) {
	case DA732X_HPF_VOICE_EN:
		ucontrol->value.enumerated.item[0] = DA732X_HPF_VOICE;
		break;
	case DA732X_HPF_MUSIC_EN:
		ucontrol->value.enumerated.item[0] = DA732X_HPF_MUSIC;
		break;
	default:
		ucontrol->value.enumerated.item[0] = DA732X_HPF_DISABLED;
		break;
	}

	return 0;
}

static const struct snd_kcontrol_new da732x_snd_controls[] = {
	/* Input PGAs */
	SOC_SINGLE_RANGE_TLV("MIC1 Boost Volume", DA732X_REG_MIC1_PRE,
			     DA732X_MICBOOST_SHIFT, DA732X_MICBOOST_MIN,
			     DA732X_MICBOOST_MAX, 0, mic_boost_tlv),
	SOC_SINGLE_RANGE_TLV("MIC2 Boost Volume", DA732X_REG_MIC2_PRE,
			     DA732X_MICBOOST_SHIFT, DA732X_MICBOOST_MIN,
			     DA732X_MICBOOST_MAX, 0, mic_boost_tlv),
	SOC_SINGLE_RANGE_TLV("MIC3 Boost Volume", DA732X_REG_MIC3_PRE,
			     DA732X_MICBOOST_SHIFT, DA732X_MICBOOST_MIN,
			     DA732X_MICBOOST_MAX, 0, mic_boost_tlv),

	/* MICs */
	SOC_SINGLE("MIC1 Switch", DA732X_REG_MIC1, DA732X_MIC_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_RANGE_TLV("MIC1 Volume", DA732X_REG_MIC1,
			     DA732X_MIC_VOL_SHIFT, DA732X_MIC_VOL_VAL_MIN,
			     DA732X_MIC_VOL_VAL_MAX, 0, mic_pga_tlv),
	SOC_SINGLE("MIC2 Switch", DA732X_REG_MIC2, DA732X_MIC_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_RANGE_TLV("MIC2 Volume", DA732X_REG_MIC2,
			     DA732X_MIC_VOL_SHIFT, DA732X_MIC_VOL_VAL_MIN,
			     DA732X_MIC_VOL_VAL_MAX, 0, mic_pga_tlv),
	SOC_SINGLE("MIC3 Switch", DA732X_REG_MIC3, DA732X_MIC_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_RANGE_TLV("MIC3 Volume", DA732X_REG_MIC3,
			     DA732X_MIC_VOL_SHIFT, DA732X_MIC_VOL_VAL_MIN,
			     DA732X_MIC_VOL_VAL_MAX, 0, mic_pga_tlv),

	/* AUXs */
	SOC_SINGLE("AUX1L Switch", DA732X_REG_AUX1L, DA732X_AUX_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("AUX1L Volume", DA732X_REG_AUX1L,
		       DA732X_AUX_VOL_SHIFT, DA732X_AUX_VOL_VAL_MAX,
		       DA732X_NO_INVERT, aux_pga_tlv),
	SOC_SINGLE("AUX1R Switch", DA732X_REG_AUX1R, DA732X_AUX_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("AUX1R Volume", DA732X_REG_AUX1R,
		       DA732X_AUX_VOL_SHIFT, DA732X_AUX_VOL_VAL_MAX,
		       DA732X_NO_INVERT, aux_pga_tlv),

	/* ADCs */
	SOC_DOUBLE_TLV("ADC1 Volume", DA732X_REG_ADC1_SEL,
		       DA732X_ADCL_VOL_SHIFT, DA732X_ADCR_VOL_SHIFT,
		       DA732X_ADC_VOL_VAL_MAX, DA732X_INVERT, adc_pga_tlv),

	SOC_DOUBLE_TLV("ADC2 Volume", DA732X_REG_ADC2_SEL,
		       DA732X_ADCL_VOL_SHIFT, DA732X_ADCR_VOL_SHIFT,
		       DA732X_ADC_VOL_VAL_MAX, DA732X_INVERT, adc_pga_tlv),

	/* DACs */
	SOC_DOUBLE("Digital Playback DAC12 Switch", DA732X_REG_DAC1_SEL,
		   DA732X_DACL_MUTE_SHIFT, DA732X_DACR_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_DOUBLE_R_TLV("Digital Playback DAC12 Volume", DA732X_REG_DAC1_L_VOL,
			 DA732X_REG_DAC1_R_VOL, DA732X_DAC_VOL_SHIFT,
			 DA732X_DAC_VOL_VAL_MAX, DA732X_INVERT, dac_pga_tlv),
	SOC_SINGLE("Digital Playback DAC3 Switch", DA732X_REG_DAC2_SEL,
		   DA732X_DACL_MUTE_SHIFT, DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("Digital Playback DAC3 Volume", DA732X_REG_DAC2_L_VOL,
			DA732X_DAC_VOL_SHIFT, DA732X_DAC_VOL_VAL_MAX,
			DA732X_INVERT, dac_pga_tlv),
	SOC_SINGLE("Digital Playback DAC4 Switch", DA732X_REG_DAC2_SEL,
		   DA732X_DACR_MUTE_SHIFT, DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("Digital Playback DAC4 Volume", DA732X_REG_DAC2_R_VOL,
		       DA732X_DAC_VOL_SHIFT, DA732X_DAC_VOL_VAL_MAX,
		       DA732X_INVERT, dac_pga_tlv),
	SOC_SINGLE("Digital Playback DAC5 Switch", DA732X_REG_DAC3_SEL,
		   DA732X_DACL_MUTE_SHIFT, DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("Digital Playback DAC5 Volume", DA732X_REG_DAC3_VOL,
		       DA732X_DAC_VOL_SHIFT, DA732X_DAC_VOL_VAL_MAX,
		       DA732X_INVERT, dac_pga_tlv),

	/* High Pass Filters */
	SOC_ENUM_EXT("DAC1 High Pass Filter Mode",
		     da732x_dac1_hpf_mode_enum, da732x_hpf_get, da732x_hpf_set),
	SOC_ENUM("DAC1 High Pass Filter", da732x_dac1_hp_filter_enum),
	SOC_ENUM("DAC1 Voice Filter", da732x_dac1_voice_filter_enum),

	SOC_ENUM_EXT("DAC2 High Pass Filter Mode",
		     da732x_dac2_hpf_mode_enum, da732x_hpf_get, da732x_hpf_set),
	SOC_ENUM("DAC2 High Pass Filter", da732x_dac2_hp_filter_enum),
	SOC_ENUM("DAC2 Voice Filter", da732x_dac2_voice_filter_enum),

	SOC_ENUM_EXT("DAC3 High Pass Filter Mode",
		     da732x_dac3_hpf_mode_enum, da732x_hpf_get, da732x_hpf_set),
	SOC_ENUM("DAC3 High Pass Filter", da732x_dac3_hp_filter_enum),
	SOC_ENUM("DAC3 Filter Mode", da732x_dac3_voice_filter_enum),

	SOC_ENUM_EXT("ADC1 High Pass Filter Mode",
		     da732x_adc1_hpf_mode_enum, da732x_hpf_get, da732x_hpf_set),
	SOC_ENUM("ADC1 High Pass Filter", da732x_adc1_hp_filter_enum),
	SOC_ENUM("ADC1 Voice Filter", da732x_adc1_voice_filter_enum),

	SOC_ENUM_EXT("ADC2 High Pass Filter Mode",
		     da732x_adc2_hpf_mode_enum, da732x_hpf_get, da732x_hpf_set),
	SOC_ENUM("ADC2 High Pass Filter", da732x_adc2_hp_filter_enum),
	SOC_ENUM("ADC2 Voice Filter", da732x_adc2_voice_filter_enum),

	/* Equalizers */
	SOC_SINGLE("ADC1 EQ Switch", DA732X_REG_ADC1_EQ5,
		   DA732X_EQ_EN_SHIFT, DA732X_EQ_EN_MAX, DA732X_NO_INVERT),
	SOC_SINGLE_TLV("ADC1 EQ Band 1 Volume", DA732X_REG_ADC1_EQ12,
		       DA732X_EQ_BAND1_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC1 EQ Band 2 Volume", DA732X_REG_ADC1_EQ12,
		       DA732X_EQ_BAND2_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC1 EQ Band 3 Volume", DA732X_REG_ADC1_EQ34,
		       DA732X_EQ_BAND3_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC1 EQ Band 4 Volume", DA732X_REG_ADC1_EQ34,
		       DA732X_EQ_BAND4_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC1 EQ Band 5 Volume", DA732X_REG_ADC1_EQ5,
		       DA732X_EQ_BAND5_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC1 EQ Overall Volume", DA732X_REG_ADC1_EQ5,
		       DA732X_EQ_OVERALL_SHIFT, DA732X_EQ_OVERALL_VOL_VAL_MAX,
		       DA732X_INVERT, eq_overall_tlv),

	SOC_SINGLE("ADC2 EQ Switch", DA732X_REG_ADC2_EQ5,
		   DA732X_EQ_EN_SHIFT, DA732X_EQ_EN_MAX, DA732X_NO_INVERT),
	SOC_SINGLE_TLV("ADC2 EQ Band 1 Volume", DA732X_REG_ADC2_EQ12,
		       DA732X_EQ_BAND1_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC2 EQ Band 2 Volume", DA732X_REG_ADC2_EQ12,
		       DA732X_EQ_BAND2_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC2 EQ Band 3 Volume", DA732X_REG_ADC2_EQ34,
		       DA732X_EQ_BAND3_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ACD2 EQ Band 4 Volume", DA732X_REG_ADC2_EQ34,
		       DA732X_EQ_BAND4_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ACD2 EQ Band 5 Volume", DA732X_REG_ADC2_EQ5,
		       DA732X_EQ_BAND5_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("ADC2 EQ Overall Volume", DA732X_REG_ADC1_EQ5,
		       DA732X_EQ_OVERALL_SHIFT, DA732X_EQ_OVERALL_VOL_VAL_MAX,
		       DA732X_INVERT, eq_overall_tlv),

	SOC_SINGLE("DAC1 EQ Switch", DA732X_REG_DAC1_EQ5,
		   DA732X_EQ_EN_SHIFT, DA732X_EQ_EN_MAX, DA732X_NO_INVERT),
	SOC_SINGLE_TLV("DAC1 EQ Band 1 Volume", DA732X_REG_DAC1_EQ12,
		       DA732X_EQ_BAND1_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC1 EQ Band 2 Volume", DA732X_REG_DAC1_EQ12,
		       DA732X_EQ_BAND2_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC1 EQ Band 3 Volume", DA732X_REG_DAC1_EQ34,
		       DA732X_EQ_BAND3_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC1 EQ Band 4 Volume", DA732X_REG_DAC1_EQ34,
		       DA732X_EQ_BAND4_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC1 EQ Band 5 Volume", DA732X_REG_DAC1_EQ5,
		       DA732X_EQ_BAND5_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),

	SOC_SINGLE("DAC2 EQ Switch", DA732X_REG_DAC2_EQ5,
		   DA732X_EQ_EN_SHIFT, DA732X_EQ_EN_MAX, DA732X_NO_INVERT),
	SOC_SINGLE_TLV("DAC2 EQ Band 1 Volume", DA732X_REG_DAC2_EQ12,
		       DA732X_EQ_BAND1_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC2 EQ Band 2 Volume", DA732X_REG_DAC2_EQ12,
		       DA732X_EQ_BAND2_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC2 EQ Band 3 Volume", DA732X_REG_DAC2_EQ34,
		       DA732X_EQ_BAND3_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC2 EQ Band 4 Volume", DA732X_REG_DAC2_EQ34,
		       DA732X_EQ_BAND4_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC2 EQ Band 5 Volume", DA732X_REG_DAC2_EQ5,
		       DA732X_EQ_BAND5_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),

	SOC_SINGLE("DAC3 EQ Switch", DA732X_REG_DAC3_EQ5,
		   DA732X_EQ_EN_SHIFT, DA732X_EQ_EN_MAX, DA732X_NO_INVERT),
	SOC_SINGLE_TLV("DAC3 EQ Band 1 Volume", DA732X_REG_DAC3_EQ12,
		       DA732X_EQ_BAND1_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC3 EQ Band 2 Volume", DA732X_REG_DAC3_EQ12,
		       DA732X_EQ_BAND2_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC3 EQ Band 3 Volume", DA732X_REG_DAC3_EQ34,
		       DA732X_EQ_BAND3_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC3 EQ Band 4 Volume", DA732X_REG_DAC3_EQ34,
		       DA732X_EQ_BAND4_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),
	SOC_SINGLE_TLV("DAC3 EQ Band 5 Volume", DA732X_REG_DAC3_EQ5,
		       DA732X_EQ_BAND5_SHIFT, DA732X_EQ_VOL_VAL_MAX,
		       DA732X_INVERT, eq_band_pga_tlv),

	/* Lineout 2 Reciever*/
	SOC_SINGLE("Lineout 2 Switch", DA732X_REG_LIN2, DA732X_LOUT_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("Lineout 2 Volume", DA732X_REG_LIN2,
		       DA732X_LOUT_VOL_SHIFT, DA732X_LOUT_VOL_VAL_MAX,
		       DA732X_NO_INVERT, lin2_pga_tlv),

	/* Lineout 3 SPEAKER*/
	SOC_SINGLE("Lineout 3 Switch", DA732X_REG_LIN3, DA732X_LOUT_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("Lineout 3 Volume", DA732X_REG_LIN3,
		       DA732X_LOUT_VOL_SHIFT, DA732X_LOUT_VOL_VAL_MAX,
		       DA732X_NO_INVERT, lin3_pga_tlv),

	/* Lineout 4 */
	SOC_SINGLE("Lineout 4 Switch", DA732X_REG_LIN4, DA732X_LOUT_MUTE_SHIFT,
		   DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_SINGLE_TLV("Lineout 4 Volume", DA732X_REG_LIN4,
		       DA732X_LOUT_VOL_SHIFT, DA732X_LOUT_VOL_VAL_MAX,
		       DA732X_NO_INVERT, lin4_pga_tlv),

	/* Headphones */
	SOC_DOUBLE_R("Headphone Switch", DA732X_REG_HPR, DA732X_REG_HPL,
		     DA732X_HP_MUTE_SHIFT, DA732X_SWITCH_MAX, DA732X_INVERT),
	SOC_DOUBLE_R_TLV("Headphone Volume", DA732X_REG_HPL_VOL,
			 DA732X_REG_HPR_VOL, DA732X_HP_VOL_SHIFT,
			 DA732X_HP_VOL_VAL_MAX, DA732X_NO_INVERT, hp_pga_tlv),
};

static int da732x_adc_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		switch (w->reg) {
		case DA732X_REG_ADC1_PD:
			snd_soc_component_update_bits(component, DA732X_REG_CLK_EN3,
					    DA732X_ADCA_BB_CLK_EN,
					    DA732X_ADCA_BB_CLK_EN);
			break;
		case DA732X_REG_ADC2_PD:
			snd_soc_component_update_bits(component, DA732X_REG_CLK_EN3,
					    DA732X_ADCC_BB_CLK_EN,
					    DA732X_ADCC_BB_CLK_EN);
			break;
		default:
			return -EINVAL;
		}

		snd_soc_component_update_bits(component, w->reg, DA732X_ADC_RST_MASK,
				    DA732X_ADC_SET_ACT);
		snd_soc_component_update_bits(component, w->reg, DA732X_ADC_PD_MASK,
				    DA732X_ADC_ON);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, w->reg, DA732X_ADC_PD_MASK,
				    DA732X_ADC_OFF);
		snd_soc_component_update_bits(component, w->reg, DA732X_ADC_RST_MASK,
				    DA732X_ADC_SET_RST);

		switch (w->reg) {
		case DA732X_REG_ADC1_PD:
			snd_soc_component_update_bits(component, DA732X_REG_CLK_EN3,
					    DA732X_ADCA_BB_CLK_EN, 0);
			break;
		case DA732X_REG_ADC2_PD:
			snd_soc_component_update_bits(component, DA732X_REG_CLK_EN3,
					    DA732X_ADCC_BB_CLK_EN, 0);
			break;
		default:
			return -EINVAL;
		}

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int da732x_out_pga_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_component_update_bits(component, w->reg,
				    (1 << w->shift) | DA732X_OUT_HIZ_EN,
				    (1 << w->shift) | DA732X_OUT_HIZ_EN);
		break;
	case SND_SOC_DAPM_POST_PMD:
		snd_soc_component_update_bits(component, w->reg,
				    (1 << w->shift) | DA732X_OUT_HIZ_EN,
				    (1 << w->shift) | DA732X_OUT_HIZ_DIS);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const char *adcl_text[] = {
	"AUX1L", "MIC1"
};

static const char *adcr_text[] = {
	"AUX1R", "MIC2", "MIC3"
};

static const char *enable_text[] = {
	"Disabled",
	"Enabled"
};

/* ADC1LMUX */
static SOC_ENUM_SINGLE_DECL(adc1l_enum,
			    DA732X_REG_INP_MUX, DA732X_ADC1L_MUX_SEL_SHIFT,
			    adcl_text);
static const struct snd_kcontrol_new adc1l_mux =
	SOC_DAPM_ENUM("ADC Route", adc1l_enum);

/* ADC1RMUX */
static SOC_ENUM_SINGLE_DECL(adc1r_enum,
			    DA732X_REG_INP_MUX, DA732X_ADC1R_MUX_SEL_SHIFT,
			    adcr_text);
static const struct snd_kcontrol_new adc1r_mux =
	SOC_DAPM_ENUM("ADC Route", adc1r_enum);

/* ADC2LMUX */
static SOC_ENUM_SINGLE_DECL(adc2l_enum,
			    DA732X_REG_INP_MUX, DA732X_ADC2L_MUX_SEL_SHIFT,
			    adcl_text);
static const struct snd_kcontrol_new adc2l_mux =
	SOC_DAPM_ENUM("ADC Route", adc2l_enum);

/* ADC2RMUX */
static SOC_ENUM_SINGLE_DECL(adc2r_enum,
			    DA732X_REG_INP_MUX, DA732X_ADC2R_MUX_SEL_SHIFT,
			    adcr_text);

static const struct snd_kcontrol_new adc2r_mux =
	SOC_DAPM_ENUM("ADC Route", adc2r_enum);

static SOC_ENUM_SINGLE_DECL(da732x_hp_left_output,
			    DA732X_REG_HPL, DA732X_HP_OUT_DAC_EN_SHIFT,
			    enable_text);

static const struct snd_kcontrol_new hpl_mux =
	SOC_DAPM_ENUM("HPL Switch", da732x_hp_left_output);

static SOC_ENUM_SINGLE_DECL(da732x_hp_right_output,
			    DA732X_REG_HPR, DA732X_HP_OUT_DAC_EN_SHIFT,
			    enable_text);

static const struct snd_kcontrol_new hpr_mux =
	SOC_DAPM_ENUM("HPR Switch", da732x_hp_right_output);

static SOC_ENUM_SINGLE_DECL(da732x_speaker_output,
			    DA732X_REG_LIN3, DA732X_LOUT_DAC_EN_SHIFT,
			    enable_text);

static const struct snd_kcontrol_new spk_mux =
	SOC_DAPM_ENUM("SPK Switch", da732x_speaker_output);

static SOC_ENUM_SINGLE_DECL(da732x_lout4_output,
			    DA732X_REG_LIN4, DA732X_LOUT_DAC_EN_SHIFT,
			    enable_text);

static const struct snd_kcontrol_new lout4_mux =
	SOC_DAPM_ENUM("LOUT4 Switch", da732x_lout4_output);

static SOC_ENUM_SINGLE_DECL(da732x_lout2_output,
			    DA732X_REG_LIN2, DA732X_LOUT_DAC_EN_SHIFT,
			    enable_text);

static const struct snd_kcontrol_new lout2_mux =
	SOC_DAPM_ENUM("LOUT2 Switch", da732x_lout2_output);

static const struct snd_soc_dapm_widget da732x_dapm_widgets[] = {
	/* Supplies */
	SND_SOC_DAPM_SUPPLY("ADC1 Supply", DA732X_REG_ADC1_PD, 0,
			    DA732X_NO_INVERT, da732x_adc_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("ADC2 Supply", DA732X_REG_ADC2_PD, 0,
			    DA732X_NO_INVERT, da732x_adc_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY("DAC1 CLK", DA732X_REG_CLK_EN4,
			    DA732X_DACA_BB_CLK_SHIFT, DA732X_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC2 CLK", DA732X_REG_CLK_EN4,
			    DA732X_DACC_BB_CLK_SHIFT, DA732X_NO_INVERT,
			    NULL, 0),
	SND_SOC_DAPM_SUPPLY("DAC3 CLK", DA732X_REG_CLK_EN5,
			    DA732X_DACE_BB_CLK_SHIFT, DA732X_NO_INVERT,
			    NULL, 0),

	/* Micbias */
	SND_SOC_DAPM_SUPPLY("MICBIAS1", DA732X_REG_MICBIAS1,
			    DA732X_MICBIAS_EN_SHIFT,
			    DA732X_NO_INVERT, NULL, 0),
	SND_SOC_DAPM_SUPPLY("MICBIAS2", DA732X_REG_MICBIAS2,
			    DA732X_MICBIAS_EN_SHIFT,
			    DA732X_NO_INVERT, NULL, 0),

	/* Inputs */
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_INPUT("MIC3"),
	SND_SOC_DAPM_INPUT("AUX1L"),
	SND_SOC_DAPM_INPUT("AUX1R"),

	/* Outputs */
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),
	SND_SOC_DAPM_OUTPUT("LOUTL"),
	SND_SOC_DAPM_OUTPUT("LOUTR"),
	SND_SOC_DAPM_OUTPUT("ClassD"),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC1L", NULL, DA732X_REG_ADC1_SEL,
			 DA732X_ADCL_EN_SHIFT, DA732X_NO_INVERT),
	SND_SOC_DAPM_ADC("ADC1R", NULL, DA732X_REG_ADC1_SEL,
			 DA732X_ADCR_EN_SHIFT, DA732X_NO_INVERT),
	SND_SOC_DAPM_ADC("ADC2L", NULL, DA732X_REG_ADC2_SEL,
			 DA732X_ADCL_EN_SHIFT, DA732X_NO_INVERT),
	SND_SOC_DAPM_ADC("ADC2R", NULL, DA732X_REG_ADC2_SEL,
			 DA732X_ADCR_EN_SHIFT, DA732X_NO_INVERT),

	/* DACs */
	SND_SOC_DAPM_DAC("DAC1L", NULL, DA732X_REG_DAC1_SEL,
			 DA732X_DACL_EN_SHIFT, DA732X_NO_INVERT),
	SND_SOC_DAPM_DAC("DAC1R", NULL, DA732X_REG_DAC1_SEL,
			 DA732X_DACR_EN_SHIFT, DA732X_NO_INVERT),
	SND_SOC_DAPM_DAC("DAC2L", NULL, DA732X_REG_DAC2_SEL,
			 DA732X_DACL_EN_SHIFT, DA732X_NO_INVERT),
	SND_SOC_DAPM_DAC("DAC2R", NULL, DA732X_REG_DAC2_SEL,
			 DA732X_DACR_EN_SHIFT, DA732X_NO_INVERT),
	SND_SOC_DAPM_DAC("DAC3", NULL, DA732X_REG_DAC3_SEL,
			 DA732X_DACL_EN_SHIFT, DA732X_NO_INVERT),

	/* Input Pgas */
	SND_SOC_DAPM_PGA("MIC1 PGA", DA732X_REG_MIC1, DA732X_MIC_EN_SHIFT,
			 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC2 PGA", DA732X_REG_MIC2, DA732X_MIC_EN_SHIFT,
			 0, NULL, 0),
	SND_SOC_DAPM_PGA("MIC3 PGA", DA732X_REG_MIC3, DA732X_MIC_EN_SHIFT,
			 0, NULL, 0),
	SND_SOC_DAPM_PGA("AUX1L PGA", DA732X_REG_AUX1L, DA732X_AUX_EN_SHIFT,
			 0, NULL, 0),
	SND_SOC_DAPM_PGA("AUX1R PGA", DA732X_REG_AUX1R, DA732X_AUX_EN_SHIFT,
			 0, NULL, 0),

	SND_SOC_DAPM_PGA_E("HP Left", DA732X_REG_HPL, DA732X_HP_OUT_EN_SHIFT,
			   0, NULL, 0, da732x_out_pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("HP Right", DA732X_REG_HPR, DA732X_HP_OUT_EN_SHIFT,
			   0, NULL, 0, da732x_out_pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LIN2", DA732X_REG_LIN2, DA732X_LIN_OUT_EN_SHIFT,
			   0, NULL, 0, da732x_out_pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LIN3", DA732X_REG_LIN3, DA732X_LIN_OUT_EN_SHIFT,
			   0, NULL, 0, da732x_out_pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("LIN4", DA732X_REG_LIN4, DA732X_LIN_OUT_EN_SHIFT,
			   0, NULL, 0, da732x_out_pga_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),

	/* MUXs */
	SND_SOC_DAPM_MUX("ADC1 Left MUX", SND_SOC_NOPM, 0, 0, &adc1l_mux),
	SND_SOC_DAPM_MUX("ADC1 Right MUX", SND_SOC_NOPM, 0, 0, &adc1r_mux),
	SND_SOC_DAPM_MUX("ADC2 Left MUX", SND_SOC_NOPM, 0, 0, &adc2l_mux),
	SND_SOC_DAPM_MUX("ADC2 Right MUX", SND_SOC_NOPM, 0, 0, &adc2r_mux),

	SND_SOC_DAPM_MUX("HP Left MUX", SND_SOC_NOPM, 0, 0, &hpl_mux),
	SND_SOC_DAPM_MUX("HP Right MUX", SND_SOC_NOPM, 0, 0, &hpr_mux),
	SND_SOC_DAPM_MUX("Speaker MUX", SND_SOC_NOPM, 0, 0, &spk_mux),
	SND_SOC_DAPM_MUX("LOUT2 MUX", SND_SOC_NOPM, 0, 0, &lout2_mux),
	SND_SOC_DAPM_MUX("LOUT4 MUX", SND_SOC_NOPM, 0, 0, &lout4_mux),

	/* AIF interfaces */
	SND_SOC_DAPM_AIF_OUT("AIFA Output", "AIFA Capture", 0, DA732X_REG_AIFA3,
			     DA732X_AIF_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("AIFA Input", "AIFA Playback", 0, DA732X_REG_AIFA3,
			    DA732X_AIF_EN_SHIFT, 0),

	SND_SOC_DAPM_AIF_OUT("AIFB Output", "AIFB Capture", 0, DA732X_REG_AIFB3,
			     DA732X_AIF_EN_SHIFT, 0),
	SND_SOC_DAPM_AIF_IN("AIFB Input", "AIFB Playback", 0, DA732X_REG_AIFB3,
			    DA732X_AIF_EN_SHIFT, 0),
};

static const struct snd_soc_dapm_route da732x_dapm_routes[] = {
	/* Inputs */
	{"AUX1L PGA", NULL, "AUX1L"},
	{"AUX1R PGA", NULL, "AUX1R"},
	{"MIC1 PGA", NULL, "MIC1"},
	{"MIC2 PGA", NULL, "MIC2"},
	{"MIC3 PGA", NULL, "MIC3"},

	/* Capture Path */
	{"ADC1 Left MUX", "MIC1", "MIC1 PGA"},
	{"ADC1 Left MUX", "AUX1L", "AUX1L PGA"},

	{"ADC1 Right MUX", "AUX1R", "AUX1R PGA"},
	{"ADC1 Right MUX", "MIC2", "MIC2 PGA"},
	{"ADC1 Right MUX", "MIC3", "MIC3 PGA"},

	{"ADC2 Left MUX", "AUX1L", "AUX1L PGA"},
	{"ADC2 Left MUX", "MIC1", "MIC1 PGA"},

	{"ADC2 Right MUX", "AUX1R", "AUX1R PGA"},
	{"ADC2 Right MUX", "MIC2", "MIC2 PGA"},
	{"ADC2 Right MUX", "MIC3", "MIC3 PGA"},

	{"ADC1L", NULL, "ADC1 Supply"},
	{"ADC1R", NULL, "ADC1 Supply"},
	{"ADC2L", NULL, "ADC2 Supply"},
	{"ADC2R", NULL, "ADC2 Supply"},

	{"ADC1L", NULL, "ADC1 Left MUX"},
	{"ADC1R", NULL, "ADC1 Right MUX"},
	{"ADC2L", NULL, "ADC2 Left MUX"},
	{"ADC2R", NULL, "ADC2 Right MUX"},

	{"AIFA Output", NULL, "ADC1L"},
	{"AIFA Output", NULL, "ADC1R"},
	{"AIFB Output", NULL, "ADC2L"},
	{"AIFB Output", NULL, "ADC2R"},

	{"HP Left MUX", "Enabled", "AIFA Input"},
	{"HP Right MUX", "Enabled", "AIFA Input"},
	{"Speaker MUX", "Enabled", "AIFB Input"},
	{"LOUT2 MUX", "Enabled", "AIFB Input"},
	{"LOUT4 MUX", "Enabled", "AIFB Input"},

	{"DAC1L", NULL, "DAC1 CLK"},
	{"DAC1R", NULL, "DAC1 CLK"},
	{"DAC2L", NULL, "DAC2 CLK"},
	{"DAC2R", NULL, "DAC2 CLK"},
	{"DAC3", NULL, "DAC3 CLK"},

	{"DAC1L", NULL, "HP Left MUX"},
	{"DAC1R", NULL, "HP Right MUX"},
	{"DAC2L", NULL, "Speaker MUX"},
	{"DAC2R", NULL, "LOUT4 MUX"},
	{"DAC3", NULL, "LOUT2 MUX"},

	/* Output Pgas */
	{"HP Left", NULL, "DAC1L"},
	{"HP Right", NULL, "DAC1R"},
	{"LIN3", NULL, "DAC2L"},
	{"LIN4", NULL, "DAC2R"},
	{"LIN2", NULL, "DAC3"},

	/* Outputs */
	{"ClassD", NULL, "LIN3"},
	{"LOUTL", NULL, "LIN2"},
	{"LOUTR", NULL, "LIN4"},
	{"HPL", NULL, "HP Left"},
	{"HPR", NULL, "HP Right"},
};

static int da732x_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	u32 aif = 0;
	u32 reg_aif;
	u32 fs;

	reg_aif = dai->driver->base;

	switch (params_width(params)) {
	case 16:
		aif |= DA732X_AIF_WORD_16;
		break;
	case 20:
		aif |= DA732X_AIF_WORD_20;
		break;
	case 24:
		aif |= DA732X_AIF_WORD_24;
		break;
	case 32:
		aif |= DA732X_AIF_WORD_32;
		break;
	default:
		return -EINVAL;
	}

	switch (params_rate(params)) {
	case 8000:
		fs = DA732X_SR_8KHZ;
		break;
	case 11025:
		fs = DA732X_SR_11_025KHZ;
		break;
	case 12000:
		fs = DA732X_SR_12KHZ;
		break;
	case 16000:
		fs = DA732X_SR_16KHZ;
		break;
	case 22050:
		fs = DA732X_SR_22_05KHZ;
		break;
	case 24000:
		fs = DA732X_SR_24KHZ;
		break;
	case 32000:
		fs = DA732X_SR_32KHZ;
		break;
	case 44100:
		fs = DA732X_SR_44_1KHZ;
		break;
	case 48000:
		fs = DA732X_SR_48KHZ;
		break;
	case 88100:
		fs = DA732X_SR_88_1KHZ;
		break;
	case 96000:
		fs = DA732X_SR_96KHZ;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, reg_aif, DA732X_AIF_WORD_MASK, aif);
	snd_soc_component_update_bits(component, DA732X_REG_CLK_CTRL, DA732X_SR1_MASK, fs);

	return 0;
}

static int da732x_set_dai_fmt(struct snd_soc_dai *dai, u32 fmt)
{
	struct snd_soc_component *component = dai->component;
	u32 aif_mclk, pc_count;
	u32 reg_aif1, aif1;
	u32 reg_aif3, aif3;

	switch (dai->id) {
	case DA732X_DAI_ID1:
		reg_aif1 = DA732X_REG_AIFA1;
		reg_aif3 = DA732X_REG_AIFA3;
		pc_count = DA732X_PC_PULSE_AIFA | DA732X_PC_RESYNC_NOT_AUT |
			   DA732X_PC_SAME;
		break;
	case DA732X_DAI_ID2:
		reg_aif1 = DA732X_REG_AIFB1;
		reg_aif3 = DA732X_REG_AIFB3;
		pc_count = DA732X_PC_PULSE_AIFB | DA732X_PC_RESYNC_NOT_AUT |
			   DA732X_PC_SAME;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		aif1 = DA732X_AIF_SLAVE;
		aif_mclk = DA732X_AIFM_FRAME_64 | DA732X_AIFM_SRC_SEL_AIFA;
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		aif1 = DA732X_AIF_CLK_FROM_SRC;
		aif_mclk = DA732X_CLK_GENERATION_AIF_A;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		aif3 = DA732X_AIF_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		aif3 = DA732X_AIF_RIGHT_J_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		aif3 = DA732X_AIF_LEFT_J_MODE;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		aif3 = DA732X_AIF_DSP_MODE;
		break;
	default:
		return -EINVAL;
	}

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_B:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif3 |= DA732X_AIF_BCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	case SND_SOC_DAIFMT_I2S:
	case SND_SOC_DAIFMT_RIGHT_J:
	case SND_SOC_DAIFMT_LEFT_J:
		switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
		case SND_SOC_DAIFMT_NB_NF:
			break;
		case SND_SOC_DAIFMT_IB_IF:
			aif3 |= DA732X_AIF_BCLK_INV | DA732X_AIF_WCLK_INV;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			aif3 |= DA732X_AIF_BCLK_INV;
			break;
		case SND_SOC_DAIFMT_NB_IF:
			aif3 |= DA732X_AIF_WCLK_INV;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_write(component, DA732X_REG_AIF_MCLK, aif_mclk);
	snd_soc_component_update_bits(component, reg_aif1, DA732X_AIF1_CLK_MASK, aif1);
	snd_soc_component_update_bits(component, reg_aif3, DA732X_AIF_BCLK_INV |
			    DA732X_AIF_WCLK_INV | DA732X_AIF_MODE_MASK, aif3);
	snd_soc_component_write(component, DA732X_REG_PC_CTRL, pc_count);

	return 0;
}



static int da732x_set_dai_pll(struct snd_soc_component *component, int pll_id,
			      int source, unsigned int freq_in,
			      unsigned int freq_out)
{
	struct da732x_priv *da732x = snd_soc_component_get_drvdata(component);
	int fref, indiv;
	u8 div_lo, div_mid, div_hi;
	u64 frac_div;

	/* Disable PLL */
	if (freq_out == 0) {
		snd_soc_component_update_bits(component, DA732X_REG_PLL_CTRL,
				    DA732X_PLL_EN, 0);
		da732x->pll_en = false;
		return 0;
	}

	if (da732x->pll_en)
		return -EBUSY;

	if (source == DA732X_SRCCLK_MCLK) {
		/* Validate Sysclk rate */
		switch (da732x->sysclk) {
		case 11290000:
		case 12288000:
		case 22580000:
		case 24576000:
		case 45160000:
		case 49152000:
			snd_soc_component_write(component, DA732X_REG_PLL_CTRL,
				      DA732X_PLL_BYPASS);
			return 0;
		default:
			dev_err(component->dev,
				"Cannot use PLL Bypass, invalid SYSCLK rate\n");
			return -EINVAL;
		}
	}

	indiv = da732x_get_input_div(component, da732x->sysclk);
	if (indiv < 0)
		return indiv;

	fref = da732x->sysclk / BIT(indiv);
	div_hi = freq_out / fref;
	frac_div = (u64)(freq_out % fref) * 8192ULL;
	do_div(frac_div, fref);
	div_mid = (frac_div >> DA732X_1BYTE_SHIFT) & DA732X_U8_MASK;
	div_lo = (frac_div) & DA732X_U8_MASK;

	snd_soc_component_write(component, DA732X_REG_PLL_DIV_LO, div_lo);
	snd_soc_component_write(component, DA732X_REG_PLL_DIV_MID, div_mid);
	snd_soc_component_write(component, DA732X_REG_PLL_DIV_HI, div_hi);

	snd_soc_component_update_bits(component, DA732X_REG_PLL_CTRL, DA732X_PLL_EN,
			    DA732X_PLL_EN);

	da732x->pll_en = true;

	return 0;
}

static int da732x_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				 unsigned int freq, int dir)
{
	struct snd_soc_component *component = dai->component;
	struct da732x_priv *da732x = snd_soc_component_get_drvdata(component);

	da732x->sysclk = freq;

	return 0;
}

#define DA732X_RATES	SNDRV_PCM_RATE_8000_96000

#define	DA732X_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops da732x_dai_ops = {
	.hw_params	= da732x_hw_params,
	.set_fmt	= da732x_set_dai_fmt,
	.set_sysclk	= da732x_set_dai_sysclk,
};

static struct snd_soc_dai_driver da732x_dai[] = {
	{
		.name	= "DA732X_AIFA",
		.id	= DA732X_DAI_ID1,
		.base	= DA732X_REG_AIFA1,
		.playback = {
			.stream_name = "AIFA Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = DA732X_RATES,
			.formats = DA732X_FORMATS,
		},
		.capture = {
			.stream_name = "AIFA Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = DA732X_RATES,
			.formats = DA732X_FORMATS,
		},
		.ops = &da732x_dai_ops,
	},
	{
		.name	= "DA732X_AIFB",
		.id	= DA732X_DAI_ID2,
		.base	= DA732X_REG_AIFB1,
		.playback = {
			.stream_name = "AIFB Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = DA732X_RATES,
			.formats = DA732X_FORMATS,
		},
		.capture = {
			.stream_name = "AIFB Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = DA732X_RATES,
			.formats = DA732X_FORMATS,
		},
		.ops = &da732x_dai_ops,
	},
};

static bool da732x_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case DA732X_REG_HPL_DAC_OFF_CNTL:
	case DA732X_REG_HPR_DAC_OFF_CNTL:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config da732x_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,

	.max_register		= DA732X_MAX_REG,
	.volatile_reg		= da732x_volatile,
	.reg_defaults		= da732x_reg_cache,
	.num_reg_defaults	= ARRAY_SIZE(da732x_reg_cache),
	.cache_type		= REGCACHE_RBTREE,
};


static void da732x_dac_offset_adjust(struct snd_soc_component *component)
{
	u8 offset[DA732X_HP_DACS];
	u8 sign[DA732X_HP_DACS];
	u8 step = DA732X_DAC_OFFSET_STEP;

	/* Initialize DAC offset calibration circuits and registers */
	snd_soc_component_write(component, DA732X_REG_HPL_DAC_OFFSET,
		      DA732X_HP_DAC_OFFSET_TRIM_VAL);
	snd_soc_component_write(component, DA732X_REG_HPR_DAC_OFFSET,
		      DA732X_HP_DAC_OFFSET_TRIM_VAL);
	snd_soc_component_write(component, DA732X_REG_HPL_DAC_OFF_CNTL,
		      DA732X_HP_DAC_OFF_CALIBRATION |
		      DA732X_HP_DAC_OFF_SCALE_STEPS);
	snd_soc_component_write(component, DA732X_REG_HPR_DAC_OFF_CNTL,
		      DA732X_HP_DAC_OFF_CALIBRATION |
		      DA732X_HP_DAC_OFF_SCALE_STEPS);

	/* Wait for voltage stabilization */
	msleep(DA732X_WAIT_FOR_STABILIZATION);

	/* Check DAC offset sign */
	sign[DA732X_HPL_DAC] = (snd_soc_component_read(component, DA732X_REG_HPL_DAC_OFF_CNTL) &
				DA732X_HP_DAC_OFF_CNTL_COMPO);
	sign[DA732X_HPR_DAC] = (snd_soc_component_read(component, DA732X_REG_HPR_DAC_OFF_CNTL) &
				DA732X_HP_DAC_OFF_CNTL_COMPO);

	/* Binary search DAC offset values (both channels at once) */
	offset[DA732X_HPL_DAC] = sign[DA732X_HPL_DAC] << DA732X_HP_DAC_COMPO_SHIFT;
	offset[DA732X_HPR_DAC] = sign[DA732X_HPR_DAC] << DA732X_HP_DAC_COMPO_SHIFT;

	do {
		offset[DA732X_HPL_DAC] |= step;
		offset[DA732X_HPR_DAC] |= step;
		snd_soc_component_write(component, DA732X_REG_HPL_DAC_OFFSET,
			      ~offset[DA732X_HPL_DAC] & DA732X_HP_DAC_OFF_MASK);
		snd_soc_component_write(component, DA732X_REG_HPR_DAC_OFFSET,
			      ~offset[DA732X_HPR_DAC] & DA732X_HP_DAC_OFF_MASK);

		msleep(DA732X_WAIT_FOR_STABILIZATION);

		if ((snd_soc_component_read(component, DA732X_REG_HPL_DAC_OFF_CNTL) &
		     DA732X_HP_DAC_OFF_CNTL_COMPO) ^ sign[DA732X_HPL_DAC])
			offset[DA732X_HPL_DAC] &= ~step;
		if ((snd_soc_component_read(component, DA732X_REG_HPR_DAC_OFF_CNTL) &
		     DA732X_HP_DAC_OFF_CNTL_COMPO) ^ sign[DA732X_HPR_DAC])
			offset[DA732X_HPR_DAC] &= ~step;

		step >>= 1;
	} while (step);

	/* Write final DAC offsets to registers */
	snd_soc_component_write(component, DA732X_REG_HPL_DAC_OFFSET,
		      ~offset[DA732X_HPL_DAC] &	DA732X_HP_DAC_OFF_MASK);
	snd_soc_component_write(component, DA732X_REG_HPR_DAC_OFFSET,
		      ~offset[DA732X_HPR_DAC] &	DA732X_HP_DAC_OFF_MASK);

	/* End DAC calibration mode */
	snd_soc_component_write(component, DA732X_REG_HPL_DAC_OFF_CNTL,
		DA732X_HP_DAC_OFF_SCALE_STEPS);
	snd_soc_component_write(component, DA732X_REG_HPR_DAC_OFF_CNTL,
		DA732X_HP_DAC_OFF_SCALE_STEPS);
}

static void da732x_output_offset_adjust(struct snd_soc_component *component)
{
	u8 offset[DA732X_HP_AMPS];
	u8 sign[DA732X_HP_AMPS];
	u8 step = DA732X_OUTPUT_OFFSET_STEP;

	offset[DA732X_HPL_AMP] = DA732X_HP_OUT_TRIM_VAL;
	offset[DA732X_HPR_AMP] = DA732X_HP_OUT_TRIM_VAL;

	/* Initialize output offset calibration circuits and registers  */
	snd_soc_component_write(component, DA732X_REG_HPL_OUT_OFFSET, DA732X_HP_OUT_TRIM_VAL);
	snd_soc_component_write(component, DA732X_REG_HPR_OUT_OFFSET, DA732X_HP_OUT_TRIM_VAL);
	snd_soc_component_write(component, DA732X_REG_HPL,
		      DA732X_HP_OUT_COMP | DA732X_HP_OUT_EN);
	snd_soc_component_write(component, DA732X_REG_HPR,
		      DA732X_HP_OUT_COMP | DA732X_HP_OUT_EN);

	/* Wait for voltage stabilization */
	msleep(DA732X_WAIT_FOR_STABILIZATION);

	/* Check output offset sign */
	sign[DA732X_HPL_AMP] = snd_soc_component_read(component, DA732X_REG_HPL) &
			       DA732X_HP_OUT_COMPO;
	sign[DA732X_HPR_AMP] = snd_soc_component_read(component, DA732X_REG_HPR) &
			       DA732X_HP_OUT_COMPO;

	snd_soc_component_write(component, DA732X_REG_HPL, DA732X_HP_OUT_COMP |
		      (sign[DA732X_HPL_AMP] >> DA732X_HP_OUT_COMPO_SHIFT) |
		      DA732X_HP_OUT_EN);
	snd_soc_component_write(component, DA732X_REG_HPR, DA732X_HP_OUT_COMP |
		      (sign[DA732X_HPR_AMP] >> DA732X_HP_OUT_COMPO_SHIFT) |
		      DA732X_HP_OUT_EN);

	/* Binary search output offset values (both channels at once) */
	do {
		offset[DA732X_HPL_AMP] |= step;
		offset[DA732X_HPR_AMP] |= step;
		snd_soc_component_write(component, DA732X_REG_HPL_OUT_OFFSET,
			      offset[DA732X_HPL_AMP]);
		snd_soc_component_write(component, DA732X_REG_HPR_OUT_OFFSET,
			      offset[DA732X_HPR_AMP]);

		msleep(DA732X_WAIT_FOR_STABILIZATION);

		if ((snd_soc_component_read(component, DA732X_REG_HPL) &
		     DA732X_HP_OUT_COMPO) ^ sign[DA732X_HPL_AMP])
			offset[DA732X_HPL_AMP] &= ~step;
		if ((snd_soc_component_read(component, DA732X_REG_HPR) &
		     DA732X_HP_OUT_COMPO) ^ sign[DA732X_HPR_AMP])
			offset[DA732X_HPR_AMP] &= ~step;

		step >>= 1;
	} while (step);

	/* Write final DAC offsets to registers */
	snd_soc_component_write(component, DA732X_REG_HPL_OUT_OFFSET, offset[DA732X_HPL_AMP]);
	snd_soc_component_write(component, DA732X_REG_HPR_OUT_OFFSET, offset[DA732X_HPR_AMP]);
}

static void da732x_hp_dc_offset_cancellation(struct snd_soc_component *component)
{
	/* Make sure that we have Soft Mute enabled */
	snd_soc_component_write(component, DA732X_REG_DAC1_SOFTMUTE, DA732X_SOFTMUTE_EN |
		      DA732X_GAIN_RAMPED | DA732X_16_SAMPLES);
	snd_soc_component_write(component, DA732X_REG_DAC1_SEL, DA732X_DACL_EN |
		      DA732X_DACR_EN | DA732X_DACL_SDM | DA732X_DACR_SDM |
		      DA732X_DACL_MUTE | DA732X_DACR_MUTE);
	snd_soc_component_write(component, DA732X_REG_HPL, DA732X_HP_OUT_DAC_EN |
		      DA732X_HP_OUT_MUTE | DA732X_HP_OUT_EN);
	snd_soc_component_write(component, DA732X_REG_HPR, DA732X_HP_OUT_EN |
		      DA732X_HP_OUT_MUTE | DA732X_HP_OUT_DAC_EN);

	da732x_dac_offset_adjust(component);
	da732x_output_offset_adjust(component);

	snd_soc_component_write(component, DA732X_REG_DAC1_SEL, DA732X_DACS_DIS);
	snd_soc_component_write(component, DA732X_REG_HPL, DA732X_HP_DIS);
	snd_soc_component_write(component, DA732X_REG_HPR, DA732X_HP_DIS);
}

static int da732x_set_bias_level(struct snd_soc_component *component,
				 enum snd_soc_bias_level level)
{
	struct da732x_priv *da732x = snd_soc_component_get_drvdata(component);

	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_component_update_bits(component, DA732X_REG_BIAS_EN,
				    DA732X_BIAS_BOOST_MASK,
				    DA732X_BIAS_BOOST_100PC);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (snd_soc_component_get_bias_level(component) == SND_SOC_BIAS_OFF) {
			/* Init Codec */
			snd_soc_component_write(component, DA732X_REG_REF1,
				      DA732X_VMID_FASTCHG);
			snd_soc_component_write(component, DA732X_REG_BIAS_EN,
				      DA732X_BIAS_EN);

			mdelay(DA732X_STARTUP_DELAY);

			/* Disable Fast Charge and enable DAC ref voltage */
			snd_soc_component_write(component, DA732X_REG_REF1,
				      DA732X_REFBUFX2_EN);

			/* Enable bypass DSP routing */
			snd_soc_component_write(component, DA732X_REG_DATA_ROUTE,
				      DA732X_BYPASS_DSP);

			/* Enable Digital subsystem */
			snd_soc_component_write(component, DA732X_REG_DSP_CTRL,
				      DA732X_DIGITAL_EN);

			snd_soc_component_write(component, DA732X_REG_SPARE1_OUT,
				      DA732X_HP_DRIVER_EN |
				      DA732X_HP_GATE_LOW |
				      DA732X_HP_LOOP_GAIN_CTRL);
			snd_soc_component_write(component, DA732X_REG_HP_LIN1_GNDSEL,
				      DA732X_HP_OUT_GNDSEL);

			da732x_set_charge_pump(component, DA732X_ENABLE_CP);

			snd_soc_component_write(component, DA732X_REG_CLK_EN1,
			      DA732X_SYS3_CLK_EN | DA732X_PC_CLK_EN);

			/* Enable Zero Crossing */
			snd_soc_component_write(component, DA732X_REG_INP_ZC_EN,
				      DA732X_MIC1_PRE_ZC_EN |
				      DA732X_MIC1_ZC_EN |
				      DA732X_MIC2_PRE_ZC_EN |
				      DA732X_MIC2_ZC_EN |
				      DA732X_AUXL_ZC_EN |
				      DA732X_AUXR_ZC_EN |
				      DA732X_MIC3_PRE_ZC_EN |
				      DA732X_MIC3_ZC_EN);
			snd_soc_component_write(component, DA732X_REG_OUT_ZC_EN,
				      DA732X_HPL_ZC_EN | DA732X_HPR_ZC_EN |
				      DA732X_LIN2_ZC_EN | DA732X_LIN3_ZC_EN |
				      DA732X_LIN4_ZC_EN);

			da732x_hp_dc_offset_cancellation(component);

			regcache_cache_only(da732x->regmap, false);
			regcache_sync(da732x->regmap);
		} else {
			snd_soc_component_update_bits(component, DA732X_REG_BIAS_EN,
					    DA732X_BIAS_BOOST_MASK,
					    DA732X_BIAS_BOOST_50PC);
			snd_soc_component_update_bits(component, DA732X_REG_PLL_CTRL,
					    DA732X_PLL_EN, 0);
			da732x->pll_en = false;
		}
		break;
	case SND_SOC_BIAS_OFF:
		regcache_cache_only(da732x->regmap, true);
		da732x_set_charge_pump(component, DA732X_DISABLE_CP);
		snd_soc_component_update_bits(component, DA732X_REG_BIAS_EN, DA732X_BIAS_EN,
				    DA732X_BIAS_DIS);
		da732x->pll_en = false;
		break;
	}

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_da732x = {
	.set_bias_level		= da732x_set_bias_level,
	.controls		= da732x_snd_controls,
	.num_controls		= ARRAY_SIZE(da732x_snd_controls),
	.dapm_widgets		= da732x_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(da732x_dapm_widgets),
	.dapm_routes		= da732x_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(da732x_dapm_routes),
	.set_pll		= da732x_set_dai_pll,
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int da732x_i2c_probe(struct i2c_client *i2c)
{
	struct da732x_priv *da732x;
	unsigned int reg;
	int ret;

	da732x = devm_kzalloc(&i2c->dev, sizeof(struct da732x_priv),
			      GFP_KERNEL);
	if (!da732x)
		return -ENOMEM;

	i2c_set_clientdata(i2c, da732x);

	da732x->regmap = devm_regmap_init_i2c(i2c, &da732x_regmap);
	if (IS_ERR(da732x->regmap)) {
		ret = PTR_ERR(da732x->regmap);
		dev_err(&i2c->dev, "Failed to initialize regmap\n");
		goto err;
	}

	ret = regmap_read(da732x->regmap, DA732X_REG_ID, &reg);
	if (ret < 0) {
		dev_err(&i2c->dev, "Failed to read ID register: %d\n", ret);
		goto err;
	}

	dev_info(&i2c->dev, "Revision: %d.%d\n",
		 (reg & DA732X_ID_MAJOR_MASK) >> 4,
		 (reg & DA732X_ID_MINOR_MASK));

	ret = devm_snd_soc_register_component(&i2c->dev,
				     &soc_component_dev_da732x,
				     da732x_dai, ARRAY_SIZE(da732x_dai));
	if (ret != 0)
		dev_err(&i2c->dev, "Failed to register component.\n");

err:
	return ret;
}

static int da732x_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id da732x_i2c_id[] = {
	{ "da7320", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, da732x_i2c_id);

static struct i2c_driver da732x_i2c_driver = {
	.driver		= {
		.name	= "da7320",
	},
	.probe_new	= da732x_i2c_probe,
	.remove		= da732x_i2c_remove,
	.id_table	= da732x_i2c_id,
};

module_i2c_driver(da732x_i2c_driver);


MODULE_DESCRIPTION("ASoC DA732X driver");
MODULE_AUTHOR("Michal Hajduk <michal.hajduk@diasemi.com>");
MODULE_LICENSE("GPL");
