// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI I2S Control
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/bitops.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt8186-afe-clk.h"
#include "mt8186-afe-common.h"
#include "mt8186-afe-gpio.h"
#include "mt8186-interconnection.h"

enum {
	I2S_FMT_EIAJ = 0,
	I2S_FMT_I2S = 1,
};

enum {
	I2S_WLEN_16_BIT = 0,
	I2S_WLEN_32_BIT = 1,
};

enum {
	I2S_HD_NORMAL = 0,
	I2S_HD_LOW_JITTER = 1,
};

enum {
	I2S1_SEL_O28_O29 = 0,
	I2S1_SEL_O03_O04 = 1,
};

enum {
	I2S_IN_PAD_CONNSYS = 0,
	I2S_IN_PAD_IO_MUX = 1,
};

struct mtk_afe_i2s_priv {
	int id;
	int rate; /* for determine which apll to use */
	int low_jitter_en;
	int master; /* only i2s0 has slave mode*/

	int share_i2s_id;

	int mclk_id;
	int mclk_rate;
	int mclk_apll;
};

static unsigned int get_i2s_wlen(snd_pcm_format_t format)
{
	return snd_pcm_format_physical_width(format) <= 16 ?
	       I2S_WLEN_16_BIT : I2S_WLEN_32_BIT;
}

#define MTK_AFE_I2S0_KCONTROL_NAME "I2S0_HD_Mux"
#define MTK_AFE_I2S1_KCONTROL_NAME "I2S1_HD_Mux"
#define MTK_AFE_I2S2_KCONTROL_NAME "I2S2_HD_Mux"
#define MTK_AFE_I2S3_KCONTROL_NAME "I2S3_HD_Mux"
#define MTK_AFE_I2S0_SRC_KCONTROL_NAME "I2S0_SRC_Mux"

#define I2S0_HD_EN_W_NAME "I2S0_HD_EN"
#define I2S1_HD_EN_W_NAME "I2S1_HD_EN"
#define I2S2_HD_EN_W_NAME "I2S2_HD_EN"
#define I2S3_HD_EN_W_NAME "I2S3_HD_EN"

#define I2S0_MCLK_EN_W_NAME "I2S0_MCLK_EN"
#define I2S1_MCLK_EN_W_NAME "I2S1_MCLK_EN"
#define I2S2_MCLK_EN_W_NAME "I2S2_MCLK_EN"
#define I2S3_MCLK_EN_W_NAME "I2S3_MCLK_EN"

static int get_i2s_id_by_name(struct mtk_base_afe *afe,
			      const char *name)
{
	if (strncmp(name, "I2S0", 4) == 0)
		return MT8186_DAI_I2S_0;
	else if (strncmp(name, "I2S1", 4) == 0)
		return MT8186_DAI_I2S_1;
	else if (strncmp(name, "I2S2", 4) == 0)
		return MT8186_DAI_I2S_2;
	else if (strncmp(name, "I2S3", 4) == 0)
		return MT8186_DAI_I2S_3;

	return -EINVAL;
}

static struct mtk_afe_i2s_priv *get_i2s_priv_by_name(struct mtk_base_afe *afe,
						     const char *name)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_i2s_id_by_name(afe, name);

	if (dai_id < 0)
		return NULL;

	return afe_priv->dai_priv[dai_id];
}

/* low jitter control */
static const char * const mt8186_i2s_hd_str[] = {
	"Normal", "Low_Jitter"
};

static const struct soc_enum mt8186_i2s_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mt8186_i2s_hd_str),
			    mt8186_i2s_hd_str),
};

static int mt8186_i2s_hd_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, kcontrol->id.name);
	ucontrol->value.integer.value[0] = i2s_priv->low_jitter_en;

	return 0;
}

static int mt8186_i2s_hd_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int hd_en;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	hd_en = ucontrol->value.integer.value[0];

	dev_dbg(afe->dev, "%s(), kcontrol name %s, hd_en %d\n",
		__func__, kcontrol->id.name, hd_en);

	i2s_priv = get_i2s_priv_by_name(afe, kcontrol->id.name);
	if (i2s_priv->low_jitter_en == hd_en)
		return 0;

	i2s_priv->low_jitter_en = hd_en;

	return 1;
}

static const struct snd_kcontrol_new mtk_dai_i2s_controls[] = {
	SOC_ENUM_EXT(MTK_AFE_I2S0_KCONTROL_NAME, mt8186_i2s_enum[0],
		     mt8186_i2s_hd_get, mt8186_i2s_hd_set),
	SOC_ENUM_EXT(MTK_AFE_I2S1_KCONTROL_NAME, mt8186_i2s_enum[0],
		     mt8186_i2s_hd_get, mt8186_i2s_hd_set),
	SOC_ENUM_EXT(MTK_AFE_I2S2_KCONTROL_NAME, mt8186_i2s_enum[0],
		     mt8186_i2s_hd_get, mt8186_i2s_hd_set),
	SOC_ENUM_EXT(MTK_AFE_I2S3_KCONTROL_NAME, mt8186_i2s_enum[0],
		     mt8186_i2s_hd_get, mt8186_i2s_hd_set),
};

