// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI TDM Control
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/regmap.h>
#include <sound/pcm_params.h>

#include "mt8186-afe-clk.h"
#include "mt8186-afe-common.h"
#include "mt8186-afe-gpio.h"
#include "mt8186-interconnection.h"

#define TDM_HD_EN_W_NAME "TDM_HD_EN"
#define TDM_MCLK_EN_W_NAME "TDM_MCLK_EN"
#define MTK_AFE_TDM_KCONTROL_NAME "TDM_HD_Mux"

struct mtk_afe_tdm_priv {
	unsigned int id;
	unsigned int rate; /* for determine which apll to use */
	unsigned int bck_invert;
	unsigned int lck_invert;
	unsigned int lrck_width;
	unsigned int mclk_id;
	unsigned int mclk_multiple; /* according to sample rate */
	unsigned int mclk_rate;
	unsigned int mclk_apll;
	unsigned int tdm_mode;
	unsigned int data_mode;
	unsigned int slave_mode;
	unsigned int low_jitter_en;
};

enum {
	TDM_IN_I2S = 0,
	TDM_IN_LJ = 1,
	TDM_IN_RJ = 2,
	TDM_IN_DSP_A = 4,
	TDM_IN_DSP_B = 5,
};

enum {
	TDM_DATA_ONE_PIN = 0,
	TDM_DATA_MULTI_PIN,
};

enum {
	TDM_BCK_NON_INV = 0,
	TDM_BCK_INV = 1,
};

enum {
	TDM_LCK_NON_INV = 0,
	TDM_LCK_INV = 1,
};

static unsigned int get_tdm_lrck_width(snd_pcm_format_t format,
				       unsigned int mode)
{
	if (mode == TDM_IN_DSP_A || mode == TDM_IN_DSP_B)
		return 0;

	return snd_pcm_format_physical_width(format) - 1;
}

static unsigned int get_tdm_ch_fixup(unsigned int channels)
{
	if (channels > 4)
		return 8;
	else if (channels > 2)
		return 4;

	return 2;
}

static unsigned int get_tdm_ch_per_sdata(unsigned int mode,
					 unsigned int channels)
{
	if (mode == TDM_IN_DSP_A || mode == TDM_IN_DSP_B)
		return get_tdm_ch_fixup(channels);

	return 2;
}

enum {
	SUPPLY_SEQ_APLL,
	SUPPLY_SEQ_TDM_MCK_EN,
	SUPPLY_SEQ_TDM_HD_EN,
	SUPPLY_SEQ_TDM_EN,
};

static int get_tdm_id_by_name(const char *name)
{
	return MT8186_DAI_TDM_IN;
}

static int mtk_tdm_en_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8186_afe_gpio_request(afe->dev, true, tdm_priv->id, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		mt8186_afe_gpio_request(afe->dev, false, tdm_priv->id, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_tdm_mck_en_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol,
				int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x, dai_id %d\n",
		__func__, w->name, event, dai_id);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8186_mck_enable(afe, tdm_priv->mclk_id, tdm_priv->mclk_rate);
		break;
	case SND_SOC_DAPM_POST_PMD:
		tdm_priv->mclk_rate = 0;
		mt8186_mck_disable(afe, tdm_priv->mclk_id);
		break;
	default:
		break;
	}

	return 0;
}

/* dai component */
/* tdm virtual mux to output widget */
static const char * const tdm_mux_map[] = {
	"Normal", "Dummy_Widget",
};

static int tdm_mux_map_value[] = {
	0, 1,
};

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(tdm_mux_map_enum,
					      SND_SOC_NOPM,
					      0,
					      1,
					      tdm_mux_map,
					      tdm_mux_map_value);

static const struct snd_kcontrol_new tdm_in_mux_control =
	SOC_DAPM_ENUM("TDM In Select", tdm_mux_map_enum);

