// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI I2S Control
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/bitops.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt8183-afe-clk.h"
#include "mt8183-afe-common.h"
#include "mt8183-interconnection.h"
#include "mt8183-reg.h"

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

	int share_i2s_id;

	int mclk_id;
	int mclk_rate;
	int mclk_apll;

	int use_eiaj;
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
#define MTK_AFE_I2S5_KCONTROL_NAME "I2S5_HD_Mux"

#define I2S0_HD_EN_W_NAME "I2S0_HD_EN"
#define I2S1_HD_EN_W_NAME "I2S1_HD_EN"
#define I2S2_HD_EN_W_NAME "I2S2_HD_EN"
#define I2S3_HD_EN_W_NAME "I2S3_HD_EN"
#define I2S5_HD_EN_W_NAME "I2S5_HD_EN"

#define I2S0_MCLK_EN_W_NAME "I2S0_MCLK_EN"
#define I2S1_MCLK_EN_W_NAME "I2S1_MCLK_EN"
#define I2S2_MCLK_EN_W_NAME "I2S2_MCLK_EN"
#define I2S3_MCLK_EN_W_NAME "I2S3_MCLK_EN"
#define I2S5_MCLK_EN_W_NAME "I2S5_MCLK_EN"

static int get_i2s_id_by_name(struct mtk_base_afe *afe,
			      const char *name)
{
	if (strncmp(name, "I2S0", 4) == 0)
		return MT8183_DAI_I2S_0;
	else if (strncmp(name, "I2S1", 4) == 0)
		return MT8183_DAI_I2S_1;
	else if (strncmp(name, "I2S2", 4) == 0)
		return MT8183_DAI_I2S_2;
	else if (strncmp(name, "I2S3", 4) == 0)
		return MT8183_DAI_I2S_3;
	else if (strncmp(name, "I2S5", 4) == 0)
		return MT8183_DAI_I2S_5;
	else
		return -EINVAL;
}

static struct mtk_afe_i2s_priv *get_i2s_priv_by_name(struct mtk_base_afe *afe,
						     const char *name)
{
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_i2s_id_by_name(afe, name);

	if (dai_id < 0)
		return NULL;

	return afe_priv->dai_priv[dai_id];
}

/* low jitter control */
static const char * const mt8183_i2s_hd_str[] = {
	"Normal", "Low_Jitter"
};

static const struct soc_enum mt8183_i2s_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mt8183_i2s_hd_str),
			    mt8183_i2s_hd_str),
};

static int mt8183_i2s_hd_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, kcontrol->id.name);

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return -EINVAL;
	}

	ucontrol->value.integer.value[0] = i2s_priv->low_jitter_en;

	return 0;
}

static int mt8183_i2s_hd_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int hd_en, change;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	hd_en = ucontrol->value.integer.value[0];

	i2s_priv = get_i2s_priv_by_name(afe, kcontrol->id.name);

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return -EINVAL;
	}

	change = i2s_priv->low_jitter_en != hd_en;
	i2s_priv->low_jitter_en = hd_en;

	return change;
}

static const struct snd_kcontrol_new mtk_dai_i2s_controls[] = {
	SOC_ENUM_EXT(MTK_AFE_I2S0_KCONTROL_NAME, mt8183_i2s_enum[0],
		     mt8183_i2s_hd_get, mt8183_i2s_hd_set),
	SOC_ENUM_EXT(MTK_AFE_I2S1_KCONTROL_NAME, mt8183_i2s_enum[0],
		     mt8183_i2s_hd_get, mt8183_i2s_hd_set),
	SOC_ENUM_EXT(MTK_AFE_I2S2_KCONTROL_NAME, mt8183_i2s_enum[0],
		     mt8183_i2s_hd_get, mt8183_i2s_hd_set),
	SOC_ENUM_EXT(MTK_AFE_I2S3_KCONTROL_NAME, mt8183_i2s_enum[0],
		     mt8183_i2s_hd_get, mt8183_i2s_hd_set),
	SOC_ENUM_EXT(MTK_AFE_I2S5_KCONTROL_NAME, mt8183_i2s_enum[0],
		     mt8183_i2s_hd_get, mt8183_i2s_hd_set),
};

/* dai component */
/* interconnection */
static const struct snd_kcontrol_new mtk_i2s3_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN0, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN0, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN0, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN0,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN0,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN0,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s3_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN1, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN1, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN1, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN1,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN1,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN1,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2", AFE_CONN1,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2", AFE_CONN1,
				    I_PCM_2_CAP_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s1_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN28, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN28, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN28, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN28,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN28,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN28,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s1_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN29, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN29, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN29, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN29,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN29,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN29,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2", AFE_CONN29,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2", AFE_CONN29,
				    I_PCM_2_CAP_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s5_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN30, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN30, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN30, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN30,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN30,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN30,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_i2s5_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN31, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN31, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN31, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN31,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN31,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN31,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2", AFE_CONN31,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2", AFE_CONN31,
				    I_PCM_2_CAP_CH2, 1, 0),
};

