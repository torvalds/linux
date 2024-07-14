// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI ADDA Control
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/regmap.h>
#include <linux/delay.h>
#include "mt8186-afe-clk.h"
#include "mt8186-afe-common.h"
#include "mt8186-afe-gpio.h"
#include "mt8186-interconnection.h"

enum {
	UL_IIR_SW = 0,
	UL_IIR_5HZ,
	UL_IIR_10HZ,
	UL_IIR_25HZ,
	UL_IIR_50HZ,
	UL_IIR_75HZ,
};

enum {
	AUDIO_SDM_LEVEL_MUTE = 0,
	AUDIO_SDM_LEVEL_NORMAL = 0x1d,
	/* if you change level normal */
	/* you need to change formula of hp impedance and dc trim too */
};

enum {
	AUDIO_SDM_2ND = 0,
	AUDIO_SDM_3RD,
};

enum {
	DELAY_DATA_MISO1 = 0,
	DELAY_DATA_MISO2,
};

enum {
	MTK_AFE_ADDA_DL_RATE_8K = 0,
	MTK_AFE_ADDA_DL_RATE_11K = 1,
	MTK_AFE_ADDA_DL_RATE_12K = 2,
	MTK_AFE_ADDA_DL_RATE_16K = 3,
	MTK_AFE_ADDA_DL_RATE_22K = 4,
	MTK_AFE_ADDA_DL_RATE_24K = 5,
	MTK_AFE_ADDA_DL_RATE_32K = 6,
	MTK_AFE_ADDA_DL_RATE_44K = 7,
	MTK_AFE_ADDA_DL_RATE_48K = 8,
	MTK_AFE_ADDA_DL_RATE_96K = 9,
	MTK_AFE_ADDA_DL_RATE_192K = 10,
};

enum {
	MTK_AFE_ADDA_UL_RATE_8K = 0,
	MTK_AFE_ADDA_UL_RATE_16K = 1,
	MTK_AFE_ADDA_UL_RATE_32K = 2,
	MTK_AFE_ADDA_UL_RATE_48K = 3,
	MTK_AFE_ADDA_UL_RATE_96K = 4,
	MTK_AFE_ADDA_UL_RATE_192K = 5,
	MTK_AFE_ADDA_UL_RATE_48K_HD = 6,
};

#define SDM_AUTO_RESET_THRESHOLD 0x190000

struct mtk_afe_adda_priv {
	int dl_rate;
	int ul_rate;
};

static struct mtk_afe_adda_priv *get_adda_priv_by_name(struct mtk_base_afe *afe,
						       const char *name)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dai_id;

	if (strncmp(name, "aud_dac", 7) == 0 || strncmp(name, "aud_adc", 7) == 0)
		dai_id = MT8186_DAI_ADDA;
	else
		return NULL;

	return afe_priv->dai_priv[dai_id];
}

static unsigned int adda_dl_rate_transform(struct mtk_base_afe *afe,
					   unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_ADDA_DL_RATE_8K;
	case 11025:
		return MTK_AFE_ADDA_DL_RATE_11K;
	case 12000:
		return MTK_AFE_ADDA_DL_RATE_12K;
	case 16000:
		return MTK_AFE_ADDA_DL_RATE_16K;
	case 22050:
		return MTK_AFE_ADDA_DL_RATE_22K;
	case 24000:
		return MTK_AFE_ADDA_DL_RATE_24K;
	case 32000:
		return MTK_AFE_ADDA_DL_RATE_32K;
	case 44100:
		return MTK_AFE_ADDA_DL_RATE_44K;
	case 48000:
		return MTK_AFE_ADDA_DL_RATE_48K;
	case 96000:
		return MTK_AFE_ADDA_DL_RATE_96K;
	case 192000:
		return MTK_AFE_ADDA_DL_RATE_192K;
	default:
		dev_info(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
	}

	return MTK_AFE_ADDA_DL_RATE_48K;
}