static const struct snd_soc_dapm_widget mtk_dai_tdm_widgets[] = {
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_tdm_clk"),

	SND_SOC_DAPM_SUPPLY_S("TDM_EN", SUPPLY_SEQ_TDM_EN,
			      ETDM_IN1_CON0, ETDM_IN1_CON0_REG_ETDM_IN_EN_SFT,
			      0, mtk_tdm_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	/* tdm hd en */
	SND_SOC_DAPM_SUPPLY_S(TDM_HD_EN_W_NAME, SUPPLY_SEQ_TDM_HD_EN,
			      ETDM_IN1_CON2, ETDM_IN1_CON2_REG_CLOCK_SOURCE_SEL_SFT,
			      0, NULL,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S(TDM_MCLK_EN_W_NAME, SUPPLY_SEQ_TDM_MCK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_tdm_mck_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_INPUT("TDM_DUMMY_IN"),

	SND_SOC_DAPM_MUX("TDM_In_Mux",
			 SND_SOC_NOPM, 0, 0, &tdm_in_mux_control),
};

static int mtk_afe_tdm_mclk_connect(struct snd_soc_dapm_widget *source,
				    struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	return (tdm_priv->mclk_rate > 0) ? 1 : 0;
}

static int mtk_afe_tdm_mclk_apll_connect(struct snd_soc_dapm_widget *source,
					 struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];
	int cur_apll;

	/* which apll */
	cur_apll = mt8186_get_apll_by_name(afe, source->name);

	return (tdm_priv->mclk_apll == cur_apll) ? 1 : 0;
}

static int mtk_afe_tdm_hd_connect(struct snd_soc_dapm_widget *source,
				  struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	return tdm_priv->low_jitter_en;
}

static int mtk_afe_tdm_apll_connect(struct snd_soc_dapm_widget *source,
				    struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];
	int cur_apll;
	int tdm_need_apll;

	/* which apll */
	cur_apll = mt8186_get_apll_by_name(afe, source->name);

	/* choose APLL from tdm rate */
	tdm_need_apll = mt8186_get_apll_by_rate(afe, tdm_priv->rate);

	return (tdm_need_apll == cur_apll) ? 1 : 0;
}

/* low jitter control */
static const char * const mt8186_tdm_hd_str[] = {
	"Normal", "Low_Jitter"
};

static const struct soc_enum mt8186_tdm_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mt8186_tdm_hd_str),
			    mt8186_tdm_hd_str),
};

static int mt8186_tdm_hd_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(kcontrol->id.name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	ucontrol->value.integer.value[0] = tdm_priv->low_jitter_en;

	return 0;
}