enum {
	SUPPLY_SEQ_APLL,
	SUPPLY_SEQ_I2S_MCLK_EN,
	SUPPLY_SEQ_I2S_HD_EN,
	SUPPLY_SEQ_I2S_EN,
};

static int mtk_apll_event(struct snd_soc_dapm_widget *w,
			  struct snd_kcontrol *kcontrol,
			  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (snd_soc_dapm_widget_name_cmp(w, APLL1_W_NAME) == 0)
			mt8183_apll1_enable(afe);
		else
			mt8183_apll2_enable(afe);
		break;
	case SND_SOC_DAPM_POST_PMD:
		if (snd_soc_dapm_widget_name_cmp(w, APLL1_W_NAME) == 0)
			mt8183_apll1_disable(afe);
		else
			mt8183_apll2_disable(afe);
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

	i2s_priv = get_i2s_priv_by_name(afe, w->name);

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8183_mck_enable(afe, i2s_priv->mclk_id, i2s_priv->mclk_rate);
		break;
	case SND_SOC_DAPM_POST_PMD:
		i2s_priv->mclk_rate = 0;
		mt8183_mck_disable(afe, i2s_priv->mclk_id);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget mtk_dai_i2s_widgets[] = {
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

	SND_SOC_DAPM_MIXER("I2S5_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_i2s5_ch1_mix,
			   ARRAY_SIZE(mtk_i2s5_ch1_mix)),
	SND_SOC_DAPM_MIXER("I2S5_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_i2s5_ch2_mix,
			   ARRAY_SIZE(mtk_i2s5_ch2_mix)),

	/* i2s en*/
	SND_SOC_DAPM_SUPPLY_S("I2S0_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON, I2S_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S1_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON1, I2S_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S2_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON2, I2S_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S3_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON3, I2S_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("I2S5_EN", SUPPLY_SEQ_I2S_EN,
			      AFE_I2S_CON4, I2S5_EN_SFT, 0,
			      NULL, 0),
	/* i2s hd en */
	SND_SOC_DAPM_SUPPLY_S(I2S0_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON, I2S1_HD_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S(I2S1_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON1, I2S2_HD_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S(I2S2_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON2, I2S3_HD_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S(I2S3_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON3, I2S4_HD_EN_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S(I2S5_HD_EN_W_NAME, SUPPLY_SEQ_I2S_HD_EN,
			      AFE_I2S_CON4, I2S5_HD_EN_SFT, 0,
			      NULL, 0),

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
	SND_SOC_DAPM_SUPPLY_S(I2S5_MCLK_EN_W_NAME, SUPPLY_SEQ_I2S_MCLK_EN,
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
};

static int mtk_afe_i2s_share_connect(struct snd_soc_dapm_widget *source,
				     struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_i2s_priv *i2s_priv;

	i2s_priv = get_i2s_priv_by_name(afe, sink->name);

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return 0;
	}

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

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return 0;
	}

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

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return 0;
	}

	/* which apll */
	cur_apll = mt8183_get_apll_by_name(afe, source->name);

	/* choose APLL from i2s rate */
	i2s_need_apll = mt8183_get_apll_by_rate(afe, i2s_priv->rate);

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

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return 0;
	}

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

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return 0;
	}

	/* which apll */
	cur_apll = mt8183_get_apll_by_name(afe, source->name);

	return (i2s_priv->mclk_apll == cur_apll) ? 1 : 0;
}

static const struct snd_soc_dapm_route mtk_dai_i2s_routes[] = {
	/* i2s0 */
	{"I2S0", NULL, "I2S0_EN"},
	{"I2S0", NULL, "I2S1_EN", mtk_afe_i2s_share_connect},
	{"I2S0", NULL, "I2S2_EN", mtk_afe_i2s_share_connect},
	{"I2S0", NULL, "I2S3_EN", mtk_afe_i2s_share_connect},
	{"I2S0", NULL, "I2S5_EN", mtk_afe_i2s_share_connect},

	{"I2S0", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S0", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S0", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S0", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S0", NULL, I2S5_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S0_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S0_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S0", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S0", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S0", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S0", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S0", NULL, I2S5_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S0_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S0_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* i2s1 */
	{"I2S1_CH1", "DL1_CH1", "DL1"},
	{"I2S1_CH2", "DL1_CH2", "DL1"},

	{"I2S1_CH1", "DL2_CH1", "DL2"},
	{"I2S1_CH2", "DL2_CH2", "DL2"},

	{"I2S1_CH1", "DL3_CH1", "DL3"},
	{"I2S1_CH2", "DL3_CH2", "DL3"},

	{"I2S1", NULL, "I2S1_CH1"},
	{"I2S1", NULL, "I2S1_CH2"},

	{"I2S1", NULL, "I2S0_EN", mtk_afe_i2s_share_connect},
	{"I2S1", NULL, "I2S1_EN"},
	{"I2S1", NULL, "I2S2_EN", mtk_afe_i2s_share_connect},
	{"I2S1", NULL, "I2S3_EN", mtk_afe_i2s_share_connect},
	{"I2S1", NULL, "I2S5_EN", mtk_afe_i2s_share_connect},

	{"I2S1", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S1", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S1", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S1", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S1", NULL, I2S5_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S1_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S1_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S1", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S1", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S1", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S1", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S1", NULL, I2S5_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S1_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S1_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* i2s2 */
	{"I2S2", NULL, "I2S0_EN", mtk_afe_i2s_share_connect},
	{"I2S2", NULL, "I2S1_EN", mtk_afe_i2s_share_connect},
	{"I2S2", NULL, "I2S2_EN"},
	{"I2S2", NULL, "I2S3_EN", mtk_afe_i2s_share_connect},
	{"I2S2", NULL, "I2S5_EN", mtk_afe_i2s_share_connect},

	{"I2S2", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S2", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S2", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S2", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S2", NULL, I2S5_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S2_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S2_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S2", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S2", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S2", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S2", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S2", NULL, I2S5_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S2_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S2_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* i2s3 */
	{"I2S3_CH1", "DL1_CH1", "DL1"},
	{"I2S3_CH2", "DL1_CH2", "DL1"},

	{"I2S3_CH1", "DL2_CH1", "DL2"},
	{"I2S3_CH2", "DL2_CH2", "DL2"},

	{"I2S3_CH1", "DL3_CH1", "DL3"},
	{"I2S3_CH2", "DL3_CH2", "DL3"},

	{"I2S3", NULL, "I2S3_CH1"},
	{"I2S3", NULL, "I2S3_CH2"},

	{"I2S3", NULL, "I2S0_EN", mtk_afe_i2s_share_connect},
	{"I2S3", NULL, "I2S1_EN", mtk_afe_i2s_share_connect},
	{"I2S3", NULL, "I2S2_EN", mtk_afe_i2s_share_connect},
	{"I2S3", NULL, "I2S3_EN"},
	{"I2S3", NULL, "I2S5_EN", mtk_afe_i2s_share_connect},

	{"I2S3", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S3", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S3", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S3", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S3", NULL, I2S5_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S3_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S3_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S3", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S3", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S3", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S3", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S3", NULL, I2S5_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S3_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S3_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},

	/* i2s5 */
	{"I2S5_CH1", "DL1_CH1", "DL1"},
	{"I2S5_CH2", "DL1_CH2", "DL1"},

	{"I2S5_CH1", "DL2_CH1", "DL2"},
	{"I2S5_CH2", "DL2_CH2", "DL2"},

	{"I2S5_CH1", "DL3_CH1", "DL3"},
	{"I2S5_CH2", "DL3_CH2", "DL3"},

	{"I2S5", NULL, "I2S5_CH1"},
	{"I2S5", NULL, "I2S5_CH2"},

	{"I2S5", NULL, "I2S0_EN", mtk_afe_i2s_share_connect},
	{"I2S5", NULL, "I2S1_EN", mtk_afe_i2s_share_connect},
	{"I2S5", NULL, "I2S2_EN", mtk_afe_i2s_share_connect},
	{"I2S5", NULL, "I2S3_EN", mtk_afe_i2s_share_connect},
	{"I2S5", NULL, "I2S5_EN"},

	{"I2S5", NULL, I2S0_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S5", NULL, I2S1_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S5", NULL, I2S2_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S5", NULL, I2S3_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{"I2S5", NULL, I2S5_HD_EN_W_NAME, mtk_afe_i2s_hd_connect},
	{I2S5_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_i2s_apll_connect},
	{I2S5_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_i2s_apll_connect},

	{"I2S5", NULL, I2S0_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S5", NULL, I2S1_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S5", NULL, I2S2_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S5", NULL, I2S3_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{"I2S5", NULL, I2S5_MCLK_EN_W_NAME, mtk_afe_i2s_mclk_connect},
	{I2S5_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_mclk_apll_connect},
	{I2S5_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_mclk_apll_connect},
};

/* dai ops */
static int mtk_dai_i2s_config(struct mtk_base_afe *afe,
			      struct snd_pcm_hw_params *params,
			      int i2s_id)
{
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv = afe_priv->dai_priv[i2s_id];

	unsigned int rate = params_rate(params);
	unsigned int rate_reg = mt8183_rate_transform(afe->dev,
						      rate, i2s_id);
	snd_pcm_format_t format = params_format(params);
	unsigned int i2s_con = 0, fmt_con = I2S_FMT_I2S << I2S_FMT_SFT;
	int ret = 0;

	if (i2s_priv) {
		i2s_priv->rate = rate;

		if (i2s_priv->use_eiaj)
			fmt_con = I2S_FMT_EIAJ << I2S_FMT_SFT;
	} else {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
	}

	switch (i2s_id) {
	case MT8183_DAI_I2S_0:
		regmap_update_bits(afe->regmap, AFE_DAC_CON1,
				   I2S_MODE_MASK_SFT, rate_reg << I2S_MODE_SFT);
		i2s_con = I2S_IN_PAD_IO_MUX << I2SIN_PAD_SEL_SFT;
		i2s_con |= fmt_con;
		i2s_con |= get_i2s_wlen(format) << I2S_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON,
				   0xffffeffe, i2s_con);
		break;
	case MT8183_DAI_I2S_1:
		i2s_con = I2S1_SEL_O28_O29 << I2S2_SEL_O03_O04_SFT;
		i2s_con |= rate_reg << I2S2_OUT_MODE_SFT;
		i2s_con |= fmt_con;
		i2s_con |= get_i2s_wlen(format) << I2S2_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON1,
				   0xffffeffe, i2s_con);
		break;
	case MT8183_DAI_I2S_2:
		i2s_con = 8 << I2S3_UPDATE_WORD_SFT;
		i2s_con |= rate_reg << I2S3_OUT_MODE_SFT;
		i2s_con |= fmt_con;
		i2s_con |= get_i2s_wlen(format) << I2S3_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON2,
				   0xffffeffe, i2s_con);
		break;
	case MT8183_DAI_I2S_3:
		i2s_con = rate_reg << I2S4_OUT_MODE_SFT;
		i2s_con |= fmt_con;
		i2s_con |= get_i2s_wlen(format) << I2S4_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON3,
				   0xffffeffe, i2s_con);
		break;
	case MT8183_DAI_I2S_5:
		i2s_con = rate_reg << I2S5_OUT_MODE_SFT;
		i2s_con |= fmt_con;
		i2s_con |= get_i2s_wlen(format) << I2S5_WLEN_SFT;
		regmap_update_bits(afe->regmap, AFE_I2S_CON4,
				   0xffffeffe, i2s_con);
		break;
	default:
		dev_warn(afe->dev, "%s(), id %d not support\n",
			 __func__, i2s_id);
		return -EINVAL;
	}

	/* set share i2s */
	if (i2s_priv && i2s_priv->share_i2s_id >= 0)
		ret = mtk_dai_i2s_config(afe, params, i2s_priv->share_i2s_id);

	return ret;
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
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv = afe_priv->dai_priv[dai->id];
	int apll;
	int apll_rate;

	if (!i2s_priv) {
		dev_warn(afe->dev, "%s(), i2s_priv == NULL", __func__);
		return -EINVAL;
	}

	if (dir != SND_SOC_CLOCK_OUT) {
		dev_warn(afe->dev, "%s(), dir != SND_SOC_CLOCK_OUT", __func__);
		return -EINVAL;
	}

	apll = mt8183_get_apll_by_rate(afe, freq);
	apll_rate = mt8183_get_apll_rate(afe, apll);

	if (freq > apll_rate) {
		dev_warn(afe->dev, "%s(), freq > apll rate", __func__);
		return -EINVAL;
	}

	if (apll_rate % freq != 0) {
		dev_warn(afe->dev, "%s(), APLL cannot generate freq Hz",
			 __func__);
		return -EINVAL;
	}

	i2s_priv->mclk_rate = freq;
	i2s_priv->mclk_apll = apll;

	if (i2s_priv->share_i2s_id > 0) {
		struct mtk_afe_i2s_priv *share_i2s_priv;

		share_i2s_priv = afe_priv->dai_priv[i2s_priv->share_i2s_id];
		if (!share_i2s_priv) {
			dev_warn(afe->dev, "%s(), share_i2s_priv == NULL",
				 __func__);
			return -EINVAL;
		}

		share_i2s_priv->mclk_rate = i2s_priv->mclk_rate;
		share_i2s_priv->mclk_apll = i2s_priv->mclk_apll;
	}

	return 0;
}

static int mtk_dai_i2s_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv;

	switch (dai->id) {
	case MT8183_DAI_I2S_0:
	case MT8183_DAI_I2S_1:
	case MT8183_DAI_I2S_2:
	case MT8183_DAI_I2S_3:
	case MT8183_DAI_I2S_5:
		break;
	default:
		dev_warn(afe->dev, "%s(), id %d not support\n",
			 __func__, dai->id);
		return -EINVAL;
	}
	i2s_priv = afe_priv->dai_priv[dai->id];

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		i2s_priv->use_eiaj = 1;
		break;
	case SND_SOC_DAIFMT_I2S:
		i2s_priv->use_eiaj = 0;
		break;
	default:
		dev_warn(afe->dev, "%s(), DAI format %d not support\n",
			 __func__, fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_i2s_ops = {
	.hw_params = mtk_dai_i2s_hw_params,
	.set_sysclk = mtk_dai_i2s_set_sysclk,
	.set_fmt = mtk_dai_i2s_set_fmt,
};

/* dai driver */
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
		.name = "I2S0",
		.id = MT8183_DAI_I2S_0,
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
		.id = MT8183_DAI_I2S_1,
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
		.id = MT8183_DAI_I2S_2,
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
		.id = MT8183_DAI_I2S_3,
		.playback = {
			.stream_name = "I2S3",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_I2S_RATES,
			.formats = MTK_I2S_FORMATS,
		},
		.ops = &mtk_dai_i2s_ops,
	},
	{
		.name = "I2S5",
		.id = MT8183_DAI_I2S_5,
		.playback = {
			.stream_name = "I2S5",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_I2S_RATES,
			.formats = MTK_I2S_FORMATS,
		},
		.ops = &mtk_dai_i2s_ops,
	},
};

/* this enum is merely for mtk_afe_i2s_priv declare */
enum {
	DAI_I2S0 = 0,
	DAI_I2S1,
	DAI_I2S2,
	DAI_I2S3,
	DAI_I2S5,
	DAI_I2S_NUM,
};

static const struct mtk_afe_i2s_priv mt8183_i2s_priv[DAI_I2S_NUM] = {
	[DAI_I2S0] = {
		.id = MT8183_DAI_I2S_0,
		.mclk_id = MT8183_I2S0_MCK,
		.share_i2s_id = -1,
	},
	[DAI_I2S1] = {
		.id = MT8183_DAI_I2S_1,
		.mclk_id = MT8183_I2S1_MCK,
		.share_i2s_id = -1,
	},
	[DAI_I2S2] = {
		.id = MT8183_DAI_I2S_2,
		.mclk_id = MT8183_I2S2_MCK,
		.share_i2s_id = -1,
	},
	[DAI_I2S3] = {
		.id = MT8183_DAI_I2S_3,
		.mclk_id = MT8183_I2S3_MCK,
		.share_i2s_id = -1,
	},
	[DAI_I2S5] = {
		.id = MT8183_DAI_I2S_5,
		.mclk_id = MT8183_I2S5_MCK,
		.share_i2s_id = -1,
	},
};

/**
 * mt8183_dai_i2s_set_share() - Set up I2S ports to share a single clock.
 * @afe: Pointer to &struct mtk_base_afe
 * @main_i2s_name: The name of the I2S port that will provide the clock
 * @secondary_i2s_name: The name of the I2S port that will use this clock
 */
int mt8183_dai_i2s_set_share(struct mtk_base_afe *afe, const char *main_i2s_name,
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
EXPORT_SYMBOL_GPL(mt8183_dai_i2s_set_share);

static int mt8183_dai_i2s_set_priv(struct mtk_base_afe *afe)
{
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_i2s_priv *i2s_priv;
	int i;

	for (i = 0; i < DAI_I2S_NUM; i++) {
		i2s_priv = devm_kzalloc(afe->dev,
					sizeof(struct mtk_afe_i2s_priv),
					GFP_KERNEL);
		if (!i2s_priv)
			return -ENOMEM;

		memcpy(i2s_priv, &mt8183_i2s_priv[i],
		       sizeof(struct mtk_afe_i2s_priv));

		afe_priv->dai_priv[mt8183_i2s_priv[i].id] = i2s_priv;
	}

	return 0;
}

int mt8183_dai_i2s_register(struct mtk_base_afe *afe)
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
	ret = mt8183_dai_i2s_set_priv(afe);
	if (ret)
		return ret;

	return 0;
}
