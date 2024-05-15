// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI ADDA Control
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/regmap.h>
#include <linux/delay.h>
#include "mt6797-afe-common.h"
#include "mt6797-interconnection.h"
#include "mt6797-reg.h"
#include "../common/mtk-dai-adda-common.h"

/* dai component */
static const struct snd_kcontrol_new mtk_adda_dl_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN3, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN3, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN3, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN3,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN3,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN3,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN3,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN4, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN4, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN4, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN4, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN4, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN4, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN4,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN4,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN4,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN4,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2", AFE_CONN4,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2", AFE_CONN4,
				    I_PCM_2_CAP_CH2, 1, 0),
};

static int mtk_adda_ul_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		break;
	default:
		break;
	}

	return 0;
}

enum {
	SUPPLY_SEQ_AUD_TOP_PDN,
	SUPPLY_SEQ_ADDA_AFE_ON,
	SUPPLY_SEQ_ADDA_DL_ON,
	SUPPLY_SEQ_ADDA_UL_ON,
};

static const struct snd_soc_dapm_widget mtk_dai_adda_widgets[] = {
	/* adda */
	SND_SOC_DAPM_MIXER("ADDA_DL_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch1_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch1_mix)),
	SND_SOC_DAPM_MIXER("ADDA_DL_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch2_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch2_mix)),

	SND_SOC_DAPM_SUPPLY_S("ADDA Enable", SUPPLY_SEQ_ADDA_AFE_ON,
			      AFE_ADDA_UL_DL_CON0, ADDA_AFE_ON_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADDA Playback Enable", SUPPLY_SEQ_ADDA_DL_ON,
			      AFE_ADDA_DL_SRC2_CON0,
			      DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADDA Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA_UL_SRC_CON0,
			      UL_SRC_ON_TMP_CTL_SFT, 0,
			      mtk_adda_ul_event,
			      SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("aud_dac_clk", SUPPLY_SEQ_AUD_TOP_PDN,
			      AUDIO_TOP_CON0, PDN_DAC_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("aud_dac_predis_clk", SUPPLY_SEQ_AUD_TOP_PDN,
			      AUDIO_TOP_CON0, PDN_DAC_PREDIS_SFT, 1,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("aud_adc_clk", SUPPLY_SEQ_AUD_TOP_PDN,
			      AUDIO_TOP_CON0, PDN_ADC_SFT, 1,
			      NULL, 0),

	SND_SOC_DAPM_CLOCK_SUPPLY("mtkaif_26m_clk"),
};

static const struct snd_soc_dapm_route mtk_dai_adda_routes[] = {
	/* playback */
	{"ADDA_DL_CH1", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH2", "DL1"},

	{"ADDA_DL_CH1", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH2", "DL2"},

	{"ADDA_DL_CH1", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH2", "DL3"},

	{"ADDA Playback", NULL, "ADDA_DL_CH1"},
	{"ADDA Playback", NULL, "ADDA_DL_CH2"},

	/* adda enable */
	{"ADDA Playback", NULL, "ADDA Enable"},
	{"ADDA Playback", NULL, "ADDA Playback Enable"},
	{"ADDA Capture", NULL, "ADDA Enable"},
	{"ADDA Capture", NULL, "ADDA Capture Enable"},

	/* clk */
	{"ADDA Playback", NULL, "mtkaif_26m_clk"},
	{"ADDA Playback", NULL, "aud_dac_clk"},
	{"ADDA Playback", NULL, "aud_dac_predis_clk"},

	{"ADDA Capture", NULL, "mtkaif_26m_clk"},
	{"ADDA Capture", NULL, "aud_adc_clk"},
};

/* dai ops */
static int mtk_dai_adda_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);

	dev_dbg(afe->dev, "%s(), id %d, stream %d, rate %d\n",
		__func__, dai->id, substream->stream, rate);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		unsigned int dl_src2_con0 = 0;
		unsigned int dl_src2_con1 = 0;

		/* clean predistortion */
		regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON0, 0);
		regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON1, 0);

		/* set input sampling rate */
		dl_src2_con0 = mtk_adda_dl_rate_transform(afe, rate) << 28;

		/* set output mode */
		switch (rate) {
		case 192000:
			dl_src2_con0 |= (0x1 << 24); /* UP_SAMPLING_RATE_X2 */
			dl_src2_con0 |= 1 << 14;
			break;
		case 96000:
			dl_src2_con0 |= (0x2 << 24); /* UP_SAMPLING_RATE_X4 */
			dl_src2_con0 |= 1 << 14;
			break;
		default:
			dl_src2_con0 |= (0x3 << 24); /* UP_SAMPLING_RATE_X8 */
			break;
		}

		/* turn off mute function */
		dl_src2_con0 |= (0x03 << 11);

		/* set voice input data if input sample rate is 8k or 16k */
		if (rate == 8000 || rate == 16000)
			dl_src2_con0 |= 0x01 << 5;

		if (rate < 96000) {
			/* SA suggest apply -0.3db to audio/speech path */
			dl_src2_con1 = 0xf74f0000;
		} else {
			/* SA suggest apply -0.3db to audio/speech path
			 * with DL gain set to half,
			 * 0xFFFF = 0dB -> 0x8000 = 0dB when 96k, 192k
			 */
			dl_src2_con1 = 0x7ba70000;
		}

		/* turn on down-link gain */
		dl_src2_con0 = dl_src2_con0 | (0x01 << 1);

		regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON0, dl_src2_con0);
		regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON1, dl_src2_con1);
	} else {
		unsigned int voice_mode = 0;
		unsigned int ul_src_con0 = 0;	/* default value */

		/* Using Internal ADC */
		regmap_update_bits(afe->regmap,
				   AFE_ADDA_TOP_CON0,
				   0x1 << 0,
				   0x0 << 0);

		voice_mode = mtk_adda_ul_rate_transform(afe, rate);

		ul_src_con0 |= (voice_mode << 17) & (0x7 << 17);

		/* up8x txif sat on */
		regmap_write(afe->regmap, AFE_ADDA_NEWIF_CFG0, 0x03F87201);

		if (rate >= 96000) {	/* hires */
			/* use hires format [1 0 23] */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_NEWIF_CFG0,
					   0x1 << 5,
					   0x1 << 5);

			regmap_update_bits(afe->regmap,
					   AFE_ADDA_NEWIF_CFG2,
					   0xf << 28,
					   voice_mode << 28);
		} else {	/* normal 8~48k */
			/* use fixed 260k anc path */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_NEWIF_CFG2,
					   0xf << 28,
					   8 << 28);

			/* ul_use_cic_out */
			ul_src_con0 |= 0x1 << 20;
		}

		regmap_update_bits(afe->regmap,
				   AFE_ADDA_NEWIF_CFG2,
				   0xf << 28,
				   8 << 28);

		regmap_update_bits(afe->regmap,
				   AFE_ADDA_UL_SRC_CON0,
				   0xfffffffe,
				   ul_src_con0);
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_adda_ops = {
	.hw_params = mtk_dai_adda_hw_params,
};

/* dai driver */
#define MTK_ADDA_PLAYBACK_RATES (SNDRV_PCM_RATE_8000_48000 |\
				 SNDRV_PCM_RATE_96000 |\
				 SNDRV_PCM_RATE_192000)

#define MTK_ADDA_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
				SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 |\
				SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_96000 |\
				SNDRV_PCM_RATE_192000)

#define MTK_ADDA_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_adda_driver[] = {
	{
		.name = "ADDA",
		.id = MT6797_DAI_ADDA,
		.playback = {
			.stream_name = "ADDA Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_PLAYBACK_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.capture = {
			.stream_name = "ADDA Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
};

int mt6797_dai_adda_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_adda_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_adda_driver);

	dai->dapm_widgets = mtk_dai_adda_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_adda_widgets);
	dai->dapm_routes = mtk_dai_adda_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_adda_routes);
	return 0;
}