/* dai component */
/* i2s virtual mux to output widget */
static const char * const i2s_mux_map[] = {
	"Normal", "Dummy_Widget",
};

static int i2s_mux_map_value[] = {
	0, 1,
};

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(i2s_mux_map_enum,
					      SND_SOC_NOPM,
					      0,
					      1,
					      i2s_mux_map,
					      i2s_mux_map_value);

static const struct snd_kcontrol_new i2s0_in_mux_control =
	SOC_DAPM_ENUM("I2S0 In Select", i2s_mux_map_enum);

static const struct snd_kcontrol_new i2s1_out_mux_control =
	SOC_DAPM_ENUM("I2S1 Out Select", i2s_mux_map_enum);

static const struct snd_kcontrol_new i2s2_in_mux_control =
	SOC_DAPM_ENUM("I2S2 In Select", i2s_mux_map_enum);

static const struct snd_kcontrol_new i2s3_out_mux_control =
	SOC_DAPM_ENUM("I2S3 Out Select", i2s_mux_map_enum);

/* i2s in lpbk */
static const char * const i2s_lpbk_mux_map[] = {
	"Normal", "Lpbk",
};

static int i2s_lpbk_mux_map_value[] = {
	0, 1,
};

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(i2s0_lpbk_mux_map_enum,
					      AFE_I2S_CON,
					      I2S_LOOPBACK_SFT,
					      1,
					      i2s_lpbk_mux_map,
					      i2s_lpbk_mux_map_value);

static const struct snd_kcontrol_new i2s0_lpbk_mux_control =
	SOC_DAPM_ENUM("I2S Lpbk Select", i2s0_lpbk_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(i2s2_lpbk_mux_map_enum,
					      AFE_I2S_CON2,
					      I2S3_LOOPBACK_SFT,
					      1,
					      i2s_lpbk_mux_map,
					      i2s_lpbk_mux_map_value);

static const struct snd_kcontrol_new i2s2_lpbk_mux_control =
	SOC_DAPM_ENUM("I2S Lpbk Select", i2s2_lpbk_mux_map_enum);

/* interconnection */
static const struct snd_kcontrol_new mtk_i2s3_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1 Switch", AFE_CONN0,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1 Switch", AFE_CONN0,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1 Switch", AFE_CONN0,
				    I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH1 Switch", AFE_CONN0,
				    I_DL12_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH3 Switch", AFE_CONN0,
				    I_DL12_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1 Switch", AFE_CONN0_1,
				    I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1 Switch", AFE_CONN0_1,
				    I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1 Switch", AFE_CONN0_1,
				    I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH1 Switch", AFE_CONN0_1,
				    I_DL8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH1 Switch", AFE_CONN0,
				    I_GAIN1_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1 Switch", AFE_CONN0,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2 Switch", AFE_CONN0,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3 Switch", AFE_CONN0,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1 Switch", AFE_CONN0,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH1 Switch", AFE_CONN0_1,
				    I_SRC_1_OUT_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s3_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2 Switch", AFE_CONN1,
				    I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2 Switch", AFE_CONN1,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2 Switch", AFE_CONN1,
				    I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH2 Switch", AFE_CONN1,
				    I_DL12_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH4 Switch", AFE_CONN1,
				    I_DL12_CH4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2 Switch", AFE_CONN1_1,
				    I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2 Switch", AFE_CONN1_1,
				    I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2 Switch", AFE_CONN1_1,
				    I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH2 Switch", AFE_CONN1_1,
				    I_DL8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH2 Switch", AFE_CONN1,
				    I_GAIN1_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1 Switch", AFE_CONN1,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2 Switch", AFE_CONN1,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3 Switch", AFE_CONN1,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2 Switch", AFE_CONN1,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2 Switch", AFE_CONN1,
				    I_PCM_2_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH2 Switch", AFE_CONN1_1,
				    I_SRC_1_OUT_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s1_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1 Switch", AFE_CONN28,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1 Switch", AFE_CONN28,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1 Switch", AFE_CONN28,
				    I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH1 Switch", AFE_CONN28,
				    I_DL12_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH3 Switch", AFE_CONN28,
				    I_DL12_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1 Switch", AFE_CONN28_1,
				    I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1 Switch", AFE_CONN28_1,
				    I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1 Switch", AFE_CONN28_1,
				    I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH1 Switch", AFE_CONN28_1,
				    I_DL8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH1 Switch", AFE_CONN28,
				    I_GAIN1_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1 Switch", AFE_CONN28,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1 Switch", AFE_CONN28,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH1 Switch", AFE_CONN28_1,
				    I_SRC_1_OUT_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s1_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2 Switch", AFE_CONN29,
				    I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2 Switch", AFE_CONN29,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2 Switch", AFE_CONN29,
				    I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH2 Switch", AFE_CONN29,
				    I_DL12_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH4 Switch", AFE_CONN29,
				    I_DL12_CH4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2 Switch", AFE_CONN29_1,
				    I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2 Switch", AFE_CONN29_1,
				    I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2 Switch", AFE_CONN29_1,
				    I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH2 Switch", AFE_CONN29_1,
				    I_DL8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH2 Switch", AFE_CONN29,
				    I_GAIN1_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2 Switch", AFE_CONN29,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2 Switch", AFE_CONN29,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2 Switch", AFE_CONN29,
				    I_PCM_2_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH2 Switch", AFE_CONN29_1,
				    I_SRC_1_OUT_CH2, 1, 0),
};

