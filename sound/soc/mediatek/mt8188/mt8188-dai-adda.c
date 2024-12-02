// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek ALSA SoC Audio DAI ADDA Control
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 *         Chun-Chia Chiu <chun-chia.chiu@mediatek.com>
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/regmap.h>
#include "mt8188-afe-clk.h"
#include "mt8188-afe-common.h"
#include "mt8188-reg.h"
#include "../common/mtk-dai-adda-common.h"

#define ADDA_HIRES_THRES 48000

enum {
	SUPPLY_SEQ_ADDA_DL_ON,
	SUPPLY_SEQ_ADDA_MTKAIF_CFG,
	SUPPLY_SEQ_ADDA_UL_ON,
	SUPPLY_SEQ_ADDA_AFE_ON,
};

struct mtk_dai_adda_priv {
	bool hires_required;
};

static int mt8188_adda_mtkaif_init(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtkaif_param *param = &afe_priv->mtkaif_params;
	int delay_data;
	int delay_cycle;
	unsigned int mask = 0;
	unsigned int val = 0;

	/* set rx protocol 2 & mtkaif_rxif_clkinv_adc inverse */
	regmap_set_bits(afe->regmap, AFE_ADDA_MTKAIF_CFG0,
			MTKAIF_RXIF_CLKINV_ADC | MTKAIF_RXIF_PROTOCOL2);

	regmap_set_bits(afe->regmap, AFE_AUD_PAD_TOP, RG_RX_PROTOCOL2);

	if (!param->mtkaif_calibration_ok) {
		dev_info(afe->dev, "%s(), calibration fail\n",  __func__);
		return 0;
	}

	/* set delay for ch1, ch2 */
	if (param->mtkaif_phase_cycle[MT8188_MTKAIF_MISO_0] >=
	    param->mtkaif_phase_cycle[MT8188_MTKAIF_MISO_1]) {
		delay_data = DELAY_DATA_MISO1;
		delay_cycle =
			param->mtkaif_phase_cycle[MT8188_MTKAIF_MISO_0] -
			param->mtkaif_phase_cycle[MT8188_MTKAIF_MISO_1];
	} else {
		delay_data = DELAY_DATA_MISO0;
		delay_cycle =
			param->mtkaif_phase_cycle[MT8188_MTKAIF_MISO_1] -
			param->mtkaif_phase_cycle[MT8188_MTKAIF_MISO_0];
	}

	mask = (MTKAIF_RXIF_DELAY_DATA | MTKAIF_RXIF_DELAY_CYCLE_MASK);
	val |= FIELD_PREP(MTKAIF_RXIF_DELAY_CYCLE_MASK, delay_cycle);
	val |= FIELD_PREP(MTKAIF_RXIF_DELAY_DATA, delay_data);
	regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG2, mask, val);

	return 0;
}

static int mtk_adda_mtkaif_cfg_event(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol,
				     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8188_adda_mtkaif_init(afe);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_adda_dl_event(struct snd_soc_dapm_widget *w,
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

static void mtk_adda_ul_mictype(struct mtk_base_afe *afe, bool dmic)
{
	unsigned int reg = AFE_ADDA_UL_SRC_CON0;
	unsigned int val;

	val = (UL_SDM3_LEVEL_CTL | UL_MODE_3P25M_CH1_CTL |
	       UL_MODE_3P25M_CH2_CTL);

	/* turn on dmic, ch1, ch2 */
	if (dmic)
		regmap_set_bits(afe->regmap, reg, val);
	else
		regmap_clear_bits(afe->regmap, reg, val);
}

static int mtk_adda_ul_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtkaif_param *param = &afe_priv->mtkaif_params;

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mtk_adda_ul_mictype(afe, param->mtkaif_dmic_on);
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

static struct mtk_dai_adda_priv *get_adda_priv_by_name(struct mtk_base_afe *afe,
						       const char *name)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;

	if (strstr(name, "aud_adc_hires"))
		return afe_priv->dai_priv[MT8188_AFE_IO_UL_SRC];
	else if (strstr(name, "aud_dac_hires"))
		return afe_priv->dai_priv[MT8188_AFE_IO_DL_SRC];
	else
		return NULL;
}

static int mtk_afe_adda_hires_connect(struct snd_soc_dapm_widget *source,
				      struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = source;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_dai_adda_priv *adda_priv;

	adda_priv = get_adda_priv_by_name(afe, w->name);

	if (!adda_priv) {
		dev_dbg(afe->dev, "adda_priv == NULL");
		return 0;
	}

	return (adda_priv->hires_required) ? 1 : 0;
}

static const struct snd_kcontrol_new mtk_dai_adda_o176_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I000 Switch", AFE_CONN176, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I002 Switch", AFE_CONN176, 2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I020 Switch", AFE_CONN176, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I022 Switch", AFE_CONN176, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I070 Switch", AFE_CONN176_2, 6, 1, 0),
};