static unsigned int adda_ul_rate_transform(struct mtk_base_afe *afe,
					   unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_ADDA_UL_RATE_8K;
	case 16000:
		return MTK_AFE_ADDA_UL_RATE_16K;
	case 32000:
		return MTK_AFE_ADDA_UL_RATE_32K;
	case 48000:
		return MTK_AFE_ADDA_UL_RATE_48K;
	case 96000:
		return MTK_AFE_ADDA_UL_RATE_96K;
	case 192000:
		return MTK_AFE_ADDA_UL_RATE_192K;
	default:
		dev_info(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
	}

	return MTK_AFE_ADDA_UL_RATE_48K;
}

/* dai component */
static const struct snd_kcontrol_new mtk_adda_dl_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1 Switch", AFE_CONN3, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH1 Switch", AFE_CONN3, I_DL12_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1 Switch", AFE_CONN3, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1 Switch", AFE_CONN3, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1 Switch", AFE_CONN3_1, I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1 Switch", AFE_CONN3_1, I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1 Switch", AFE_CONN3_1, I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH1 Switch", AFE_CONN3_1, I_DL8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2 Switch", AFE_CONN3,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1 Switch", AFE_CONN3,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH1 Switch", AFE_CONN3,
				    I_GAIN1_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1 Switch", AFE_CONN3,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1 Switch", AFE_CONN3,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH1 Switch", AFE_CONN3_1,
				    I_SRC_1_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_2_OUT_CH1 Switch", AFE_CONN3_1,
				    I_SRC_2_OUT_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1 Switch", AFE_CONN4, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2 Switch", AFE_CONN4, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH2 Switch", AFE_CONN4, I_DL12_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1 Switch", AFE_CONN4, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2 Switch", AFE_CONN4, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1 Switch", AFE_CONN4, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2 Switch", AFE_CONN4, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2 Switch", AFE_CONN4_1, I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2 Switch", AFE_CONN4_1, I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2 Switch", AFE_CONN4_1, I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH2 Switch", AFE_CONN4_1, I_DL8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2 Switch", AFE_CONN4,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1 Switch", AFE_CONN4,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH2 Switch", AFE_CONN4,
				    I_GAIN1_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2 Switch", AFE_CONN4,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2 Switch", AFE_CONN4,
				    I_PCM_2_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH2 Switch", AFE_CONN4_1,
				    I_SRC_1_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_2_OUT_CH2 Switch", AFE_CONN4_1,
				    I_SRC_2_OUT_CH2, 1, 0),
};

enum {
	SUPPLY_SEQ_ADDA_AFE_ON,
	SUPPLY_SEQ_ADDA_DL_ON,
	SUPPLY_SEQ_ADDA_AUD_PAD_TOP,
	SUPPLY_SEQ_ADDA_MTKAIF_CFG,
	SUPPLY_SEQ_ADDA_FIFO,
	SUPPLY_SEQ_ADDA_AP_DMIC,
	SUPPLY_SEQ_ADDA_UL_ON,
};

static int mtk_adda_ul_src_dmic(struct mtk_base_afe *afe, int id)
{
	unsigned int reg;

	switch (id) {
	case MT8186_DAI_ADDA:
	case MT8186_DAI_AP_DMIC:
		reg = AFE_ADDA_UL_SRC_CON0;
		break;
	default:
		return -EINVAL;
	}

	/* dmic mode, 3.25M*/
	regmap_update_bits(afe->regmap, reg,
			   DIGMIC_3P25M_1P625M_SEL_MASK_SFT, 0);
	regmap_update_bits(afe->regmap, reg,
			   DMIC_LOW_POWER_CTL_MASK_SFT, 0);

	/* turn on dmic, ch1, ch2 */
	regmap_update_bits(afe->regmap, reg,
			   UL_SDM_3_LEVEL_MASK_SFT,
			   BIT(UL_SDM_3_LEVEL_SFT));
	regmap_update_bits(afe->regmap, reg,
			   UL_MODE_3P25M_CH1_CTL_MASK_SFT,
			   BIT(UL_MODE_3P25M_CH1_CTL_SFT));
	regmap_update_bits(afe->regmap, reg,
			   UL_MODE_3P25M_CH2_CTL_MASK_SFT,
			   BIT(UL_MODE_3P25M_CH2_CTL_SFT));

	return 0;
}