enum {
	SUPPLY_SEQ_APLL,
	SUPPLY_SEQ_I2S_MCLK_EN,
	SUPPLY_SEQ_I2S_HD_EN,
	SUPPLY_SEQ_I2S_EN,
};

static int mtk_i2s_en_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, w->name);

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8186_afe_gpio_request(afe->dev, true, i2s_priv->id, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		mt8186_afe_gpio_request(afe->dev, false, i2s_priv->id, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_apll_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (snd_soc_dapm_widget_name_cmp(w, APLL1_W_NAME) == 0)
			mt8186_apll1_enable(afe);
		else
			mt8186_apll2_enable(afe);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (snd_soc_dapm_widget_name_cmp(w, APLL1_W_NAME) == 0)
			mt8186_apll1_disable(afe);
		else
			mt8186_apll2_disable(afe);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_mclk_en_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	i2s_priv = get_i2s_priv_by_name(afe, w->name);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8186_mck_enable(afe, i2s_priv->mclk_id, i2s_priv->mclk_rate);
		break;
	case SND_SOC_DAPM_POST_PMD:
		i2s_priv->mclk_rate = 0;
		mt8186_mck_disable(afe, i2s_priv->mclk_id);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget mtk_dai_i2s_widgets[] = {
	SND_SOC_DAPM_INPUT("CONNSYS"),

	SND_SOC_DAPM_MIXER("I2S1_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_i2s1_ch1_mix,
			   ARRAY_SIZE(mtk_i2s1_ch1_mix)),
	SND_SOC_DAPM_MIXER("I2S1_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_i2s1_ch2_mix,
			   ARRAY_SIZE(mtk_i2s1_ch2_mix)),

	SND_SOC_DAPM_MIXER("I2S3_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_i2s3_ch1_mix,
			   ARRAY_SIZE(mtk_i2s3_ch1_mix)),
	SND_SOC_DAPM_MIXER("I2S3_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_i2s3_ch2_mix,
			   ARRAY_SIZE(mtk_i2s3_ch2_mix)),

	/* i2s en*/
	SND_SOC_DAPM_SUPPLY_S("I2S0_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON, I2S_EN_SFT, 0,
			      mtk_i2s_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("I2S1_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON1, I2S_EN_SFT, 0,
			      mtk_i2s_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("I2S2_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON2, I2S_EN_SFT, 0,
			      mtk_i2s_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("I2S3_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON3, I2S_EN_SFT, 0,
			      mtk_i2s_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	/* i2s hd en */
	SND_SOC_DAPM_SUPPLY_S(I2S0_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON, I2S1_HD_EN_SFT, 0, NULL,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2S1_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON1, I2S2_HD_EN_SFT, 0, NULL,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2S2_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON2, I2S3_HD_EN_SFT, 0, NULL,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2S3_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON3, I2S4_HD_EN_SFT, 0, NULL,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* i2s mclk en */
	SND_SOC_DAPM_SUPPLY_S(I2S0_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2S1_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2S2_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(I2S3_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_mclk_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* apll */
	SND_SOC_DAPM_SUPPLY_S(APLL1_W_NAME, SUPPLY_SEQ_APLL,
			      SND_SOC_NOPM, 0, 0,
			      mtk_apll_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S(APLL2_W_NAME, SUPPLY_SEQ_APLL,
			      SND_SOC_NOPM, 0, 0,
			      mtk_apll_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* allow i2s on without codec on */
	SND_SOC_DAPM_OUTPUT("I2S_DUMMY_OUT"),
	SND_SOC_DAPM_MUX("I2S1_Out_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s1_out_mux_control),
	SND_SOC_DAPM_MUX("I2S3_Out_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s3_out_mux_control),
	SND_SOC_DAPM_INPUT("I2S_DUMMY_IN"),
	SND_SOC_DAPM_MUX("I2S0_In_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s0_in_mux_control),
	SND_SOC_DAPM_MUX("I2S2_In_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s2_in_mux_control),

	/* i2s in lpbk */
	SND_SOC_DAPM_MUX("I2S0_Lpbk_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s0_lpbk_mux_control),
	SND_SOC_DAPM_MUX("I2S2_Lpbk_Mux",
			 SND_SOC_NOPM, 0, 0, &i2s2_lpbk_mux_control),
};

static int mtk_afe_i2s_share_connect(struct snd_soc_dapm_widget *source,
				     struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, sink->name);
	if (i2s_priv->share_i2s_id < 0)
		return 0;

	return i2s_priv->share_i2s_id == get_i2s_id_by_name(afe, source->name);
}

static int mtk_afe_i2s_hd_connect(struct snd_soc_dapm_widget *source,
				  struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, sink->name);
	if (get_i2s_id_by_name(afe, sink->name) ==
	    get_i2s_id_by_name(afe, source->name))
		return i2s_priv->low_jitter_en;

	/* check if share i2s need hd en */
	if (i2s_priv->share_i2s_id < 0)
		return 0;

	if (i2s_priv->share_i2s_id == get_i2s_id_by_name(afe, source->name))
		return i2s_priv->low_jitter_en;

	return 0;
}

static int mtk_afe_i2s_apll_connect(struct snd_soc_dapm_widget *source,
				    struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;
	int cur_apll;
	int i2s_need_apll;

	i2s_priv = get_i2s_priv_by_name(afe, w->name);
	/* which apll */
	cur_apll = mt8186_get_apll_by_name(afe, source->name);
	/* choose APLL from i2s rate */
	i2s_need_apll = mt8186_get_apll_by_rate(afe, i2s_priv->rate);

	return (i2s_need_apll == cur_apll) ? 1 : 0;
}

static int mtk_afe_i2s_mclk_connect(struct snd_soc_dapm_widget *source,
				    struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, sink->name);
	if (get_i2s_id_by_name(afe, sink->name) ==
	    get_i2s_id_by_name(afe, source->name))
		return (i2s_priv->mclk_rate > 0) ? 1 : 0;

	/* check if share i2s need mclk */
	if (i2s_priv->share_i2s_id < 0)
		return 0;

	if (i2s_priv->share_i2s_id == get_i2s_id_by_name(afe, source->name))
		return (i2s_priv->mclk_rate > 0) ? 1 : 0;

	return 0;
}

static int mtk_afe_mclk_apll_connect(struct snd_soc_dapm_widget *source,
				     struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;
	int cur_apll;

	i2s_priv = get_i2s_priv_by_name(afe, w->name);
	/* which apll */
	cur_apll = mt8186_get_apll_by_name(afe, source->name);

	return (i2s_priv->mclk_apll == cur_apll) ? 1 : 0;
}

static const struct snd_soc_dapm_route mtk_dai_i2s_routes[] = {
	{"Connsys I2S", NULL, "CONNSYS"},

	/* i2s0 */
	{"I2S0", NULL, "I2S0_EN"},
	{"I2S0", NULL, "I2S1_EN", mtk_afe_i2s_share_connect},
	{"I2S0", NULL, "I2S2_EN", mtk_afe_i2s_share_connect},
	{"I2S0", NULL, "I2S3_EN", mtk_afe_i2s_share_connect},

	{"I2S0", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S0", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S0", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S0", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S0_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S0_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S0", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S0", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S0", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S0", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S0_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S0_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* i2s1 */
	{"I2S1_CH1", "DL1_CH1 Switch", "DL1"},
	{"I2S1_CH2", "DL1_CH2 Switch", "DL1"},

	{"I2S1_CH1", "DL1_CH1 Switch", "DSP_DL1_VIRT"},
	{"I2S1_CH2", "DL1_CH2 Switch", "DSP_DL1_VIRT"},

	{"I2S1_CH1", "DL2_CH1 Switch", "DL2"},
	{"I2S1_CH2", "DL2_CH2 Switch", "DL2"},

	{"I2S1_CH1", "DL2_CH1 Switch", "DSP_DL2_VIRT"},
	{"I2S1_CH2", "DL2_CH2 Switch", "DSP_DL2_VIRT"},

	{"I2S1_CH1", "DL3_CH1 Switch", "DL3"},
	{"I2S1_CH2", "DL3_CH2 Switch", "DL3"},

	{"I2S1_CH1", "DL12_CH1 Switch", "DL12"},
	{"I2S1_CH2", "DL12_CH2 Switch", "DL12"},

	{"I2S1_CH1", "DL12_CH3 Switch", "DL12"},
	{"I2S1_CH2", "DL12_CH4 Switch", "DL12"},

	{"I2S1_CH1", "DL6_CH1 Switch", "DL6"},
	{"I2S1_CH2", "DL6_CH2 Switch", "DL6"},

	{"I2S1_CH1", "DL4_CH1 Switch", "DL4"},
	{"I2S1_CH2", "DL4_CH2 Switch", "DL4"},

	{"I2S1_CH1", "DL5_CH1 Switch", "DL5"},
	{"I2S1_CH2", "DL5_CH2 Switch", "DL5"},

	{"I2S1_CH1", "DL8_CH1 Switch", "DL8"},
	{"I2S1_CH2", "DL8_CH2 Switch", "DL8"},

	{"I2S1", NULL, "I2S1_CH1"},
	{"I2S1", NULL, "I2S1_CH2"},

	{"I2S1", NULL, "I2S0_EN", mtk_afe_i2s_share_connect},
	{"I2S1", NULL, "I2S1_EN"},
	{"I2S1", NULL, "I2S2_EN", mtk_afe_i2s_share_connect},
	{"I2S1", NULL, "I2S3_EN", mtk_afe_i2s_share_connect},

	{"I2S1", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S1", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S1", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S1", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S1_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S1_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S1", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S1", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S1", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S1", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S1_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S1_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* i2s2 */
	{"I2S2", NULL, "I2S0_EN", mtk_afe_i2s_share_connect},
	{"I2S2", NULL, "I2S1_EN", mtk_afe_i2s_share_connect},
	{"I2S2", NULL, "I2S2_EN"},
	{"I2S2", NULL, "I2S3_EN", mtk_afe_i2s_share_connect},

	{"I2S2", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S2", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S2", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S2", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S2_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S2_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S2", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S2", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S2", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S2", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S2_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S2_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* i2s3 */
	{"I2S3_CH1", "DL1_CH1 Switch", "DL1"},
	{"I2S3_CH2", "DL1_CH2 Switch", "DL1"},

	{"I2S3_CH1", "DL1_CH1 Switch", "DSP_DL1_VIRT"},
	{"I2S3_CH2", "DL1_CH2 Switch", "DSP_DL1_VIRT"},

	{"I2S3_CH1", "DL2_CH1 Switch", "DL2"},
	{"I2S3_CH2", "DL2_CH2 Switch", "DL2"},

	{"I2S3_CH1", "DL2_CH1 Switch", "DSP_DL2_VIRT"},
	{"I2S3_CH2", "DL2_CH2 Switch", "DSP_DL2_VIRT"},

	{"I2S3_CH1", "DL3_CH1 Switch", "DL3"},
	{"I2S3_CH2", "DL3_CH2 Switch", "DL3"},

	{"I2S3_CH1", "DL12_CH1 Switch", "DL12"},
	{"I2S3_CH2", "DL12_CH2 Switch", "DL12"},

	{"I2S3_CH1", "DL12_CH3 Switch", "DL12"},
	{"I2S3_CH2", "DL12_CH4 Switch", "DL12"},

	{"I2S3_CH1", "DL6_CH1 Switch", "DL6"},
	{"I2S3_CH2", "DL6_CH2 Switch", "DL6"},

	{"I2S3_CH1", "DL4_CH1 Switch", "DL4"},
	{"I2S3_CH2", "DL4_CH2 Switch", "DL4"},

	{"I2S3_CH1", "DL5_CH1 Switch", "DL5"},
	{"I2S3_CH2", "DL5_CH2 Switch", "DL5"},

	{"I2S3_CH1", "DL8_CH1 Switch", "DL8"},
	{"I2S3_CH2", "DL8_CH2 Switch", "DL8"},

	{"I2S3", NULL, "I2S3_CH1"},
	{"I2S3", NULL, "I2S3_CH2"},

	{"I2S3", NULL, "I2S0_EN", mtk_afe_i2s_share_connect},
	{"I2S3", NULL, "I2S1_EN", mtk_afe_i2s_share_connect},
	{"I2S3", NULL, "I2S2_EN", mtk_afe_i2s_share_connect},
	{"I2S3", NULL, "I2S3_EN"},

	{"I2S3", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S3", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S3", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S3", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S3_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S3_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S3", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S3", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S3", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S3", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S3_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S3_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* allow i2s on without codec on */
	{"I2S0", NULL, "I2S0_In_Mux"},
	{"I2S0_In_Mux", "Dummy_Widget", "I2S_DUMMY_IN"},

	{"I2S1_Out_Mux", "Dummy_Widget", "I2S1"},
	{"I2S_DUMMY_OUT", NULL, "I2S1_Out_Mux"},

	{"I2S2", NULL, "I2S2_In_Mux"},
	{"I2S2_In_Mux", "Dummy_Widget", "I2S_DUMMY_IN"},

	{"I2S3_Out_Mux", "Dummy_Widget", "I2S3"},
	{"I2S_DUMMY_OUT", NULL, "I2S3_Out_Mux"},

	/* i2s in lpbk */
	{"I2S0_Lpbk_Mux", "Lpbk", "I2S3"},
	{"I2S2_Lpbk_Mux", "Lpbk", "I2S1"},
	{"I2S0", NULL, "I2S0_Lpbk_Mux"},
	{"I2S2", NULL, "I2S2_Lpbk_Mux"},
};

/* dai ops */
static int mtk_dai_connsys_i2s_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *params,
					 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	unsigned int rate_reg = mt8186_rate_transform(afe->dev,
						      rate, dai->id);
	unsigned int i2s_con = 0;

	dev_dbg(afe->dev, "%s(), id %d, stream %d, rate %d\n",
		__func__, dai->id, substream->stream, rate);

	/* non-inverse, i2s mode, slave, 16bits, from connsys */
	i2s_con |= 0 << INV_PAD_CTRL_SFT;
	i2s_con |= I2S_FMT_I2S << I2S_FMT_SFT;
	i2s_con |= 1 << I2S_SRC_SFT;
	i2s_con |= get_i2s_wlen(SNDRV_PCM_FORMAT_S16_LE) << I2S_WLEN_SFT;
	i2s_con |= 0 << I2SIN_PAD_SEL_SFT;
	regmap_write(afe->regmap, AFE_CONNSYS_I2S_CON, i2s_con);

	/* use asrc */
	regmap_update_bits(afe->regmap, AFE_CONNSYS_I2S_CON,
			   I2S_BYPSRC_MASK_SFT, 0);

	/* slave mode, set i2s for asrc */
	regmap_update_bits(afe->regmap, AFE_CONNSYS_I2S_CON,
			   I2S_MODE_MASK_SFT, rate_reg << I2S_MODE_SFT);

	if (rate == 44100)
		regmap_write(afe->regmap, AFE_ASRC_2CH_CON3, 0x1b9000);
	else if (rate == 32000)
		regmap_write(afe->regmap, AFE_ASRC_2CH_CON3, 0x140000);
	else
		regmap_write(afe->regmap, AFE_ASRC_2CH_CON3, 0x1e0000);

	/* Calibration setting */
	regmap_write(afe->regmap, AFE_ASRC_2CH_CON4, 0x140000);
	regmap_write(afe->regmap, AFE_ASRC_2CH_CON9, 0x36000);
	regmap_write(afe->regmap, AFE_ASRC_2CH_CON10, 0x2fc00);
	regmap_write(afe->regmap, AFE_ASRC_2CH_CON6, 0x7ef4);
	regmap_write(afe->regmap, AFE_ASRC_2CH_CON5, 0xff5986);

	/* 0:Stereo 1:Mono */
	regmap_update_bits(afe->regmap, AFE_ASRC_2CH_CON2,
			   CHSET_IS_MONO_MASK_SFT, 0);

	return 0;
}

static int mtk_dai_connsys_i2s_trigger(struct snd_pcm_substream *substream,
				       int cmd, struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	dev_dbg(afe->dev, "%s(), cmd %d, stream %d\n",
		__func__, cmd, substream->stream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		/* i2s enable */
		regmap_update_bits(afe->regmap,
				   AFE_CONNSYS_I2S_CON,
				   I2S_EN_MASK_SFT,
				   BIT(I2S_EN_SFT));

		/* calibrator enable */
		regmap_update_bits(afe->regmap,
				   AFE_ASRC_2CH_CON5,
				   CALI_EN_MASK_SFT,
				   BIT(CALI_EN_SFT));

		/* asrc enable */
		regmap_update_bits(afe->regmap,
				   AFE_ASRC_2CH_CON0,
				   CON0_CHSET_STR_CLR_MASK_SFT,
				   BIT(CON0_CHSET_STR_CLR_SFT));
		regmap_update_bits(afe->regmap,
				   AFE_ASRC_2CH_CON0,
				   CON0_ASM_ON_MASK_SFT,
				   BIT(CON0_ASM_ON_SFT));

		afe_priv->dai_on[dai->id] = true;
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		regmap_update_bits(afe->regmap, AFE_ASRC_2CH_CON0,
				   CON0_ASM_ON_MASK_SFT, 0);
		regmap_update_bits(afe->regmap, AFE_ASRC_2CH_CON5,
				   CALI_EN_MASK_SFT, 0);

		/* i2s disable */
		regmap_update_bits(afe->regmap, AFE_CONNSYS_I2S_CON,
				   I2S_EN_MASK_SFT, 0);

		/* bypass asrc */
		regmap_update_bits(afe->regmap, AFE_CONNSYS_I2S_CON,
				   I2S_BYPSRC_MASK_SFT, BIT(I2S_BYPSRC_SFT));

		afe_priv->dai_on[dai->id] = false;
		return 0;
	default:
		return -EINVAL;
	}
	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_connsys_i2s_ops = {
	.hw_params = mtk_dai_connsys_i2s_hw_params,
	.trigger = mtk_dai_connsys_i2s_trigger,
};

/* i2s */
static int mtk_dai_i2s_config(struct mtk_base_afe *afe,
			      struct snd_pcm_hw_params *params,
			      int i2s_id)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv = afe_priv->dai_priv[i2s_id];

	unsigned int rate = params_rate(params);
	unsigned int rate_reg = mt8186_rate_transform(afe->dev,
						      rate, i2s_id);
	snd_pcm_format_t format = params_format(params);
	unsigned int i2s_con = 0;
	int ret;

	dev_dbg(afe->dev, "%s(), id %d, rate %d, format %d\n",
		__func__, i2s_id, rate, format);

	i2s_priv->rate = rate;

	switch (i2s_id) {
	case MT8186_DAI_I2S_0:
		i2s_con = I2S_IN_PAD_IO_MUX << I2SIN_PAD_SEL_SFT;
		i2s_con |= rate_reg << I2S_OUT_MODE_SFT;
		i2s_con |= I2S_FMT_I2S << I2S_FMT_SFT;
		i2s_con |= get_i2s_wlen(format) << I2S_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON,
				   0xffffeffa, i2s_con);
		break;
	case MT8186_DAI_I2S_1:
		i2s_con = I2S1_SEL_O28_O29 << I2S2_SEL_O03_O04_SFT;
		i2s_con |= rate_reg << I2S2_OUT_MODE_SFT;
		i2s_con |= I2S_FMT_I2S << I2S2_FMT_SFT;
		i2s_con |= get_i2s_wlen(format) << I2S2_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON1,
				   0xffffeffa, i2s_con);
		break;
	case MT8186_DAI_I2S_2:
		i2s_con = 8 << I2S3_UPDATE_WORD_SFT;
		i2s_con |= rate_reg << I2S3_OUT_MODE_SFT;
		i2s_con |= I2S_FMT_I2S << I2S3_FMT_SFT;
		i2s_con |= get_i2s_wlen(format) << I2S3_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON2,
				   0xffffeffa, i2s_con);
		break;
	case MT8186_DAI_I2S_3:
		i2s_con = rate_reg << I2S4_OUT_MODE_SFT;
		i2s_con |= I2S_FMT_I2S << I2S4_FMT_SFT;
		i2s_con |= get_i2s_wlen(format) << I2S4_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON3,
				   0xffffeffa, i2s_con);
		break;
	default:
		dev_err(afe->dev, "%s(), id %d not support\n",
			__func__, i2s_id);
		return -EINVAL;
	}

	/* set share i2s */
	if (i2s_priv->share_i2s_id >= 0) {
		ret = mtk_dai_i2s_config(afe, params, i2s_priv->share_i2s_id);
		if (ret)
			return ret;
	}

	return 0;
}

static int mtk_dai_i2s_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	return mtk_dai_i2s_config(afe, params, dai->id);
}

static int mtk_dai_i2s_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv = afe_priv->dai_priv[dai->id];
	int apll;
	int apll_rate;

	if (dir != SND_SOC_CLOCK_OUT) {
		dev_err(afe->dev, "%s(), dir != SND_SOC_CLOCK_OUT", __func__);
		return -EINVAL;
	}

	dev_dbg(afe->dev, "%s(), freq %d\n", __func__, freq);

	apll = mt8186_get_apll_by_rate(afe, freq);
	apll_rate = mt8186_get_apll_rate(afe, apll);

	if (freq > apll_rate) {
		dev_err(afe->dev, "%s(), freq > apll rate", __func__);
		return -EINVAL;
	}

	if (apll_rate % freq != 0) {
		dev_err(afe->dev, "%s(), APLL cannot generate freq Hz", __func__);
		return -EINVAL;
	}

	i2s_priv->mclk_rate = freq;
	i2s_priv->mclk_apll = apll;

	if (i2s_priv->share_i2s_id > 0) {
		struct mtk_afe_i2s_priv *share_i2s_priv;

		share_i2s_priv = afe_priv->dai_priv[i2s_priv->share_i2s_id];
		if (!share_i2s_priv) {
			dev_err(afe->dev, "%s(), share_i2s_priv == NULL", __func__);
			return -EINVAL;
		}

		share_i2s_priv->mclk_rate = i2s_priv->mclk_rate;
		share_i2s_priv->mclk_apll = i2s_priv->mclk_apll;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_i2s_ops = {
	.hw_params = mtk_dai_i2s_hw_params,
	.set_sysclk = mtk_dai_i2s_set_sysclk,
};

/* dai driver */
#define MTK_CONNSYS_I2S_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)

#define MTK_I2S_RATES (SNDRV_PCM_RATE_8000_48000 |\
		       SNDRV_PCM_RATE_88200 |\
		       SNDRV_PCM_RATE_96000 |\
		       SNDRV_PCM_RATE_176400 |\
		       SNDRV_PCM_RATE_192000)

#define MTK_I2S_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_i2s_driver[] = {
	{
		.name = "CONNSYS_I2S",
		.id = MT8186_DAI_CONNSYS_I2S,
		.capture = {
			.stream_name = "Connsys I2S",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_CONNSYS_I2S_RATES,
			.formats = MTK_I2S_FORMATS,
		},
		.ops = &mtk_dai_connsys_i2s_ops,
	},
	{
		.name = "I2S0",
		.id = MT8186_DAI_I2S_0,
		.capture = {
			.stream_name = "I2S0",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_I2S_RATES,
			.formats = MTK_I2S_FORMATS,
		},
		.ops = &mtk_dai_i2s_ops,
	},
	{
		.name = "I2S1",
		.id = MT8186_DAI_I2S_1,
		.playback = {
			.stream_name = "I2S1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_I2S_RATES,
			.formats = MTK_I2S_FORMATS,
		},
		.ops = &mtk_dai_i2s_ops,
	},
	{
		.name = "I2S2",
		.id = MT8186_DAI_I2S_2,
		.capture = {
			.stream_name = "I2S2",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_I2S_RATES,
			.formats = MTK_I2S_FORMATS,
		},
		.ops = &mtk_dai_i2s_ops,
	},
	{
		.name = "I2S3",
		.id = MT8186_DAI_I2S_3,
		.playback = {
			.stream_name = "I2S3",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_I2S_RATES,
			.formats = MTK_I2S_FORMATS,
		},
		.ops = &mtk_dai_i2s_ops,
	}
};

/* this enum is merely for mtk_afe_i2s_priv declare */
enum {
	DAI_I2S0 = 0,
	DAI_I2S1,
	DAI_I2S2,
	DAI_I2S3,
	DAI_I2S_NUM,
};

static const struct mtk_afe_i2s_priv mt8186_i2s_priv[DAI_I2S_NUM] = {
	[DAI_I2S0] = {
		.id = MT8186_DAI_I2S_0,
		.mclk_id = MT8186_I2S0_MCK,
		.share_i2s_id = -1,
	},
	[DAI_I2S1] = {
		.id = MT8186_DAI_I2S_1,
		.mclk_id = MT8186_I2S1_MCK,
		.share_i2s_id = -1,
	},
	[DAI_I2S2] = {
		.id = MT8186_DAI_I2S_2,
		.mclk_id = MT8186_I2S2_MCK,
		.share_i2s_id = -1,
	},
	[DAI_I2S3] = {
		.id = MT8186_DAI_I2S_3,
		/*  clock gate naming is hf_faud_i2s4_m_ck*/
		.mclk_id = MT8186_I2S4_MCK,
		.share_i2s_id = -1,
	}
};

/**
 * mt8186_dai_i2s_set_share() - Set up I2S ports to share a single clock.
 * @afe: Pointer to &struct mtk_base_afe
 * @main_i2s_name: The name of the I2S port that will provide the clock
 * @secondary_i2s_name: The name of the I2S port that will use this clock
 */
int mt8186_dai_i2s_set_share(struct mtk_base_afe *afe, const char *main_i2s_name,
			     const char *secondary_i2s_name)
{
	struct mtk_afe_i2s_priv *secondary_i2s_priv;
	int main_i2s_id;

	secondary_i2s_priv = get_i2s_priv_by_name(afe, secondary_i2s_name);
	if (!secondary_i2s_priv)
		return -EINVAL;

	main_i2s_id = get_i2s_id_by_name(afe, main_i2s_name);
	if (main_i2s_id < 0)
		return main_i2s_id;

	secondary_i2s_priv->share_i2s_id = main_i2s_id;

	return 0;
}
EXPORT_SYMBOL_GPL(mt8186_dai_i2s_set_share);

static int mt8186_dai_i2s_set_priv(struct mtk_base_afe *afe)
{
	int i;
	int ret;

	for (i = 0; i < DAI_I2S_NUM; i++) {
		ret = mt8186_dai_set_priv(afe, mt8186_i2s_priv[i].id,
					  sizeof(struct mtk_afe_i2s_priv),
					  &mt8186_i2s_priv[i]);
		if (ret)
			return ret;
	}

	return 0;
}

int mt8186_dai_i2s_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;
	int ret;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_i2s_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_i2s_driver);

	dai->controls = mtk_dai_i2s_controls;
	dai->num_controls = ARRAY_SIZE(mtk_dai_i2s_controls);
	dai->dapm_widgets = mtk_dai_i2s_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_i2s_widgets);
	dai->dapm_routes = mtk_dai_i2s_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_i2s_routes);

	/* set all dai i2s private data */
	ret = mt8186_dai_i2s_set_priv(afe);
	if (ret)
		return ret;

	return 0;
}