static const struct snd_kcontrol_new mtk_dai_adda_o177_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I001 Switch", AFE_CONN177, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I003 Switch", AFE_CONN177, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I021 Switch", AFE_CONN177, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I023 Switch", AFE_CONN177, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I071 Switch", AFE_CONN177_2, 7, 1, 0),
};

static const char * const adda_dlgain_mux_map[] = {
	"Bypass", "Connect",
};

static SOC_ENUM_SINGLE_DECL(adda_dlgain_mux_map_enum,
			    SND_SOC_NOPM, 0,
			    adda_dlgain_mux_map);

static const struct snd_kcontrol_new adda_dlgain_mux_control =
	SOC_DAPM_ENUM("DL_GAIN_MUX", adda_dlgain_mux_map_enum);

static const struct snd_soc_dapm_widget mtk_dai_adda_widgets[] = {
	SND_SOC_DAPM_MIXER("I168", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I169", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("O176", SND_SOC_NOPM, 0, 0,
			   mtk_dai_adda_o176_mix,
			   ARRAY_SIZE(mtk_dai_adda_o176_mix)),
	SND_SOC_DAPM_MIXER("O177", SND_SOC_NOPM, 0, 0,
			   mtk_dai_adda_o177_mix,
			   ARRAY_SIZE(mtk_dai_adda_o177_mix)),

	SND_SOC_DAPM_SUPPLY_S("ADDA Enable", SUPPLY_SEQ_ADDA_AFE_ON,
			      AFE_ADDA_UL_DL_CON0,
			      ADDA_AFE_ON_SHIFT, 0,
			      NULL,
			      0),

	SND_SOC_DAPM_SUPPLY_S("ADDA Playback Enable", SUPPLY_SEQ_ADDA_DL_ON,
			      AFE_ADDA_DL_SRC2_CON0,
			      DL_2_SRC_ON_TMP_CTRL_PRE_SHIFT, 0,
			      mtk_adda_dl_event,
			      SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("ADDA Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA_UL_SRC_CON0,
			      UL_SRC_ON_TMP_CTL_SHIFT, 0,
			      mtk_adda_ul_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("ADDA_MTKAIF_CFG", SUPPLY_SEQ_ADDA_MTKAIF_CFG,
			      SND_SOC_NOPM,
			      0, 0,
			      mtk_adda_mtkaif_cfg_event,
			      SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_MUX("DL_GAIN_MUX", SND_SOC_NOPM, 0, 0,
			 &adda_dlgain_mux_control),

	SND_SOC_DAPM_PGA("DL_GAIN", AFE_ADDA_DL_SRC2_CON0,
			 DL_2_GAIN_ON_CTL_PRE_SHIFT, 0, NULL, 0),

	SND_SOC_DAPM_INPUT("ADDA_INPUT"),
	SND_SOC_DAPM_OUTPUT("ADDA_OUTPUT"),

	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_adc"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_hires"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_adc_hires"),
};

static const struct snd_soc_dapm_route mtk_dai_adda_routes[] = {
	{"ADDA Capture", NULL, "ADDA Enable"},
	{"ADDA Capture", NULL, "ADDA Capture Enable"},
	{"ADDA Capture", NULL, "ADDA_MTKAIF_CFG"},
	{"ADDA Capture", NULL, "aud_adc"},
	{"ADDA Capture", NULL, "aud_adc_hires", mtk_afe_adda_hires_connect},

	{"I168", NULL, "ADDA Capture"},
	{"I169", NULL, "ADDA Capture"},

	{"ADDA Playback", NULL, "ADDA Enable"},
	{"ADDA Playback", NULL, "ADDA Playback Enable"},
	{"ADDA Playback", NULL, "aud_dac"},
	{"ADDA Playback", NULL, "aud_dac_hires", mtk_afe_adda_hires_connect},

	{"DL_GAIN", NULL, "O176"},
	{"DL_GAIN", NULL, "O177"},

	{"DL_GAIN_MUX", "Bypass", "O176"},
	{"DL_GAIN_MUX", "Bypass", "O177"},
	{"DL_GAIN_MUX", "Connect", "DL_GAIN"},

	{"ADDA Playback", NULL, "DL_GAIN_MUX"},

	{"O176", "I000 Switch", "I000"},
	{"O177", "I001 Switch", "I001"},

	{"O176", "I002 Switch", "I002"},
	{"O177", "I003 Switch", "I003"},

	{"O176", "I020 Switch", "I020"},
	{"O177", "I021 Switch", "I021"},

	{"O176", "I022 Switch", "I022"},
	{"O177", "I023 Switch", "I023"},

	{"O176", "I070 Switch", "I070"},
	{"O177", "I071 Switch", "I071"},

	{"ADDA Capture", NULL, "ADDA_INPUT"},
	{"ADDA_OUTPUT", NULL, "ADDA Playback"},
};

static int mt8188_adda_dmic_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtkaif_param *param = &afe_priv->mtkaif_params;

	ucontrol->value.integer.value[0] = param->mtkaif_dmic_on;
	return 0;
}

static int mt8188_adda_dmic_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtkaif_param *param = &afe_priv->mtkaif_params;
	int dmic_on;

	dmic_on = !!ucontrol->value.integer.value[0];

	dev_dbg(afe->dev, "%s(), kcontrol name %s, dmic_on %d\n",
		__func__, kcontrol->id.name, dmic_on);

	if (param->mtkaif_dmic_on == dmic_on)
		return 0;

	param->mtkaif_dmic_on = dmic_on;
	return 1;
}

static const struct snd_kcontrol_new mtk_dai_adda_controls[] = {
	SOC_SINGLE("ADDA_DL_GAIN", AFE_ADDA_DL_SRC2_CON1,
		   DL_2_GAIN_CTL_PRE_SHIFT, 65535, 0),
	SOC_SINGLE_BOOL_EXT("MTKAIF_DMIC Switch", 0,
			    mt8188_adda_dmic_get, mt8188_adda_dmic_set),
};

static int mtk_dai_da_configure(struct mtk_base_afe *afe,
				unsigned int rate, int id)
{
	unsigned int val = 0;
	unsigned int mask = 0;

	/* set sampling rate */
	mask |= DL_2_INPUT_MODE_CTL_MASK;
	val |= FIELD_PREP(DL_2_INPUT_MODE_CTL_MASK,
			  mtk_adda_dl_rate_transform(afe, rate));

	/* turn off saturation */
	mask |= DL_2_CH1_SATURATION_EN_CTL;
	mask |= DL_2_CH2_SATURATION_EN_CTL;

	/* turn off mute function */
	mask |= DL_2_MUTE_CH1_OFF_CTL_PRE;
	mask |= DL_2_MUTE_CH2_OFF_CTL_PRE;
	val |= DL_2_MUTE_CH1_OFF_CTL_PRE;
	val |= DL_2_MUTE_CH2_OFF_CTL_PRE;

	/* set voice input data if input sample rate is 8k or 16k */
	mask |= DL_2_VOICE_MODE_CTL_PRE;
	if (rate == 8000 || rate == 16000)
		val |= DL_2_VOICE_MODE_CTL_PRE;

	regmap_update_bits(afe->regmap, AFE_ADDA_DL_SRC2_CON0, mask, val);

	/* new 2nd sdm */
	regmap_set_bits(afe->regmap, AFE_ADDA_DL_SDM_DCCOMP_CON,
			DL_USE_NEW_2ND_SDM);

	return 0;
}

static int mtk_dai_ad_configure(struct mtk_base_afe *afe,
				unsigned int rate, int id)
{
	unsigned int val;
	unsigned int mask;

	mask = UL_VOICE_MODE_CTL_MASK;
	val = FIELD_PREP(UL_VOICE_MODE_CTL_MASK,
			 mtk_adda_ul_rate_transform(afe, rate));

	regmap_update_bits(afe->regmap, AFE_ADDA_UL_SRC_CON0,
			   mask, val);
	return 0;
}

static int mtk_dai_adda_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_adda_priv *adda_priv = afe_priv->dai_priv[dai->id];
	unsigned int rate = params_rate(params);
	int id = dai->id;
	int ret = 0;

	dev_dbg(afe->dev, "%s(), id %d, stream %d, rate %u\n",
		__func__, id, substream->stream, rate);

	adda_priv->hires_required = (rate > ADDA_HIRES_THRES);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		ret = mtk_dai_da_configure(afe, rate, id);
	else
		ret = mtk_dai_ad_configure(afe, rate, id);

	return ret;
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
		.name = "DL_SRC",
		.id = MT8188_AFE_IO_DL_SRC,
		.playback = {
			.stream_name = "ADDA Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_PLAYBACK_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
	{
		.name = "UL_SRC",
		.id = MT8188_AFE_IO_UL_SRC,
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

static int init_adda_priv_data(struct mtk_base_afe *afe)
{
	struct mt8188_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_adda_priv *adda_priv;
	int adda_dai_list[] = {MT8188_AFE_IO_DL_SRC, MT8188_AFE_IO_UL_SRC};
	int i;

	for (i = 0; i < ARRAY_SIZE(adda_dai_list); i++) {
		adda_priv = devm_kzalloc(afe->dev,
					 sizeof(struct mtk_dai_adda_priv),
					 GFP_KERNEL);
		if (!adda_priv)
			return -ENOMEM;

		afe_priv->dai_priv[adda_dai_list[i]] = adda_priv;
	}

	return 0;
}

int mt8188_dai_adda_register(struct mtk_base_afe *afe)
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
	dai->controls = mtk_dai_adda_controls;
	dai->num_controls = ARRAY_SIZE(mtk_dai_adda_controls);

	return init_adda_priv_data(afe);
}
