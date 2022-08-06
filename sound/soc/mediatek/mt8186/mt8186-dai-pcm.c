// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI I2S Control
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt8186-afe-common.h"
#include "mt8186-afe-gpio.h"
#include "mt8186-interconnection.h"

struct mtk_afe_pcm_priv {
	unsigned int id;
	unsigned int fmt;
	unsigned int bck_invert;
	unsigned int lck_invert;
};

enum aud_tx_lch_rpt {
	AUD_TX_LCH_RPT_NO_REPEAT = 0,
	AUD_TX_LCH_RPT_REPEAT = 1
};

enum aud_vbt_16k_mode {
	AUD_VBT_16K_MODE_DISABLE = 0,
	AUD_VBT_16K_MODE_ENABLE = 1
};

enum aud_ext_modem {
	AUD_EXT_MODEM_SELECT_INTERNAL = 0,
	AUD_EXT_MODEM_SELECT_EXTERNAL = 1
};

enum aud_pcm_sync_type {
	/* bck sync length = 1 */
	AUD_PCM_ONE_BCK_CYCLE_SYNC = 0,
	/* bck sync length = PCM_INTF_CON1[9:13] */
	AUD_PCM_EXTENDED_BCK_CYCLE_SYNC = 1
};

enum aud_bt_mode {
	AUD_BT_MODE_DUAL_MIC_ON_TX = 0,
	AUD_BT_MODE_SINGLE_MIC_ON_TX = 1
};

enum aud_pcm_afifo_src {
	/* slave mode & external modem uses different crystal */
	AUD_PCM_AFIFO_ASRC = 0,
	/* slave mode & external modem uses the same crystal */
	AUD_PCM_AFIFO_AFIFO = 1
};

enum aud_pcm_clock_source {
	AUD_PCM_CLOCK_MASTER_MODE = 0,
	AUD_PCM_CLOCK_SLAVE_MODE = 1
};

enum aud_pcm_wlen {
	AUD_PCM_WLEN_PCM_32_BCK_CYCLES = 0,
	AUD_PCM_WLEN_PCM_64_BCK_CYCLES = 1
};

enum aud_pcm_24bit {
	AUD_PCM_24BIT_PCM_16_BITS = 0,
	AUD_PCM_24BIT_PCM_24_BITS = 1
};

enum aud_pcm_mode {
	AUD_PCM_MODE_PCM_MODE_8K = 0,
	AUD_PCM_MODE_PCM_MODE_16K = 1,
	AUD_PCM_MODE_PCM_MODE_32K = 2,
	AUD_PCM_MODE_PCM_MODE_48K = 3,
};

enum aud_pcm_fmt {
	AUD_PCM_FMT_I2S = 0,
	AUD_PCM_FMT_EIAJ = 1,
	AUD_PCM_FMT_PCM_MODE_A = 2,
	AUD_PCM_FMT_PCM_MODE_B = 3
};

enum aud_bclk_out_inv {
	AUD_BCLK_OUT_INV_NO_INVERSE = 0,
	AUD_BCLK_OUT_INV_INVERSE = 1
};

enum aud_lrclk_out_inv {
	AUD_LRCLK_OUT_INV_NO_INVERSE = 0,
	AUD_LRCLK_OUT_INV_INVERSE = 1
};

enum aud_pcm_en {
	AUD_PCM_EN_DISABLE = 0,
	AUD_PCM_EN_ENABLE = 1
};

/* dai component */
static const struct snd_kcontrol_new mtk_pcm_1_playback_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1 Switch", AFE_CONN7,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1 Switch", AFE_CONN7,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1 Switch", AFE_CONN7_1,
				    I_DL4_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_pcm_1_playback_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2 Switch", AFE_CONN8,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2 Switch", AFE_CONN8,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2 Switch", AFE_CONN8_1,
				    I_DL4_CH2, 1, 0),
};

static int mtk_pcm_en_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8186_afe_gpio_request(afe->dev, true, MT8186_DAI_PCM, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		mt8186_afe_gpio_request(afe->dev, false, MT8186_DAI_PCM, 0);
		break;
	}

	return 0;
}

/* pcm in/out lpbk */
static const char * const pcm_lpbk_mux_map[] = {
	"Normal", "Lpbk",
};

static int pcm_lpbk_mux_map_value[] = {
	0, 1,
};

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(pcm_in_lpbk_mux_map_enum,
					      PCM_INTF_CON1,
					      PCM_I2S_PCM_LOOPBACK_SFT,
					      1,
					      pcm_lpbk_mux_map,
					      pcm_lpbk_mux_map_value);

static const struct snd_kcontrol_new pcm_in_lpbk_mux_control =
	SOC_DAPM_ENUM("PCM In Lpbk Select", pcm_in_lpbk_mux_map_enum);