static int mt8186_tdm_hd_set(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(kcontrol->id.name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	int hd_en;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	hd_en = ucontrol->value.integer.value[0];

	dev_dbg(afe->dev, "%s(), kcontrol name %s, hd_en %d\n",
		__func__, kcontrol->id.name, hd_en);

	if (tdm_priv->low_jitter_en == hd_en)
		return 0;

	tdm_priv->low_jitter_en = hd_en;

	return 1;
}

static const struct snd_kcontrol_new mtk_dai_tdm_controls[] = {
	SOC_ENUM_EXT(MTK_AFE_TDM_KCONTROL_NAME, mt8186_tdm_enum[0],
		     mt8186_tdm_hd_get, mt8186_tdm_hd_set),
};

static const struct snd_soc_dapm_route mtk_dai_tdm_routes[] = {
	{"TDM IN", NULL, "aud_tdm_clk"},
	{"TDM IN", NULL, "TDM_EN"},
	{"TDM IN", NULL, TDM_HD_EN_W_NAME, mtk_afe_tdm_hd_connect},
	{TDM_HD_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_tdm_apll_connect},
	{TDM_HD_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_tdm_apll_connect},

	{"TDM IN", NULL, TDM_MCLK_EN_W_NAME, mtk_afe_tdm_mclk_connect},
	{TDM_MCLK_EN_W_NAME, NULL, APLL1_W_NAME, mtk_afe_tdm_mclk_apll_connect},
	{TDM_MCLK_EN_W_NAME, NULL, APLL2_W_NAME, mtk_afe_tdm_mclk_apll_connect},

	/* allow tdm on without codec on */
	{"TDM IN", NULL, "TDM_In_Mux"},
	{"TDM_In_Mux", "Dummy_Widget", "TDM_DUMMY_IN"},
};

/* dai ops */
static int mtk_dai_tdm_cal_mclk(struct mtk_base_afe *afe,
				struct mtk_afe_tdm_priv *tdm_priv,
				int freq)
{
	int apll;
	int apll_rate;

	apll = mt8186_get_apll_by_rate(afe, freq);
	apll_rate = mt8186_get_apll_rate(afe, apll);

	if (!freq || freq > apll_rate) {
		dev_err(afe->dev,
			"%s(), freq(%d Hz) invalid\n", __func__, freq);
		return -EINVAL;
	}

	if (apll_rate % freq != 0) {
		dev_err(afe->dev,
			"%s(), APLL cannot generate %d Hz", __func__, freq);
		return -EINVAL;
	}

	tdm_priv->mclk_rate = freq;
	tdm_priv->mclk_apll = apll;

	return 0;
}

static int mtk_dai_tdm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int tdm_id = dai->id;
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[tdm_id];
	unsigned int tdm_mode = tdm_priv->tdm_mode;
	unsigned int data_mode = tdm_priv->data_mode;
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	snd_pcm_format_t format = params_format(params);
	unsigned int bit_width =
		snd_pcm_format_physical_width(format);
	unsigned int tdm_channels = (data_mode == TDM_DATA_ONE_PIN) ?
		get_tdm_ch_per_sdata(tdm_mode, channels) : 2;
	unsigned int lrck_width =
		get_tdm_lrck_width(format, tdm_mode);
	unsigned int tdm_con = 0;
	bool slave_mode = tdm_priv->slave_mode;
	bool lrck_inv = tdm_priv->lck_invert;
	bool bck_inv = tdm_priv->bck_invert;
	unsigned int tran_rate;
	unsigned int tran_relatch_rate;

	tdm_priv->rate = rate;
	tran_rate = mt8186_rate_transform(afe->dev, rate, dai->id);
	tran_relatch_rate = mt8186_tdm_relatch_rate_transform(afe->dev, rate);

	/* calculate mclk_rate, if not set explicitly */
	if (!tdm_priv->mclk_rate) {
		tdm_priv->mclk_rate = rate * tdm_priv->mclk_multiple;
		mtk_dai_tdm_cal_mclk(afe, tdm_priv, tdm_priv->mclk_rate);
	}

	/* ETDM_IN1_CON0 */
	tdm_con |= slave_mode << ETDM_IN1_CON0_REG_SLAVE_MODE_SFT;
	tdm_con |= tdm_mode << ETDM_IN1_CON0_REG_FMT_SFT;
	tdm_con |= (bit_width - 1) << ETDM_IN1_CON0_REG_BIT_LENGTH_SFT;
	tdm_con |= (bit_width - 1) << ETDM_IN1_CON0_REG_WORD_LENGTH_SFT;
	tdm_con |= (tdm_channels - 1) << ETDM_IN1_CON0_REG_CH_NUM_SFT;
	/* need to disable sync mode otherwise this may cause latch data error */
	tdm_con |= 0 << ETDM_IN1_CON0_REG_SYNC_MODE_SFT;
	/* relatch 1x en clock fix to h26m */
	tdm_con |= 0 << ETDM_IN1_CON0_REG_RELATCH_1X_EN_SEL_DOMAIN_SFT;
	regmap_update_bits(afe->regmap, ETDM_IN1_CON0, ETDM_IN_CON0_CTRL_MASK, tdm_con);

	/* ETDM_IN1_CON1 */
	tdm_con = 0;
	tdm_con |= 0 << ETDM_IN1_CON1_REG_LRCK_AUTO_MODE_SFT;
	tdm_con |= 1 << ETDM_IN1_CON1_PINMUX_MCLK_CTRL_OE_SFT;
	tdm_con |= (lrck_width - 1) << ETDM_IN1_CON1_REG_LRCK_WIDTH_SFT;
	regmap_update_bits(afe->regmap, ETDM_IN1_CON1, ETDM_IN_CON1_CTRL_MASK, tdm_con);

	/* ETDM_IN1_CON3 */
	tdm_con = ETDM_IN_CON3_FS(tran_rate);
	regmap_update_bits(afe->regmap, ETDM_IN1_CON3, ETDM_IN_CON3_CTRL_MASK, tdm_con);

	/* ETDM_IN1_CON4 */
	tdm_con = ETDM_IN_CON4_FS(tran_relatch_rate);
	if (slave_mode) {
		if (lrck_inv)
			tdm_con |= ETDM_IN_CON4_CON0_SLAVE_LRCK_INV;
		if (bck_inv)
			tdm_con |= ETDM_IN_CON4_CON0_SLAVE_BCK_INV;
	} else {
		if (lrck_inv)
			tdm_con |= ETDM_IN_CON4_CON0_MASTER_LRCK_INV;
		if (bck_inv)
			tdm_con |= ETDM_IN_CON4_CON0_MASTER_BCK_INV;
	}
	regmap_update_bits(afe->regmap, ETDM_IN1_CON4, ETDM_IN_CON4_CTRL_MASK, tdm_con);

	/* ETDM_IN1_CON2 */
	tdm_con = 0;
	if (data_mode == TDM_DATA_MULTI_PIN) {
		tdm_con |= ETDM_IN_CON2_MULTI_IP_2CH_MODE;
		tdm_con |= ETDM_IN_CON2_MULTI_IP_CH(channels);
	}
	regmap_update_bits(afe->regmap, ETDM_IN1_CON2, ETDM_IN_CON2_CTRL_MASK, tdm_con);

	/* ETDM_IN1_CON8 */
	tdm_con = 0;
	if (slave_mode) {
		tdm_con |= 1 << ETDM_IN1_CON8_REG_ETDM_USE_AFIFO_SFT;
		tdm_con |= 0 << ETDM_IN1_CON8_REG_AFIFO_CLOCK_DOMAIN_SEL_SFT;
		tdm_con |= ETDM_IN_CON8_FS(tran_relatch_rate);
	} else {
		tdm_con |= 0 << ETDM_IN1_CON8_REG_ETDM_USE_AFIFO_SFT;
	}
	regmap_update_bits(afe->regmap, ETDM_IN1_CON8, ETDM_IN_CON8_CTRL_MASK, tdm_con);

	return 0;
}

