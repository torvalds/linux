// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI TDM Control
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Shane Chien <shane.chien@mediatek.com>

#include <linux/regmap.h>
#include <sound/pcm_params.h>

#include "mt8192-afe-clk.h"
#include "mt8192-afe-common.h"
#include "mt8192-afe-gpio.h"
#include "mt8192-interconnection.h"

struct mtk_afe_tdm_priv {
	int id;
	int bck_id;
	int bck_rate;
	int tdm_out_mode;
	int bck_invert;
	int lck_invert;
	int mclk_id;
	int mclk_multiple; /* according to sample rate */
	int mclk_rate;
	int mclk_apll;
};

enum {
	TDM_OUT_I2S = 0,
	TDM_OUT_DSP_A = 1,
	TDM_OUT_DSP_B = 2,
};

enum {
	TDM_BCK_NON_INV = 0,
	TDM_BCK_INV = 1,
};

enum {
	TDM_LCK_NON_INV = 0,
	TDM_LCK_INV = 1,
};

enum {
	TDM_WLEN_16_BIT = 1,
	TDM_WLEN_32_BIT = 2,
};

enum {
	TDM_CHANNEL_BCK_16 = 0,
	TDM_CHANNEL_BCK_24 = 1,
	TDM_CHANNEL_BCK_32 = 2,
};

enum {
	TDM_CHANNEL_NUM_2 = 0,
	TDM_CHANNEL_NUM_4 = 1,
	TDM_CHANNEL_NUM_8 = 2,
};

enum  {
	TDM_CH_START_O30_O31 = 0,
	TDM_CH_START_O32_O33,
	TDM_CH_START_O34_O35,
	TDM_CH_START_O36_O37,
	TDM_CH_ZERO,
};

static unsigned int get_tdm_wlen(snd_pcm_format_t format)
{
	return snd_pcm_format_physical_width(format) <= 16 ?
	       TDM_WLEN_16_BIT : TDM_WLEN_32_BIT;
}

static unsigned int get_tdm_channel_bck(snd_pcm_format_t format)
{
	return snd_pcm_format_physical_width(format) <= 16 ?
	       TDM_CHANNEL_BCK_16 : TDM_CHANNEL_BCK_32;
}

static unsigned int get_tdm_lrck_width(snd_pcm_format_t format)
{
	return snd_pcm_format_physical_width(format) - 1;
}

static unsigned int get_tdm_ch(unsigned int ch)
{
	switch (ch) {
	case 1:
	case 2:
		return TDM_CHANNEL_NUM_2;
	case 3:
	case 4:
		return TDM_CHANNEL_NUM_4;
	case 5:
	case 6:
	case 7:
	case 8:
	default:
		return TDM_CHANNEL_NUM_8;
	}
}

static unsigned int get_tdm_ch_fixup(unsigned int channels)
{
	if (channels > 4)
		return 8;
	else if (channels > 2)
		return 4;
	else
		return 2;
}

static unsigned int get_tdm_ch_per_sdata(unsigned int mode,
					 unsigned int channels)
{
	if (mode == TDM_OUT_DSP_A || mode == TDM_OUT_DSP_B)
		return get_tdm_ch_fixup(channels);
	else
		return 2;
}

/* interconnection */
enum {
	HDMI_CONN_CH0 = 0,
	HDMI_CONN_CH1,
	HDMI_CONN_CH2,
	HDMI_CONN_CH3,
	HDMI_CONN_CH4,
	HDMI_CONN_CH5,
	HDMI_CONN_CH6,
	HDMI_CONN_CH7,
};

static const char *const hdmi_conn_mux_map[] = {
	"CH0", "CH1", "CH2", "CH3",
	"CH4", "CH5", "CH6", "CH7",
};

