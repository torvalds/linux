// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI I2S Control
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include "mt8183-afe-common.h"
#include "mt8183-interconnection.h"
#include "mt8183-reg.h"

enum AUD_TX_LCH_RPT {
	AUD_TX_LCH_RPT_NO_REPEAT = 0,
	AUD_TX_LCH_RPT_REPEAT = 1
};

enum AUD_VBT_16K_MODE {
	AUD_VBT_16K_MODE_DISABLE = 0,
	AUD_VBT_16K_MODE_ENABLE = 1
};

enum AUD_EXT_MODEM {
	AUD_EXT_MODEM_SELECT_INTERNAL = 0,
	AUD_EXT_MODEM_SELECT_EXTERNAL = 1
};

enum AUD_PCM_SYNC_TYPE {
	/* bck sync length = 1 */
	AUD_PCM_ONE_BCK_CYCLE_SYNC = 0,
	/* bck sync length = PCM_INTF_CON1[9:13] */
	AUD_PCM_EXTENDED_BCK_CYCLE_SYNC = 1
};

enum AUD_BT_MODE {
	AUD_BT_MODE_DUAL_MIC_ON_TX = 0,
	AUD_BT_MODE_SINGLE_MIC_ON_TX = 1
};

enum AUD_PCM_AFIFO_SRC {
	/* slave mode & external modem uses different crystal */
	AUD_PCM_AFIFO_ASRC = 0,
	/* slave mode & external modem uses the same crystal */
	AUD_PCM_AFIFO_AFIFO = 1
};

enum AUD_PCM_CLOCK_SOURCE {
	AUD_PCM_CLOCK_MASTER_MODE = 0,
	AUD_PCM_CLOCK_SLAVE_MODE = 1
};

enum AUD_PCM_WLEN {
	AUD_PCM_WLEN_PCM_32_BCK_CYCLES = 0,
	AUD_PCM_WLEN_PCM_64_BCK_CYCLES = 1
};

enum AUD_PCM_MODE {
	AUD_PCM_MODE_PCM_MODE_8K = 0,
	AUD_PCM_MODE_PCM_MODE_16K = 1,
	AUD_PCM_MODE_PCM_MODE_32K = 2,
	AUD_PCM_MODE_PCM_MODE_48K = 3,
};

enum AUD_PCM_FMT {
	AUD_PCM_FMT_I2S = 0,
	AUD_PCM_FMT_EIAJ = 1,
	AUD_PCM_FMT_PCM_MODE_A = 2,
	AUD_PCM_FMT_PCM_MODE_B = 3
};

enum AUD_BCLK_OUT_INV {
	AUD_BCLK_OUT_INV_NO_INVERSE = 0,
	AUD_BCLK_OUT_INV_INVERSE = 1
};

enum AUD_PCM_EN {
	AUD_PCM_EN_DISABLE = 0,
	AUD_PCM_EN_ENABLE = 1
};

/* dai component */
static const struct snd_kcontrol_new mtk_pcm_1_playback_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN7,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN7,
				    I_DL2_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_pcm_1_playback_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN8,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN8,
				    I_DL2_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_pcm_1_playback_ch4_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN27,
				    I_DL1_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_pcm_2_playback_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN17,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN17,
				    I_DL2_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_pcm_2_playback_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN18,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN18,
				    I_DL2_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_pcm_2_playback_ch4_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN24,
				    I_DL1_CH1, 1, 0),
};

static const struct snd_soc_dapm_widget mtk_dai_pcm_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("PCM_1_PB_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_1_playback_ch1_mix,
			   ARRAY_SIZE(mtk_pcm_1_playback_ch1_mix)),
	SND_SOC_DAPM_MIXER("PCM_1_PB_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_1_playback_ch2_mix,
			   ARRAY_SIZE(mtk_pcm_1_playback_ch2_mix)),
	SND_SOC_DAPM_MIXER("PCM_1_PB_CH4", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_1_playback_ch4_mix,
			   ARRAY_SIZE(mtk_pcm_1_playback_ch4_mix)),
	SND_SOC_DAPM_MIXER("PCM_2_PB_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_2_playback_ch1_mix,
			   ARRAY_SIZE(mtk_pcm_2_playback_ch1_mix)),
	SND_SOC_DAPM_MIXER("PCM_2_PB_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_2_playback_ch2_mix,
			   ARRAY_SIZE(mtk_pcm_2_playback_ch2_mix)),
	SND_SOC_DAPM_MIXER("PCM_2_PB_CH4", SND_SOC_NOPM, 0, 0,
			   mtk_pcm_2_playback_ch4_mix,
			   ARRAY_SIZE(mtk_pcm_2_playback_ch4_mix)),

	SND_SOC_DAPM_SUPPLY("PCM_1_EN", PCM_INTF_CON1, PCM_EN_SFT, 0,
			    NULL, 0),

	SND_SOC_DAPM_SUPPLY("PCM_2_EN", PCM2_INTF_CON, PCM2_EN_SFT, 0,
			    NULL, 0),

	SND_SOC_DAPM_INPUT("MD1_TO_AFE"),
	SND_SOC_DAPM_INPUT("MD2_TO_AFE"),
	SND_SOC_DAPM_OUTPUT("AFE_TO_MD1"),
	SND_SOC_DAPM_OUTPUT("AFE_TO_MD2"),
};