static SOC_VALUE_ENUM_SINGLE_AUTODISABLE_DECL(pcm_out_lpbk_mux_map_enum,
					      PCM_INTF_CON1,
					      PCM_I2S_PCM_LOOPBACK_SFT,
					      1,
					      pcm_lpbk_mux_map,
					      pcm_lpbk_mux_map_value);

static const struct snd_kcontrol_new pcm_out_lpbk_mux_control =
	SOC_DAPM_ENUM("PCM Out Lpbk Select", pcm_out_lpbk_mux_map_enum);

static const struct snd_soc_dapm_widget mtk_dai_pcm_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("PCM_1_PB_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_1_playback_ch1_mix,
			   ARRAY_SIZE(mtk_pcm_1_playback_ch1_mix)),
	SND_SOC_DAPM_MIXER("PCM_1_PB_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_1_playback_ch2_mix,
			   ARRAY_SIZE(mtk_pcm_1_playback_ch2_mix)),

	SND_SOC_DAPM_SUPPLY("PCM_1_EN",
			    PCM_INTF_CON1, PCM_EN_SFT, 0,
			    mtk_pcm_en_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	/* pcm in lpbk */
	SND_SOC_DAPM_MUX("PCM_In_Lpbk_Mux",
			 SND_SOC_NOPM, 0, 0, &pcm_in_lpbk_mux_control),

	/* pcm out lpbk */
	SND_SOC_DAPM_MUX("PCM_Out_Lpbk_Mux",
			 SND_SOC_NOPM, 0, 0, &pcm_out_lpbk_mux_control),
};

static const struct snd_soc_dapm_route mtk_dai_pcm_routes[] = {
	{"PCM 1 Playback", NULL, "PCM_1_PB_CH1"},
	{"PCM 1 Playback", NULL, "PCM_1_PB_CH2"},

	{"PCM 1 Playback", NULL, "PCM_1_EN"},
	{"PCM 1 Capture", NULL, "PCM_1_EN"},

	{"PCM_1_PB_CH1", "DL2_CH1 Switch", "DL2"},
	{"PCM_1_PB_CH2", "DL2_CH2 Switch", "DL2"},

	{"PCM_1_PB_CH1", "DL4_CH1 Switch", "DL4"},
	{"PCM_1_PB_CH2", "DL4_CH2 Switch", "DL4"},

	/* pcm out lpbk */
	{"PCM_Out_Lpbk_Mux", "Lpbk", "PCM 1 Playback"},
	{"I2S0", NULL, "PCM_Out_Lpbk_Mux"},

	/* pcm in lpbk */
	{"PCM_In_Lpbk_Mux", "Lpbk", "PCM 1 Capture"},
	{"I2S3", NULL, "PCM_In_Lpbk_Mux"},
};

/* dai ops */
static int mtk_dai_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int pcm_id = dai->id;
	struct mtk_afe_pcm_priv *pcm_priv = afe_priv->dai_priv[pcm_id];
	unsigned int rate = params_rate(params);
	unsigned int rate_reg = mt8186_rate_transform(afe->dev, rate, dai->id);
	snd_pcm_format_t format = params_format(params);
	unsigned int data_width =
		snd_pcm_format_width(format);
	unsigned int wlen_width =
		snd_pcm_format_physical_width(format);
	unsigned int pcm_con = 0;

	dev_dbg(afe->dev, "%s(), id %d, stream %d, widget active p %d, c %d\n",
		__func__, dai->id, substream->stream, dai->playback_widget->active,
		dai->capture_widget->active);
	dev_dbg(afe->dev, "%s(), rate %d, rate_reg %d, data_width %d, wlen_width %d\n",
		__func__, rate, rate_reg, data_width, wlen_width);

	if (dai->playback_widget->active || dai->capture_widget->active)
		return 0;

	switch (dai->id) {
	case MT8186_DAI_PCM:
		pcm_con |= AUD_TX_LCH_RPT_NO_REPEAT << PCM_TX_LCH_RPT_SFT;
		pcm_con |= AUD_VBT_16K_MODE_DISABLE << PCM_VBT_16K_MODE_SFT;
		pcm_con |= AUD_EXT_MODEM_SELECT_EXTERNAL << PCM_EXT_MODEM_SFT;
		pcm_con |= AUD_PCM_ONE_BCK_CYCLE_SYNC << PCM_SYNC_TYPE_SFT;
		pcm_con |= AUD_BT_MODE_DUAL_MIC_ON_TX << PCM_BT_MODE_SFT;
		pcm_con |= AUD_PCM_AFIFO_AFIFO << PCM_BYP_ASRC_SFT;
		pcm_con |= AUD_PCM_CLOCK_MASTER_MODE << PCM_SLAVE_SFT;
		pcm_con |= 0 << PCM_SYNC_LENGTH_SFT;

		/* sampling rate */
		pcm_con |= rate_reg << PCM_MODE_SFT;

		/* format */
		pcm_con |= pcm_priv->fmt << PCM_FMT_SFT;

		/* 24bit data width */
		if (data_width > 16)
			pcm_con |= AUD_PCM_24BIT_PCM_24_BITS << PCM_24BIT_SFT;
		else
			pcm_con |= AUD_PCM_24BIT_PCM_16_BITS << PCM_24BIT_SFT;

		/* wlen width*/
		if (wlen_width > 16)
			pcm_con |= AUD_PCM_WLEN_PCM_64_BCK_CYCLES << PCM_WLEN_SFT;
		else
			pcm_con |= AUD_PCM_WLEN_PCM_32_BCK_CYCLES << PCM_WLEN_SFT;

		/* clock invert */
		pcm_con |= pcm_priv->lck_invert << PCM_SYNC_OUT_INV_SFT;
		pcm_con |= pcm_priv->bck_invert << PCM_BCLK_OUT_INV_SFT;

		regmap_update_bits(afe->regmap, PCM_INTF_CON1, 0xfffffffe, pcm_con);
		break;
	default:
		dev_err(afe->dev, "%s(), id %d not support\n", __func__, dai->id);
		return -EINVAL;
	}

	return 0;
}