static int mtk_adda_ul_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int mtkaif_dmic = afe_priv->mtkaif_dmic;

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x, mtkaif_dmic %d\n",
		__func__, w->name, event, mtkaif_dmic);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8186_afe_gpio_request(afe->dev, true, MT8186_DAI_ADDA, 1);

		/* update setting to dmic */
		if (mtkaif_dmic) {
			/* mtkaif_rxif_data_mode = 1, dmic */
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG0,
					   0x1, 0x1);

			/* dmic mode, 3.25M*/
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG0,
					   MTKAIF_RXIF_VOICE_MODE_MASK_SFT,
					   0x0);
			mtk_adda_ul_src_dmic(afe, MT8186_DAI_ADDA);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		mt8186_afe_gpio_request(afe->dev, false, MT8186_DAI_ADDA, 1);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_adda_pad_top_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol,
				  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (afe_priv->mtkaif_protocol == MTKAIF_PROTOCOL_2_CLK_P2)
			regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x39);
		else
			regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x31);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_adda_mtkaif_cfg_event(struct snd_soc_dapm_widget *w,
				     struct snd_kcontrol *kcontrol,
				     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int delay_data;
	int delay_cycle;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (afe_priv->mtkaif_protocol == MTKAIF_PROTOCOL_2_CLK_P2) {
			/* set protocol 2 */
			regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0, 0x10000);
			/* mtkaif_rxif_clkinv_adc inverse */
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_CFG0,
					   MTKAIF_RXIF_CLKINV_ADC_MASK_SFT,
					   BIT(MTKAIF_RXIF_CLKINV_ADC_SFT));

			if (strcmp(w->name, "ADDA_MTKAIF_CFG") == 0) {
				if (afe_priv->mtkaif_chosen_phase[0] < 0 &&
				    afe_priv->mtkaif_chosen_phase[1] < 0) {
					dev_err(afe->dev,
						"%s(), calib fail mtkaif_chosen_phase[0/1]:%d/%d\n",
						__func__,
						afe_priv->mtkaif_chosen_phase[0],
						afe_priv->mtkaif_chosen_phase[1]);
					break;
				}

				if (afe_priv->mtkaif_chosen_phase[0] < 0 ||
				    afe_priv->mtkaif_chosen_phase[1] < 0) {
					dev_err(afe->dev,
						"%s(), skip delay setting mtkaif_chosen_phase[0/1]:%d/%d\n",
						__func__,
						afe_priv->mtkaif_chosen_phase[0],
						afe_priv->mtkaif_chosen_phase[1]);
					break;
				}
			}

			/* set delay for ch12 */
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
					   delay_data <<
					   MTKAIF_RXIF_DELAY_DATA_SFT);

			regmap_update_bits(afe->regmap,
					   AFE_ADDA_MTKAIF_RX_CFG2,
					   MTKAIF_RXIF_DELAY_CYCLE_MASK_SFT,
					   delay_cycle <<
					   MTKAIF_RXIF_DELAY_CYCLE_SFT);

		} else if (afe_priv->mtkaif_protocol == MTKAIF_PROTOCOL_2) {
			regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0, 0x10000);
		} else {
			regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0, 0);
		}

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
	case SND_SOC_DAPM_PRE_PMU:
		mt8186_afe_gpio_request(afe->dev, true, MT8186_DAI_ADDA, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		mt8186_afe_gpio_request(afe->dev, false, MT8186_DAI_ADDA, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int mt8186_adda_dmic_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->mtkaif_dmic;

	return 0;
}

static int mt8186_adda_dmic_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int dmic_on;

	dmic_on = ucontrol->value.integer.value[0];

	dev_dbg(afe->dev, "%s(), kcontrol name %s, dmic_on %d\n",
		__func__, kcontrol->id.name, dmic_on);

	if (afe_priv->mtkaif_dmic == dmic_on)
		return 0;

	afe_priv->mtkaif_dmic = dmic_on;

	return 1;
}

static const struct snd_kcontrol_new mtk_adda_controls[] = {
	SOC_SINGLE("ADDA_DL_GAIN", AFE_ADDA_DL_SRC2_CON1,
		   DL_2_GAIN_CTL_PRE_SFT, DL_2_GAIN_CTL_PRE_MASK, 0),
	SOC_SINGLE_BOOL_EXT("MTKAIF_DMIC Switch", 0,
			    mt8186_adda_dmic_get, mt8186_adda_dmic_set),
};