static int hdmi_conn_mux_map_value[] = {
	HDMI_CONN_CH0,
	HDMI_CONN_CH1,
	HDMI_CONN_CH2,
	HDMI_CONN_CH3,
	HDMI_CONN_CH4,
	HDMI_CONN_CH5,
	HDMI_CONN_CH6,
	HDMI_CONN_CH7,
};

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch0_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_0_SFT,
				  HDMI_O_0_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch0_mux_control =
	SOC_DAPM_ENUM("HDMI_CH0_MUX", hdmi_ch0_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch1_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_1_SFT,
				  HDMI_O_1_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch1_mux_control =
	SOC_DAPM_ENUM("HDMI_CH1_MUX", hdmi_ch1_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch2_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_2_SFT,
				  HDMI_O_2_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch2_mux_control =
	SOC_DAPM_ENUM("HDMI_CH2_MUX", hdmi_ch2_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch3_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_3_SFT,
				  HDMI_O_3_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch3_mux_control =
	SOC_DAPM_ENUM("HDMI_CH3_MUX", hdmi_ch3_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch4_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_4_SFT,
				  HDMI_O_4_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch4_mux_control =
	SOC_DAPM_ENUM("HDMI_CH4_MUX", hdmi_ch4_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch5_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_5_SFT,
				  HDMI_O_5_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch5_mux_control =
	SOC_DAPM_ENUM("HDMI_CH5_MUX", hdmi_ch5_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch6_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_6_SFT,
				  HDMI_O_6_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch6_mux_control =
	SOC_DAPM_ENUM("HDMI_CH6_MUX", hdmi_ch6_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(hdmi_ch7_mux_map_enum,
				  AFE_HDMI_CONN0,
				  HDMI_O_7_SFT,
				  HDMI_O_7_MASK,
				  hdmi_conn_mux_map,
				  hdmi_conn_mux_map_value);

static const struct snd_kcontrol_new hdmi_ch7_mux_control =
	SOC_DAPM_ENUM("HDMI_CH7_MUX", hdmi_ch7_mux_map_enum);

enum {
	SUPPLY_SEQ_APLL,
	SUPPLY_SEQ_TDM_MCK_EN,
	SUPPLY_SEQ_TDM_BCK_EN,
	SUPPLY_SEQ_TDM_EN,
};

static int get_tdm_id_by_name(const char *name)
{
	return MT8192_DAI_TDM;
}

static int mtk_tdm_en_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	if (!tdm_priv) {
		dev_warn(afe->dev, "%s(), tdm_priv == NULL", __func__);
		return -EINVAL;
	}

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8192_afe_gpio_request(afe->dev, true, tdm_priv->id, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		mt8192_afe_gpio_request(afe->dev, false, tdm_priv->id, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_tdm_bck_en_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol,
				int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	if (!tdm_priv) {
		dev_warn(afe->dev, "%s(), tdm_priv == NULL", __func__);
		return -EINVAL;
	}

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x, dai_id %d\n",
		__func__, w->name, event, dai_id);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8192_mck_enable(afe, tdm_priv->bck_id, tdm_priv->bck_rate);
		break;
	case SND_SOC_DAPM_POST_PMD:
		mt8192_mck_disable(afe, tdm_priv->bck_id);
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
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];

	if (!tdm_priv) {
		dev_warn(afe->dev, "%s(), tdm_priv == NULL", __func__);
		return -EINVAL;
	}

	dev_dbg(cmpnt->dev, "%s(), name %s, event 0x%x, dai_id %d\n",
		__func__, w->name, event, dai_id);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8192_mck_enable(afe, tdm_priv->mclk_id, tdm_priv->mclk_rate);
		break;
	case SND_SOC_DAPM_POST_PMD:
		tdm_priv->mclk_rate = 0;
		mt8192_mck_disable(afe, tdm_priv->mclk_id);
		break;
	default:
		break;
	}

	return 0;
}

static const struct snd_soc_dapm_widget mtk_dai_tdm_widgets[] = {
	SND_SOC_DAPM_MUX("HDMI_CH0_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch0_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH1_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch1_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH2_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch2_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH3_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch3_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH4_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch4_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH5_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch5_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH6_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch6_mux_control),
	SND_SOC_DAPM_MUX("HDMI_CH7_MUX", SND_SOC_NOPM, 0, 0,
			 &hdmi_ch7_mux_control),

	SND_SOC_DAPM_CLOCK_SUPPLY("aud_tdm_clk"),

	SND_SOC_DAPM_SUPPLY_S("TDM_EN", SUPPLY_SEQ_TDM_EN,
			      AFE_TDM_CON1, TDM_EN_SFT, 0,
			      mtk_tdm_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("TDM_BCK", SUPPLY_SEQ_TDM_BCK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_tdm_bck_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("TDM_MCK", SUPPLY_SEQ_TDM_MCK_EN,
			      SND_SOC_NOPM, 0, 0,
			      mtk_tdm_mck_en_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static int mtk_afe_tdm_apll_connect(struct snd_soc_dapm_widget *source,
				    struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = sink;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int dai_id = get_tdm_id_by_name(w->name);
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai_id];
	int cur_apll;

	/* which apll */
	cur_apll = mt8192_get_apll_by_name(afe, source->name);

	return (tdm_priv->mclk_apll == cur_apll) ? 1 : 0;
}

static const struct snd_soc_dapm_route mtk_dai_tdm_routes[] = {
	{"HDMI_CH0_MUX", "CH0", "HDMI"},
	{"HDMI_CH0_MUX", "CH1", "HDMI"},
	{"HDMI_CH0_MUX", "CH2", "HDMI"},
	{"HDMI_CH0_MUX", "CH3", "HDMI"},
	{"HDMI_CH0_MUX", "CH4", "HDMI"},
	{"HDMI_CH0_MUX", "CH5", "HDMI"},
	{"HDMI_CH0_MUX", "CH6", "HDMI"},
	{"HDMI_CH0_MUX", "CH7", "HDMI"},

	{"HDMI_CH1_MUX", "CH0", "HDMI"},
	{"HDMI_CH1_MUX", "CH1", "HDMI"},
	{"HDMI_CH1_MUX", "CH2", "HDMI"},
	{"HDMI_CH1_MUX", "CH3", "HDMI"},
	{"HDMI_CH1_MUX", "CH4", "HDMI"},
	{"HDMI_CH1_MUX", "CH5", "HDMI"},
	{"HDMI_CH1_MUX", "CH6", "HDMI"},
	{"HDMI_CH1_MUX", "CH7", "HDMI"},

	{"HDMI_CH2_MUX", "CH0", "HDMI"},
	{"HDMI_CH2_MUX", "CH1", "HDMI"},
	{"HDMI_CH2_MUX", "CH2", "HDMI"},
	{"HDMI_CH2_MUX", "CH3", "HDMI"},
	{"HDMI_CH2_MUX", "CH4", "HDMI"},
	{"HDMI_CH2_MUX", "CH5", "HDMI"},
	{"HDMI_CH2_MUX", "CH6", "HDMI"},
	{"HDMI_CH2_MUX", "CH7", "HDMI"},

	{"HDMI_CH3_MUX", "CH0", "HDMI"},
	{"HDMI_CH3_MUX", "CH1", "HDMI"},
	{"HDMI_CH3_MUX", "CH2", "HDMI"},
	{"HDMI_CH3_MUX", "CH3", "HDMI"},
	{"HDMI_CH3_MUX", "CH4", "HDMI"},
	{"HDMI_CH3_MUX", "CH5", "HDMI"},
	{"HDMI_CH3_MUX", "CH6", "HDMI"},
	{"HDMI_CH3_MUX", "CH7", "HDMI"},

	{"HDMI_CH4_MUX", "CH0", "HDMI"},
	{"HDMI_CH4_MUX", "CH1", "HDMI"},
	{"HDMI_CH4_MUX", "CH2", "HDMI"},
	{"HDMI_CH4_MUX", "CH3", "HDMI"},
	{"HDMI_CH4_MUX", "CH4", "HDMI"},
	{"HDMI_CH4_MUX", "CH5", "HDMI"},
	{"HDMI_CH4_MUX", "CH6", "HDMI"},
	{"HDMI_CH4_MUX", "CH7", "HDMI"},

	{"HDMI_CH5_MUX", "CH0", "HDMI"},
	{"HDMI_CH5_MUX", "CH1", "HDMI"},
	{"HDMI_CH5_MUX", "CH2", "HDMI"},
	{"HDMI_CH5_MUX", "CH3", "HDMI"},
	{"HDMI_CH5_MUX", "CH4", "HDMI"},
	{"HDMI_CH5_MUX", "CH5", "HDMI"},
	{"HDMI_CH5_MUX", "CH6", "HDMI"},
	{"HDMI_CH5_MUX", "CH7", "HDMI"},

	{"HDMI_CH6_MUX", "CH0", "HDMI"},
	{"HDMI_CH6_MUX", "CH1", "HDMI"},
	{"HDMI_CH6_MUX", "CH2", "HDMI"},
	{"HDMI_CH6_MUX", "CH3", "HDMI"},
	{"HDMI_CH6_MUX", "CH4", "HDMI"},
	{"HDMI_CH6_MUX", "CH5", "HDMI"},
	{"HDMI_CH6_MUX", "CH6", "HDMI"},
	{"HDMI_CH6_MUX", "CH7", "HDMI"},

	{"HDMI_CH7_MUX", "CH0", "HDMI"},
	{"HDMI_CH7_MUX", "CH1", "HDMI"},
	{"HDMI_CH7_MUX", "CH2", "HDMI"},
	{"HDMI_CH7_MUX", "CH3", "HDMI"},
	{"HDMI_CH7_MUX", "CH4", "HDMI"},
	{"HDMI_CH7_MUX", "CH5", "HDMI"},
	{"HDMI_CH7_MUX", "CH6", "HDMI"},
	{"HDMI_CH7_MUX", "CH7", "HDMI"},

	{"TDM", NULL, "HDMI_CH0_MUX"},
	{"TDM", NULL, "HDMI_CH1_MUX"},
	{"TDM", NULL, "HDMI_CH2_MUX"},
	{"TDM", NULL, "HDMI_CH3_MUX"},
	{"TDM", NULL, "HDMI_CH4_MUX"},
	{"TDM", NULL, "HDMI_CH5_MUX"},
	{"TDM", NULL, "HDMI_CH6_MUX"},
	{"TDM", NULL, "HDMI_CH7_MUX"},

	{"TDM", NULL, "aud_tdm_clk"},
	{"TDM", NULL, "TDM_BCK"},
	{"TDM", NULL, "TDM_EN"},
	{"TDM_BCK", NULL, "TDM_MCK"},
	{"TDM_MCK", NULL, APLL1_W_NAME, mtk_afe_tdm_apll_connect},
	{"TDM_MCK", NULL, APLL2_W_NAME, mtk_afe_tdm_apll_connect},
};

/* dai ops */
static int mtk_dai_tdm_cal_mclk(struct mtk_base_afe *afe,
				struct mtk_afe_tdm_priv *tdm_priv,
				int freq)
{
	int apll;
	int apll_rate;

	apll = mt8192_get_apll_by_rate(afe, freq);
	apll_rate = mt8192_get_apll_rate(afe, apll);

	if (!freq || freq > apll_rate) {
		dev_warn(afe->dev,
			 "%s(), freq(%d Hz) invalid\n", __func__, freq);
		return -EINVAL;
	}

	if (apll_rate % freq != 0) {
		dev_warn(afe->dev,
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
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int tdm_id = dai->id;
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[tdm_id];
	unsigned int tdm_out_mode = tdm_priv->tdm_out_mode;
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	unsigned int out_channels_per_sdata =
		get_tdm_ch_per_sdata(tdm_out_mode, channels);
	snd_pcm_format_t format = params_format(params);
	unsigned int tdm_con = 0;

	/* calculate mclk_rate, if not set explicitly */
	if (!tdm_priv->mclk_rate) {
		tdm_priv->mclk_rate = rate * tdm_priv->mclk_multiple;
		mtk_dai_tdm_cal_mclk(afe,
				     tdm_priv,
				     tdm_priv->mclk_rate);
	}

	/* calculate bck */
	tdm_priv->bck_rate = rate *
			     out_channels_per_sdata *
			     snd_pcm_format_physical_width(format);

	if (tdm_priv->bck_rate > tdm_priv->mclk_rate)
		dev_warn(afe->dev, "%s(), bck_rate > mclk_rate rate", __func__);

	if (tdm_priv->mclk_rate % tdm_priv->bck_rate != 0)
		dev_warn(afe->dev, "%s(), bck cannot generate", __func__);

	dev_dbg(afe->dev, "%s(), id %d, rate %d, channels %d, format %d, mclk_rate %d, bck_rate %d\n",
		__func__,
		tdm_id, rate, channels, format,
		tdm_priv->mclk_rate, tdm_priv->bck_rate);

	dev_dbg(afe->dev, "%s(), out_channels_per_sdata = %d\n",
		__func__, out_channels_per_sdata);

	/* set tdm */
	if (tdm_priv->bck_invert)
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON3,
				   BCK_INVERSE_MASK_SFT,
				   0x1 << BCK_INVERSE_SFT);

	if (tdm_priv->lck_invert)
		tdm_con |= 1 << LRCK_INVERSE_SFT;

	if (tdm_priv->tdm_out_mode == TDM_OUT_I2S) {
		tdm_con |= 1 << DELAY_DATA_SFT;
		tdm_con |= get_tdm_lrck_width(format) << LRCK_TDM_WIDTH_SFT;
	} else if (tdm_priv->tdm_out_mode == TDM_OUT_DSP_A) {
		tdm_con |= 0 << DELAY_DATA_SFT;
		tdm_con |= 0 << LRCK_TDM_WIDTH_SFT;
	} else if (tdm_priv->tdm_out_mode == TDM_OUT_DSP_B) {
		tdm_con |= 1 << DELAY_DATA_SFT;
		tdm_con |= 0 << LRCK_TDM_WIDTH_SFT;
	}

	tdm_con |= 1 << LEFT_ALIGN_SFT;
	tdm_con |= get_tdm_wlen(format) << WLEN_SFT;
	tdm_con |= get_tdm_ch(out_channels_per_sdata) << CHANNEL_NUM_SFT;
	tdm_con |= get_tdm_channel_bck(format) << CHANNEL_BCK_CYCLES_SFT;
	regmap_write(afe->regmap, AFE_TDM_CON1, tdm_con);

	if (out_channels_per_sdata == 2) {
		switch (channels) {
		case 1:
		case 2:
			tdm_con = TDM_CH_START_O30_O31 << ST_CH_PAIR_SOUT0_SFT;
			tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT1_SFT;
			tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT2_SFT;
			tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT3_SFT;
			break;
		case 3:
		case 4:
			tdm_con = TDM_CH_START_O30_O31 << ST_CH_PAIR_SOUT0_SFT;
			tdm_con |= TDM_CH_START_O32_O33 << ST_CH_PAIR_SOUT1_SFT;
			tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT2_SFT;
			tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT3_SFT;
			break;
		case 5:
		case 6:
			tdm_con = TDM_CH_START_O30_O31 << ST_CH_PAIR_SOUT0_SFT;
			tdm_con |= TDM_CH_START_O32_O33 << ST_CH_PAIR_SOUT1_SFT;
			tdm_con |= TDM_CH_START_O34_O35 << ST_CH_PAIR_SOUT2_SFT;
			tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT3_SFT;
			break;
		case 7:
		case 8:
			tdm_con = TDM_CH_START_O30_O31 << ST_CH_PAIR_SOUT0_SFT;
			tdm_con |= TDM_CH_START_O32_O33 << ST_CH_PAIR_SOUT1_SFT;
			tdm_con |= TDM_CH_START_O34_O35 << ST_CH_PAIR_SOUT2_SFT;
			tdm_con |= TDM_CH_START_O36_O37 << ST_CH_PAIR_SOUT3_SFT;
			break;
		default:
			tdm_con = 0;
		}
	} else {
		tdm_con = TDM_CH_START_O30_O31 << ST_CH_PAIR_SOUT0_SFT;
		tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT1_SFT;
		tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT2_SFT;
		tdm_con |= TDM_CH_ZERO << ST_CH_PAIR_SOUT3_SFT;
	}

	regmap_write(afe->regmap, AFE_TDM_CON2, tdm_con);

	regmap_update_bits(afe->regmap, AFE_HDMI_OUT_CON0,
			   HDMI_CH_NUM_MASK_SFT,
			   channels << HDMI_CH_NUM_SFT);
	return 0;
}

static int mtk_dai_tdm_set_sysclk(struct snd_soc_dai *dai,
				  int clk_id, unsigned int freq, int dir)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai->id];

	if (!tdm_priv) {
		dev_warn(afe->dev, "%s(), tdm_priv == NULL", __func__);
		return -EINVAL;
	}

	if (dir != SND_SOC_CLOCK_OUT) {
		dev_warn(afe->dev, "%s(), dir != SND_SOC_CLOCK_OUT", __func__);
		return -EINVAL;
	}

	dev_dbg(afe->dev, "%s(), freq %d\n", __func__, freq);

	return mtk_dai_tdm_cal_mclk(afe, tdm_priv, freq);
}

static int mtk_dai_tdm_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dai->dev);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_tdm_priv *tdm_priv = afe_priv->dai_priv[dai->id];

	if (!tdm_priv) {
		dev_warn(afe->dev, "%s(), tdm_priv == NULL", __func__);
		return -EINVAL;
	}

	/* DAI mode*/
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		tdm_priv->tdm_out_mode = TDM_OUT_I2S;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		tdm_priv->tdm_out_mode = TDM_OUT_DSP_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		tdm_priv->tdm_out_mode = TDM_OUT_DSP_B;
		break;
	default:
		tdm_priv->tdm_out_mode = TDM_OUT_I2S;
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
	default:
		tdm_priv->bck_invert = TDM_BCK_INV;
		tdm_priv->lck_invert = TDM_LCK_INV;
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_tdm_ops = {
	.hw_params = mtk_dai_tdm_hw_params,
	.set_sysclk = mtk_dai_tdm_set_sysclk,
	.set_fmt = mtk_dai_tdm_set_fmt,
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
		.name = "TDM",
		.id = MT8192_DAI_TDM,
		.playback = {
			.stream_name = "TDM",
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
	tdm_priv->bck_id = MT8192_I2S4_BCK;
	tdm_priv->mclk_id = MT8192_I2S4_MCK;
	tdm_priv->id = MT8192_DAI_TDM;

	return tdm_priv;
}

int mt8192_dai_tdm_register(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_tdm_priv *tdm_priv;
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_tdm_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_tdm_driver);

	dai->dapm_widgets = mtk_dai_tdm_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_tdm_widgets);
	dai->dapm_routes = mtk_dai_tdm_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_tdm_routes);

	tdm_priv = init_tdm_priv_data(afe);
	if (!tdm_priv)
		return -ENOMEM;

	afe_priv->dai_priv[MT8192_DAI_TDM] = tdm_priv;

	return 0;
}
