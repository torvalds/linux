// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio Misc Control
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "../common/mtk-afe-fe-dai.h"
#include "../common/mtk-afe-platform-driver.h"
#include "mt8186-afe-common.h"

static const char * const mt8186_sgen_mode_str[] = {
	"I0I1",   "I2",     "I3I4",   "I5I6",
	"I7I8",   "I9I22",  "I10I11", "I12I13",
	"I14I21", "I15I16", "I17I18", "I19I20",
	"I23I24", "I25I26", "I27I28", "I33",
	"I34I35", "I36I37", "I38I39", "I40I41",
	"I42I43", "I44I45", "I46I47", "I48I49",
	"I56I57", "I58I59", "I60I61", "I62I63",
	"O0O1",   "O2",     "O3O4",   "O5O6",
	"O7O8",   "O9O10",  "O11",    "O12",
	"O13O14", "O15O16", "O17O18", "O19O20",
	"O21O22", "O23O24", "O25",    "O28O29",
	"O34",    "O35",    "O32O33", "O36O37",
	"O38O39", "O30O31", "O40O41", "O42O43",
	"O44O45", "O46O47", "O48O49", "O50O51",
	"O58O59", "O60O61", "O62O63", "O64O65",
	"O66O67", "O68O69", "O26O27", "OFF",
};

static const int mt8186_sgen_mode_idx[] = {
	0, 2, 4, 6,
	8, 22, 10, 12,
	14, -1, 18, 20,
	24, 26, 28, 33,
	34, 36, 38, 40,
	42, 44, 46, 48,
	56, 58, 60, 62,
	128, 130, 132, 134,
	135, 138, 139, 140,
	142, 144, 166, 148,
	150, 152, 153, 156,
	162, 163, 160, 164,
	166, -1, 168, 170,
	172, 174, 176, 178,
	186, 188, 190, 192,
	194, 196, -1, -1,
};

static const char * const mt8186_sgen_rate_str[] = {
	"8K", "11K", "12K", "16K",
	"22K", "24K", "32K", "44K",
	"48K", "88k", "96k", "176k",
	"192k"
};

static const int mt8186_sgen_rate_idx[] = {
	0, 1, 2, 4,
	5, 6, 8, 9,
	10, 11, 12, 13,
	14
};

/* this order must match reg bit amp_div_ch1/2 */
static const char * const mt8186_sgen_amp_str[] = {
	"1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1" };

static int mt8186_sgen_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->sgen_mode;

	return 0;
}

static int mt8186_sgen_set(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int mode;
	int mode_idx;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	mode = ucontrol->value.integer.value[0];
	mode_idx = mt8186_sgen_mode_idx[mode];

	dev_dbg(afe->dev, "%s(), mode %d, mode_idx %d\n",
		__func__, mode, mode_idx);

	if (mode == afe_priv->sgen_mode)
		return 0;

	if (mode_idx >= 0) {
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON2,
				   INNER_LOOP_BACK_MODE_MASK_SFT,
				   mode_idx << INNER_LOOP_BACK_MODE_SFT);
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON0,
				   DAC_EN_MASK_SFT, BIT(DAC_EN_SFT));
	} else {
		/* disable sgen */
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON0,
				   DAC_EN_MASK_SFT, 0);
		regmap_update_bits(afe->regmap, AFE_SINEGEN_CON2,
				   INNER_LOOP_BACK_MODE_MASK_SFT,
				   0x3f << INNER_LOOP_BACK_MODE_SFT);
	}

	afe_priv->sgen_mode = mode;

	return 1;
}

static int mt8186_sgen_rate_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->sgen_rate;

	return 0;
}

static int mt8186_sgen_rate_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int rate;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	rate = ucontrol->value.integer.value[0];

	dev_dbg(afe->dev, "%s(), rate %d\n", __func__, rate);

	if (rate == afe_priv->sgen_rate)
		return 0;

	regmap_update_bits(afe->regmap, AFE_SINEGEN_CON0,
			   SINE_MODE_CH1_MASK_SFT,
			   mt8186_sgen_rate_idx[rate] << SINE_MODE_CH1_SFT);

	regmap_update_bits(afe->regmap, AFE_SINEGEN_CON0,
			   SINE_MODE_CH2_MASK_SFT,
			   mt8186_sgen_rate_idx[rate] << SINE_MODE_CH2_SFT);

	afe_priv->sgen_rate = rate;

	return 1;
}

static int mt8186_sgen_amplitude_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->sgen_amplitude;
	return 0;
}

static int mt8186_sgen_amplitude_set(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int amplitude;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	amplitude = ucontrol->value.integer.value[0];
	if (amplitude > AMP_DIV_CH1_MASK) {
		dev_err(afe->dev, "%s(), amplitude %d invalid\n",
			__func__, amplitude);
		return -EINVAL;
	}

	dev_dbg(afe->dev, "%s(), amplitude %d\n", __func__, amplitude);

	if (amplitude == afe_priv->sgen_amplitude)
		return 0;

	regmap_update_bits(afe->regmap, AFE_SINEGEN_CON0,
			   AMP_DIV_CH1_MASK_SFT,
			   amplitude << AMP_DIV_CH1_SFT);
	regmap_update_bits(afe->regmap, AFE_SINEGEN_CON0,
			   AMP_DIV_CH2_MASK_SFT,
			   amplitude << AMP_DIV_CH2_SFT);

	afe_priv->sgen_amplitude = amplitude;

	return 1;
}

static const struct soc_enum mt8186_afe_sgen_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mt8186_sgen_mode_str),
			    mt8186_sgen_mode_str),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mt8186_sgen_rate_str),
			    mt8186_sgen_rate_str),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mt8186_sgen_amp_str),
			    mt8186_sgen_amp_str),
};

static const struct snd_kcontrol_new mt8186_afe_sgen_controls[] = {
	SOC_ENUM_EXT("Audio_SineGen_Switch", mt8186_afe_sgen_enum[0],
		     mt8186_sgen_get, mt8186_sgen_set),
	SOC_ENUM_EXT("Audio_SineGen_SampleRate", mt8186_afe_sgen_enum[1],
		     mt8186_sgen_rate_get, mt8186_sgen_rate_set),
	SOC_ENUM_EXT("Audio_SineGen_Amplitude", mt8186_afe_sgen_enum[2],
		     mt8186_sgen_amplitude_get, mt8186_sgen_amplitude_set),
	SOC_SINGLE("Audio_SineGen_Mute_Ch1", AFE_SINEGEN_CON0,
		   MUTE_SW_CH1_MASK_SFT, MUTE_SW_CH1_MASK, 0),
	SOC_SINGLE("Audio_SineGen_Mute_Ch2", AFE_SINEGEN_CON0,
		   MUTE_SW_CH2_MASK_SFT, MUTE_SW_CH2_MASK, 0),
	SOC_SINGLE("Audio_SineGen_Freq_Div_Ch1", AFE_SINEGEN_CON0,
		   FREQ_DIV_CH1_SFT, FREQ_DIV_CH1_MASK, 0),
	SOC_SINGLE("Audio_SineGen_Freq_Div_Ch2", AFE_SINEGEN_CON0,
		   FREQ_DIV_CH2_SFT, FREQ_DIV_CH2_MASK, 0),
};

int mt8186_add_misc_control(struct snd_soc_component *component)
{
	snd_soc_add_component_controls(component,
				       mt8186_afe_sgen_controls,
				       ARRAY_SIZE(mt8186_afe_sgen_controls));

	return 0;
}