static const struct snd_soc_dapm_route mtk_dai_pcm_routes[] = {
	{"PCM 1 Playback", NULL, "PCM_1_PB_CH1"},
	{"PCM 1 Playback", NULL, "PCM_1_PB_CH2"},
	{"PCM 1 Playback", NULL, "PCM_1_PB_CH4"},
	{"PCM 2 Playback", NULL, "PCM_2_PB_CH1"},
	{"PCM 2 Playback", NULL, "PCM_2_PB_CH2"},
	{"PCM 2 Playback", NULL, "PCM_2_PB_CH4"},

	{"PCM 1 Playback", NULL, "PCM_1_EN"},
	{"PCM 2 Playback", NULL, "PCM_2_EN"},
	{"PCM 1 Capture", NULL, "PCM_1_EN"},
	{"PCM 2 Capture", NULL, "PCM_2_EN"},

	{"AFE_TO_MD1", NULL, "PCM 2 Playback"},
	{"AFE_TO_MD2", NULL, "PCM 1 Playback"},
	{"PCM 2 Capture", NULL, "MD1_TO_AFE"},
	{"PCM 1 Capture", NULL, "MD2_TO_AFE"},

	{"PCM_1_PB_CH1", "DL2_CH1", "DL2"},
	{"PCM_1_PB_CH2", "DL2_CH2", "DL2"},
	{"PCM_1_PB_CH4", "DL1_CH1", "DL1"},
	{"PCM_2_PB_CH1", "DL2_CH1", "DL2"},
	{"PCM_2_PB_CH2", "DL2_CH2", "DL2"},
	{"PCM_2_PB_CH4", "DL1_CH1", "DL1"},
};

/* dai ops */
static int mtk_dai_pcm_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct snd_soc_dapm_widget *p = snd_soc_dai_get_widget_playback(dai);
	struct snd_soc_dapm_widget *c = snd_soc_dai_get_widget_capture(dai);
	unsigned int rate = params_rate(params);
	unsigned int rate_reg = mt8183_rate_transform(afe->dev, rate, dai->id);
	unsigned int pcm_con = 0;

	dev_dbg(afe->dev, "%s(), id %d, stream %d, rate %d, rate_reg %d, widget active p %d, c %d\n",
		__func__,
		dai->id,
		substream->stream,
		rate,
		rate_reg,
		p->active, c->active);

	if (p->active || c->active)
		return 0;

	switch (dai->id) {
	case MT8183_DAI_PCM_1:
		pcm_con |= AUD_BCLK_OUT_INV_NO_INVERSE << PCM_BCLK_OUT_INV_SFT;
		pcm_con |= AUD_TX_LCH_RPT_NO_REPEAT << PCM_TX_LCH_RPT_SFT;
		pcm_con |= AUD_VBT_16K_MODE_DISABLE << PCM_VBT_16K_MODE_SFT;
		pcm_con |= AUD_EXT_MODEM_SELECT_INTERNAL << PCM_EXT_MODEM_SFT;
		pcm_con |= 0 << PCM_SYNC_LENGTH_SFT;
		pcm_con |= AUD_PCM_ONE_BCK_CYCLE_SYNC << PCM_SYNC_TYPE_SFT;
		pcm_con |= AUD_BT_MODE_DUAL_MIC_ON_TX << PCM_BT_MODE_SFT;
		pcm_con |= AUD_PCM_AFIFO_AFIFO << PCM_BYP_ASRC_SFT;
		pcm_con |= AUD_PCM_CLOCK_SLAVE_MODE << PCM_SLAVE_SFT;
		pcm_con |= rate_reg << PCM_MODE_SFT;
		pcm_con |= AUD_PCM_FMT_PCM_MODE_B << PCM_FMT_SFT;

		regmap_update_bits(afe->regmap, PCM_INTF_CON1,
				   0xfffffffe, pcm_con);
		break;
	case MT8183_DAI_PCM_2:
		pcm_con |= AUD_TX_LCH_RPT_NO_REPEAT << PCM2_TX_LCH_RPT_SFT;
		pcm_con |= AUD_VBT_16K_MODE_DISABLE << PCM2_VBT_16K_MODE_SFT;
		pcm_con |= AUD_BT_MODE_DUAL_MIC_ON_TX << PCM2_BT_MODE_SFT;
		pcm_con |= AUD_PCM_AFIFO_AFIFO << PCM2_AFIFO_SFT;
		pcm_con |= AUD_PCM_WLEN_PCM_32_BCK_CYCLES << PCM2_WLEN_SFT;
		pcm_con |= rate_reg << PCM2_MODE_SFT;
		pcm_con |= AUD_PCM_FMT_PCM_MODE_B << PCM2_FMT_SFT;

		regmap_update_bits(afe->regmap, PCM2_INTF_CON,
				   0xfffffffe, pcm_con);
		break;
	default:
		dev_warn(afe->dev, "%s(), id %d not support\n",
			 __func__, dai->id);
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_pcm_ops = {
	.hw_params = mtk_dai_pcm_hw_params,
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
		.id = MT8183_DAI_PCM_1,
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
	{
		.name = "PCM 2",
		.id = MT8183_DAI_PCM_2,
		.playback = {
			.stream_name = "PCM 2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.capture = {
			.stream_name = "PCM 2 Capture",
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

int mt8183_dai_pcm_register(struct mtk_base_afe *afe)
{
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

	return 0;
}
