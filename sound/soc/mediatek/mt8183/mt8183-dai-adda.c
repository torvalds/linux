// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI ADDA Control
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/regmap.h>
#include <linux/delay.h>
#include "mt8183-afe-common.h"
#include "mt8183-interconnection.h"
#include "mt8183-reg.h"
#include "../common/mtk-dai-adda-common.h"

enum {
	AUDIO_SDM_LEVEL_MUTE = 0,
	AUDIO_SDM_LEVEL_NORMAL = 0x1d,
	/* if you change level normal */
	/* you need to change formula of hp impedance and dc trim too */
};

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
	struct mt8183_afe_private *afe_priv = afe->platform_priv;

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* update setting to dmic */
		if (afe_priv->mtkaif_dmic) {
			/* mtkaif_rxif_data_mode = 1, dmic */
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG0,
					   0x1, 0x1);

			/* dmic mode, 3.25M*/
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG0,
					   0x0, 0xf << 20);
			regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
					   0x0, 0x1 << 5);
			regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
					   0x0, 0x3 << 14);

			/* turn on dmic, ch1, ch2 */
			regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
					   0x1 << 1, 0x1 << 1);
			regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
					   0x3 << 21, 0x3 << 21);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		break;
	default:
		break;
	}

	return 0;
}

/* mtkaif dmic */
static const char * const mt8183_adda_off_on_str[] = {
	"Off", "On"
};

static const struct soc_enum mt8183_adda_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(mt8183_adda_off_on_str),
			    mt8183_adda_off_on_str),
};

static int mt8183_adda_dmic_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8183_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->mtkaif_dmic;

	return 0;
}

static int mt8183_adda_dmic_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;

	if (ucontrol->value.enumerated.item[0] >= e->items)
		return -EINVAL;

	afe_priv->mtkaif_dmic = ucontrol->value.integer.value[0];

	dev_info(afe->dev, "%s(), kcontrol name %s, mtkaif_dmic %d\n",
		 __func__, kcontrol->id.name, afe_priv->mtkaif_dmic);

	return 0;
}

static const struct snd_kcontrol_new mtk_adda_controls[] = {
	SOC_ENUM_EXT("MTKAIF_DMIC", mt8183_adda_enum[0],
		     mt8183_adda_dmic_get, mt8183_adda_dmic_set),
};

enum {
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
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_predis_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_adc_clk"),
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

static int set_mtkaif_rx(struct mtk_base_afe *afe)
{
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	int delay_data;
	int delay_cycle;

	switch (afe_priv->mtkaif_protocol) {
	case MT8183_MTKAIF_PROTOCOL_2_CLK_P2:
		regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x38);
		regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x39);
		/* mtkaif_rxif_clkinv_adc inverse for calibration */
		regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0,
			     0x80010000);

		if (afe_priv->mtkaif_phase_cycle[0] >=
		    afe_priv->mtkaif_phase_cycle[1]) {
			delay_data = DELAY_DATA_MISO1;
			delay_cycle = afe_priv->mtkaif_phase_cycle[0] -
				      afe_priv->mtkaif_phase_cycle[1];
		} else {
			delay_data = DELAY_DATA_MISO2;
			delay_cycle = afe_priv->mtkaif_phase_cycle[1] -
				      afe_priv->mtkaif_phase_cycle[0];
		}

		regmap_update_bits(afe->regmap,
				   AFE_ADDA_MTKAIF_RX_CFG2,
				   MTKAIF_RXIF_DELAY_DATA_MASK_SFT,
				   delay_data << MTKAIF_RXIF_DELAY_DATA_SFT);

		regmap_update_bits(afe->regmap,
				   AFE_ADDA_MTKAIF_RX_CFG2,
				   MTKAIF_RXIF_DELAY_CYCLE_MASK_SFT,
				   delay_cycle << MTKAIF_RXIF_DELAY_CYCLE_SFT);
		break;
	case MT8183_MTKAIF_PROTOCOL_2:
		regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x31);
		regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0,
			     0x00010000);
		break;
	case MT8183_MTKAIF_PROTOCOL_1:
		regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x31);
		regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0, 0x0);
		break;
	default:
		break;
	}

	return 0;
}

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

		/* set sampling rate */
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

		/* SA suggest apply -0.3db to audio/speech path */
		dl_src2_con1 = 0xf74f0000;

		/* turn on down-link gain */
		dl_src2_con0 = dl_src2_con0 | (0x01 << 1);

		regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON0, dl_src2_con0);
		regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON1, dl_src2_con1);

		/* set sdm gain */
		regmap_update_bits(afe->regmap,
				   AFE_ADDA_DL_SDM_DCCOMP_CON,
				   ATTGAIN_CTL_MASK_SFT,
				   AUDIO_SDM_LEVEL_NORMAL << ATTGAIN_CTL_SFT);
	} else {
		unsigned int voice_mode = 0;
		unsigned int ul_src_con0 = 0;	/* default value */

		/* set mtkaif protocol */
		set_mtkaif_rx(afe);

		/* Using Internal ADC */
		regmap_update_bits(afe->regmap,
				   AFE_ADDA_TOP_CON0,
				   0x1 << 0,
				   0x0 << 0);

		voice_mode = mtk_adda_ul_rate_transform(afe, rate);

		ul_src_con0 |= (voice_mode << 17) & (0x7 << 17);

		/* enable iir */
		ul_src_con0 |= (1 << UL_IIR_ON_TMP_CTL_SFT) &
			       UL_IIR_ON_TMP_CTL_MASK_SFT;

		/* 35Hz @ 48k */
		regmap_write(afe->regmap, AFE_ADDA_IIR_COEF_02_01, 0x00000000);
		regmap_write(afe->regmap, AFE_ADDA_IIR_COEF_04_03, 0x00003FB8);
		regmap_write(afe->regmap, AFE_ADDA_IIR_COEF_06_05, 0x3FB80000);
		regmap_write(afe->regmap, AFE_ADDA_IIR_COEF_08_07, 0x3FB80000);
		regmap_write(afe->regmap, AFE_ADDA_IIR_COEF_10_09, 0x0000C048);

		regmap_write(afe->regmap, AFE_ADDA_UL_SRC_CON0, ul_src_con0);

		/* mtkaif_rxif_data_mode = 0, amic */
		regmap_update_bits(afe->regmap,
				   AFE_ADDA_MTKAIF_RX_CFG0,
				   0x1 << 0,
				   0x0 << 0);
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
				SNDRV_PCM_RATE_48000)

#define MTK_ADDA_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_adda_driver[] = {
	{
		.name = "ADDA",
		.id = MT8183_DAI_ADDA,
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

int mt8183_dai_adda_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_adda_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_adda_driver);

	dai->controls = mtk_adda_controls;
	dai->num_controls = ARRAY_SIZE(mtk_adda_controls);
	dai->dapm_widgets = mtk_dai_adda_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_adda_widgets);
	dai->dapm_routes = mtk_dai_adda_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_adda_routes);
	return 0;
}