static int mtk_dai_tdm_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai->id];

	if (dir != SND_SOC_CLOCK_IN) {
		dev_err(afe->dev, "%s(), dir != SND_SOC_CLOCK_OUT", __func__);
		return -EINVAL;
	}

	dev_dbg(afe->dev, "%s(), freq %d\n", __func__, freq);

	return mtk_dai_tdm_cal_mclk(afe, tdm_priv, freq);
}

static int mtk_dai_tdm_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai->id];

	/* DAI mode*/
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		tdm_priv->tdm_mode = TDM_IN_I2S;
		tdm_priv->data_mode = TDM_DATA_MULTI_PIN;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		tdm_priv->tdm_mode = TDM_IN_LJ;
		tdm_priv->data_mode = TDM_DATA_MULTI_PIN;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		tdm_priv->tdm_mode = TDM_IN_RJ;
		tdm_priv->data_mode = TDM_DATA_MULTI_PIN;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		tdm_priv->tdm_mode = TDM_IN_DSP_A;
		tdm_priv->data_mode = TDM_DATA_ONE_PIN;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		tdm_priv->tdm_mode = TDM_IN_DSP_B;
		tdm_priv->data_mode = TDM_DATA_ONE_PIN;
		break;
	default:
		dev_err(afe->dev, "%s(), invalid DAIFMT_FORMAT_MASK", __func__);
		return -EINVAL;
	}

	/* DAI clock inversion*/
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		tdm_priv->bck_invert = TDM_BCK_NON_INV;
		tdm_priv->lck_invert = TDM_LCK_NON_INV;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		tdm_priv->bck_invert = TDM_BCK_NON_INV;
		tdm_priv->lck_invert = TDM_LCK_INV;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		tdm_priv->bck_invert = TDM_BCK_INV;
		tdm_priv->lck_invert = TDM_LCK_NON_INV;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		tdm_priv->bck_invert = TDM_BCK_INV;
		tdm_priv->lck_invert = TDM_LCK_INV;
		break;
	default:
		dev_err(afe->dev, "%s(), invalid DAIFMT_INV_MASK", __func__);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		tdm_priv->slave_mode = false;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		tdm_priv->slave_mode = true;
		break;
	default:
		dev_err(afe->dev, "%s(), invalid DAIFMT_CLOCK_PROVIDER_MASK",
			__func__);
		return -EINVAL;
	}

	return 0;
}