/* ADDA UL MUX */
enum {
	ADDA_UL_MUX_MTKAIF = 0,
	ADDA_UL_MUX_AP_DMIC,
	ADDA_UL_MUX_MASK = 0x1,
};

static const char * const adda_ul_mux_map[] = {
	"MTKAIF", "AP_DMIC"
};

static int adda_ul_map_value[] = {
	ADDA_UL_MUX_MTKAIF,
	ADDA_UL_MUX_AP_DMIC,
};

static SOC_VALUE_ENUM_SINGLE_DECL(adda_ul_mux_map_enum,
				  SND_SOC_NOPM,
				  0,
				  ADDA_UL_MUX_MASK,
				  adda_ul_mux_map,
				  adda_ul_map_value);

static const struct snd_kcontrol_new adda_ul_mux_control =
	SOC_DAPM_ENUM("ADDA_UL_MUX Select", adda_ul_mux_map_enum);

static const struct snd_soc_dapm_widget mtk_dai_adda_widgets[] = {
	/* inter-connections */
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
			      DL_2_SRC_ON_CTL_PRE_SFT, 0,
			      mtk_adda_dl_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("ADDA Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA_UL_SRC_CON0,
			      UL_SRC_ON_CTL_SFT, 0,
			      mtk_adda_ul_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("AUD_PAD_TOP", SUPPLY_SEQ_ADDA_AUD_PAD_TOP,
			      AFE_AUD_PAD_TOP, RG_RX_FIFO_ON_SFT, 0,
			      mtk_adda_pad_top_event,
			      SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY_S("ADDA_MTKAIF_CFG", SUPPLY_SEQ_ADDA_MTKAIF_CFG,
			      SND_SOC_NOPM, 0, 0,
			      mtk_adda_mtkaif_cfg_event,
			      SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_SUPPLY_S("AP_DMIC_EN", SUPPLY_SEQ_ADDA_AP_DMIC,
			      AFE_ADDA_UL_SRC_CON0,
			      UL_AP_DMIC_ON_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADDA_FIFO", SUPPLY_SEQ_ADDA_FIFO,
			      AFE_ADDA_UL_DL_CON0,
			      AFE_ADDA_FIFO_AUTO_RST_SFT, 1,
			      NULL, 0),

	SND_SOC_DAPM_MUX("ADDA_UL_Mux", SND_SOC_NOPM, 0, 0,
			 &adda_ul_mux_control),

	SND_SOC_DAPM_INPUT("AP_DMIC_INPUT"),

	/* clock */
	SND_SOC_DAPM_CLOCK_SUPPLY("top_mux_audio_h"),

	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_hires_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_predis_clk"),

	SND_SOC_DAPM_CLOCK_SUPPLY("aud_adc_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_adc_hires_clk"),
};

#define HIRES_THRESHOLD 48000
static int mtk_afe_dac_hires_connect(struct snd_soc_dapm_widget *source,
				     struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = source;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_adda_priv *adda_priv;

	adda_priv = get_adda_priv_by_name(afe, w->name);

	if (!adda_priv) {
		dev_err(afe->dev, "%s(), adda_priv == NULL", __func__);
		return 0;
	}

	return (adda_priv->dl_rate > HIRES_THRESHOLD) ? 1 : 0;
}

static int mtk_afe_adc_hires_connect(struct snd_soc_dapm_widget *source,
				     struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = source;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mtk_afe_adda_priv *adda_priv;

	adda_priv = get_adda_priv_by_name(afe, w->name);

	if (!adda_priv) {
		dev_err(afe->dev, "%s(), adda_priv == NULL", __func__);
		return 0;
	}

	return (adda_priv->ul_rate > HIRES_THRESHOLD) ? 1 : 0;
}

static const struct snd_soc_dapm_route mtk_dai_adda_routes[] = {
	/* playback */
	{"ADDA_DL_CH1", "DL1_CH1 Switch", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH1 Switch", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH2 Switch", "DL1"},

	{"ADDA_DL_CH1", "DL12_CH1 Switch", "DL12"},
	{"ADDA_DL_CH2", "DL12_CH2 Switch", "DL12"},

	{"ADDA_DL_CH1", "DL6_CH1 Switch", "DL6"},
	{"ADDA_DL_CH2", "DL6_CH2 Switch", "DL6"},

	{"ADDA_DL_CH1", "DL8_CH1 Switch", "DL8"},
	{"ADDA_DL_CH2", "DL8_CH2 Switch", "DL8"},

	{"ADDA_DL_CH1", "DL2_CH1 Switch", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH1 Switch", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH2 Switch", "DL2"},

	{"ADDA_DL_CH1", "DL3_CH1 Switch", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH1 Switch", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH2 Switch", "DL3"},

	{"ADDA_DL_CH1", "DL4_CH1 Switch", "DL4"},
	{"ADDA_DL_CH2", "DL4_CH2 Switch", "DL4"},

	{"ADDA_DL_CH1", "DL5_CH1 Switch", "DL5"},
	{"ADDA_DL_CH2", "DL5_CH2 Switch", "DL5"},

	{"ADDA Playback", NULL, "ADDA_DL_CH1"},
	{"ADDA Playback", NULL, "ADDA_DL_CH2"},

	{"ADDA Playback", NULL, "ADDA Enable"},
	{"ADDA Playback", NULL, "ADDA Playback Enable"},

	/* capture */
	{"ADDA_UL_Mux", "MTKAIF", "ADDA Capture"},
	{"ADDA_UL_Mux", "AP_DMIC", "AP DMIC Capture"},

	{"ADDA Capture", NULL, "ADDA Enable"},
	{"ADDA Capture", NULL, "ADDA Capture Enable"},
	{"ADDA Capture", NULL, "AUD_PAD_TOP"},
	{"ADDA Capture", NULL, "ADDA_MTKAIF_CFG"},

	{"AP DMIC Capture", NULL, "ADDA Enable"},
	{"AP DMIC Capture", NULL, "ADDA Capture Enable"},
	{"AP DMIC Capture", NULL, "ADDA_FIFO"},
	{"AP DMIC Capture", NULL, "AP_DMIC_EN"},

	{"AP DMIC Capture", NULL, "AP_DMIC_INPUT"},

	/* clk */
	{"ADDA Playback", NULL, "aud_dac_clk"},
	{"ADDA Playback", NULL, "aud_dac_predis_clk"},
	{"ADDA Playback", NULL, "aud_dac_hires_clk", mtk_afe_dac_hires_connect},

	{"ADDA Capture Enable", NULL, "aud_adc_clk"},
	{"ADDA Capture Enable", NULL, "aud_adc_hires_clk",
	 mtk_afe_adc_hires_connect},

	/* hires source from apll1 */
	{"top_mux_audio_h", NULL, APLL2_W_NAME},

	{"aud_dac_hires_clk", NULL, "top_mux_audio_h"},
	{"aud_adc_hires_clk", NULL, "top_mux_audio_h"},
};

/* dai ops */
static int mtk_dai_adda_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	unsigned int rate = params_rate(params);
	int id = dai->id;
	struct mtk_afe_adda_priv *adda_priv = afe_priv->dai_priv[id];

	dev_dbg(afe->dev, "%s(), id %d, stream %d, rate %d\n",
		__func__, id, substream->stream, rate);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		unsigned int dl_src2_con0;
		unsigned int dl_src2_con1;

		adda_priv->dl_rate = rate;

		/* set sampling rate */
		dl_src2_con0 = adda_dl_rate_transform(afe, rate) <<
			       DL_2_INPUT_MODE_CTL_SFT;

		/* set output mode, UP_SAMPLING_RATE_X8 */
		dl_src2_con0 |= (0x3 << DL_2_OUTPUT_SEL_CTL_SFT);

		/* turn off mute function */
		dl_src2_con0 |= BIT(DL_2_MUTE_CH2_OFF_CTL_PRE_SFT);
		dl_src2_con0 |= BIT(DL_2_MUTE_CH1_OFF_CTL_PRE_SFT);

		/* set voice input data if input sample rate is 8k or 16k */
		if (rate == 8000 || rate == 16000)
			dl_src2_con0 |= BIT(DL_2_VOICE_MODE_CTL_PRE_SFT);

		/* SA suggest apply -0.3db to audio/speech path */
		dl_src2_con1 = MTK_AFE_ADDA_DL_GAIN_NORMAL <<
			       DL_2_GAIN_CTL_PRE_SFT;

		/* turn on down-link gain */
		dl_src2_con0 |= BIT(DL_2_GAIN_ON_CTL_PRE_SFT);

		if (id == MT8186_DAI_ADDA) {
			/* clean predistortion */
			regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON0, 0);
			regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON1, 0);

			regmap_write(afe->regmap,
				     AFE_ADDA_DL_SRC2_CON0, dl_src2_con0);
			regmap_write(afe->regmap,
				     AFE_ADDA_DL_SRC2_CON1, dl_src2_con1);

			/* set sdm gain */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_DL_SDM_DCCOMP_CON,
					   ATTGAIN_CTL_MASK_SFT,
					   AUDIO_SDM_LEVEL_NORMAL <<
					   ATTGAIN_CTL_SFT);

			/* Use new 2nd sdm */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_DL_SDM_DITHER_CON,
					   AFE_DL_SDM_DITHER_64TAP_EN_MASK_SFT,
					   BIT(AFE_DL_SDM_DITHER_64TAP_EN_SFT));
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_DL_SDM_AUTO_RESET_CON,
					   AFE_DL_USE_NEW_2ND_SDM_MASK_SFT,
					   BIT(AFE_DL_USE_NEW_2ND_SDM_SFT));
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_DL_SDM_DCCOMP_CON,
					   USE_3RD_SDM_MASK_SFT,
					   AUDIO_SDM_2ND << USE_3RD_SDM_SFT);

			/* sdm auto reset */
			regmap_write(afe->regmap,
				     AFE_ADDA_DL_SDM_AUTO_RESET_CON,
				     SDM_AUTO_RESET_THRESHOLD);
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_DL_SDM_AUTO_RESET_CON,
					   SDM_AUTO_RESET_TEST_ON_MASK_SFT,
					   BIT(SDM_AUTO_RESET_TEST_ON_SFT));
		}
	} else {
		unsigned int ul_src_con0 = 0;
		unsigned int voice_mode = adda_ul_rate_transform(afe, rate);

		adda_priv->ul_rate = rate;
		ul_src_con0 |= (voice_mode << 17) & (0x7 << 17);

		/* enable iir */
		ul_src_con0 |= (1 << UL_IIR_ON_TMP_CTL_SFT) &
			       UL_IIR_ON_TMP_CTL_MASK_SFT;
		ul_src_con0 |= (UL_IIR_SW << UL_IIRMODE_CTL_SFT) &
			       UL_IIRMODE_CTL_MASK_SFT;
		switch (id) {
		case MT8186_DAI_ADDA:
		case MT8186_DAI_AP_DMIC:
			/* 35Hz @ 48k */
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_02_01, 0);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_04_03, 0x3fb8);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_06_05, 0x3fb80000);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_08_07, 0x3fb80000);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_10_09, 0xc048);

			regmap_write(afe->regmap,
				     AFE_ADDA_UL_SRC_CON0, ul_src_con0);

			/* Using Internal ADC */
			regmap_update_bits(afe->regmap, AFE_ADDA_TOP_CON0, BIT(0), 0);

			/* mtkaif_rxif_data_mode = 0, amic */
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG0, BIT(0), 0);
			break;
		default:
			break;
		}

		/* ap dmic */
		switch (id) {
		case MT8186_DAI_AP_DMIC:
			mtk_adda_ul_src_dmic(afe, id);
			break;
		default:
			break;
		}
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
		.id = MT8186_DAI_ADDA,
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
	{
		.name = "AP_DMIC",
		.id = MT8186_DAI_AP_DMIC,
		.capture = {
			.stream_name = "AP DMIC Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
};

int mt8186_dai_adda_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

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

	/* set dai priv */
	ret = mt8186_dai_set_priv(afe, MT8186_DAI_ADDA,
				  sizeof(struct mtk_afe_adda_priv), NULL);
	if (ret)
		return ret;

	/* ap dmic priv share with adda */
	afe_priv->dai_priv[MT8186_DAI_AP_DMIC] =
		afe_priv->dai_priv[MT8186_DAI_ADDA];

	return 0;
}