static int mtk_dai_pcm_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_pcm_priv *pcm_priv = afe_priv->dai_priv[dai->id];

	/* DAI mode*/
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		pcm_priv->fmt = AUD_PCM_FMT_I2S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		pcm_priv->fmt = AUD_PCM_FMT_EIAJ;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		pcm_priv->fmt = AUD_PCM_FMT_PCM_MODE_A;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		pcm_priv->fmt = AUD_PCM_FMT_PCM_MODE_B;
		break;
	default:
		pcm_priv->fmt = AUD_PCM_FMT_I2S;
	}

	/* DAI clock inversion*/
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		pcm_priv->bck_invert = AUD_BCLK_OUT_INV_NO_INVERSE;
		pcm_priv->lck_invert = AUD_LRCLK_OUT_INV_NO_INVERSE;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		pcm_priv->bck_invert = AUD_BCLK_OUT_INV_NO_INVERSE;
		pcm_priv->lck_invert = AUD_BCLK_OUT_INV_INVERSE;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		pcm_priv->bck_invert = AUD_BCLK_OUT_INV_INVERSE;
		pcm_priv->lck_invert = AUD_LRCLK_OUT_INV_NO_INVERSE;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		pcm_priv->bck_invert = AUD_BCLK_OUT_INV_INVERSE;
		pcm_priv->lck_invert = AUD_BCLK_OUT_INV_INVERSE;
		break;
	default:
		pcm_priv->bck_invert = AUD_BCLK_OUT_INV_NO_INVERSE;
		pcm_priv->lck_invert = AUD_LRCLK_OUT_INV_NO_INVERSE;
		break;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_pcm_ops = {
	.hw_params = mtk_dai_pcm_hw_params,
	.set_fmt = mtk_dai_pcm_set_fmt,
};

/* dai driver */
#define MTK_PCM_RATES (SNDRV_PCM_RATE_8000 |\
		       SNDRV_PCM_RATE_16000 |\
		       SNDRV_PCM_RATE_32000 |\
		       SNDRV_PCM_RATE_48000)

#define MTK_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_pcm_driver[] = {
	{
		.name = "PCM 1",
		.id = MT8186_DAI_PCM,
		.playback = {
			.stream_name = "PCM 1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "PCM 1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_dai_pcm_ops,
		.symmetric_rate = 1,
		.symmetric_sample_bits = 1,
	},
};

static struct mtk_afe_pcm_priv *init_pcm_priv_data(struct mtk_base_afe *afe)
{
	struct mtk_afe_pcm_priv *pcm_priv;

	pcm_priv = devm_kzalloc(afe->dev, sizeof(struct mtk_afe_pcm_priv),
				GFP_KERNEL);
	if (!pcm_priv)
		return NULL;

	pcm_priv->id = MT8186_DAI_PCM;
	pcm_priv->fmt = AUD_PCM_FMT_I2S;
	pcm_priv->bck_invert = AUD_BCLK_OUT_INV_NO_INVERSE;
	pcm_priv->lck_invert = AUD_LRCLK_OUT_INV_NO_INVERSE;

	return pcm_priv;
}

int mt8186_dai_pcm_register(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_pcm_priv *pcm_priv;
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_pcm_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_pcm_driver);

	dai->dapm_widgets = mtk_dai_pcm_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_pcm_widgets);
	dai->dapm_routes = mtk_dai_pcm_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_pcm_routes);

	pcm_priv = init_pcm_priv_data(afe);
	if (!pcm_priv)
		return -ENOMEM;

	afe_priv->dai_priv[MT8186_DAI_PCM] = pcm_priv;

	return 0;
}