static int mtk_dai_tdm_set_tdm_slot(struct snd_soc_dai *dai,
				    unsigned int tx_mask,
				    unsigned int rx_mask,
				    int slots,
				    int slot_width)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai->id];

	dev_dbg(dai->dev, "%s %d slot_width %d\n", __func__, dai->id, slot_width);

	tdm_priv->lrck_width = slot_width;

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_tdm_ops = {
	.hw_params = mtk_dai_tdm_hw_params,
	.set_sysclk = mtk_dai_tdm_set_sysclk,
	.set_fmt = mtk_dai_tdm_set_fmt,
	.set_tdm_slot = mtk_dai_tdm_set_tdm_slot,
};

/* dai driver */
#define MTK_TDM_RATES (SNDRV_PCM_RATE_8000_48000 |\
		       SNDRV_PCM_RATE_88200 |\
		       SNDRV_PCM_RATE_96000 |\
		       SNDRV_PCM_RATE_176400 |\
		       SNDRV_PCM_RATE_192000)

#define MTK_TDM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_tdm_driver[] = {
	{
		.name = "TDM IN",
		.id = MT8186_DAI_TDM_IN,
		.capture = {
			.stream_name = "TDM IN",
			.channels_min = 2,
			.channels_max = 8,
			.rates = MTK_TDM_RATES,
			.formats = MTK_TDM_FORMATS,
		},
		.ops = &mtk_dai_tdm_ops,
	},
};

static struct mtk_afe_tdm_priv *init_tdm_priv_data(struct mtk_base_afe *afe)
{
	struct mtk_afe_tdm_priv *tdm_priv;

	tdm_priv = devm_kzalloc(afe->dev, sizeof(struct mtk_afe_tdm_priv),
				GFP_KERNEL);
	if (!tdm_priv)
		return NULL;

	tdm_priv->mclk_multiple = 512;
	tdm_priv->mclk_id = MT8186_TDM_MCK;
	tdm_priv->id = MT8186_DAI_TDM_IN;

	return tdm_priv;
}

int mt8186_dai_tdm_register(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_tdm_priv *tdm_priv;
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_tdm_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_tdm_driver);

	dai->controls = mtk_dai_tdm_controls;
	dai->num_controls = ARRAY_SIZE(mtk_dai_tdm_controls);
	dai->dapm_widgets = mtk_dai_tdm_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_tdm_widgets);
	dai->dapm_routes = mtk_dai_tdm_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_tdm_routes);

	tdm_priv = init_tdm_priv_data(afe);
	if (!tdm_priv)
		return -ENOMEM;

	afe_priv->dai_priv[MT8186_DAI_TDM_IN] = tdm_priv;

	return 0;
}
