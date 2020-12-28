// SPDX-License-Identifier: GPL-2.0
//
// MediaTek ALSA SoC Audio DAI ADDA Control
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Shane Chien <shane.chien@mediatek.com>
//

#include <linux/delay.h>
#include <linux/regmap.h>

#include "mt8192-afe-clk.h"
#include "mt8192-afe-common.h"
#include "mt8192-afe-gpio.h"
#include "mt8192-interconnection.h"

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
		dev_warn(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
		return MTK_AFE_ADDA_DL_RATE_48K;
	}
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
		dev_warn(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
		return MTK_AFE_ADDA_UL_RATE_48K;
	}
}

/* dai component */
static const struct snd_kcontrol_new mtk_adda_dl_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN3, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH1", AFE_CONN3, I_DL12_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN3, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN3, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1", AFE_CONN3_1, I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1", AFE_CONN3_1, I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1", AFE_CONN3_1, I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH1", AFE_CONN3_1, I_DL8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN3,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN3,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN3,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH1", AFE_CONN3,
				    I_GAIN1_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN3,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN3,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH1", AFE_CONN3_1,
				    I_SRC_1_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_2_OUT_CH1", AFE_CONN3_1,
				    I_SRC_2_OUT_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN4, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN4, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH2", AFE_CONN4, I_DL12_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN4, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN4, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN4, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN4, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2", AFE_CONN4_1, I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2", AFE_CONN4_1, I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2", AFE_CONN4_1, I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL8_CH2", AFE_CONN4_1, I_DL8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN4,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN4,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN4,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH2", AFE_CONN4,
				    I_GAIN1_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN4,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN4,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2", AFE_CONN4,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2", AFE_CONN4,
				    I_PCM_2_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH2", AFE_CONN4_1,
				    I_SRC_1_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_2_OUT_CH2", AFE_CONN4_1,
				    I_SRC_2_OUT_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch3_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN52, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH1", AFE_CONN52, I_DL12_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN52, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN52, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1", AFE_CONN52_1, I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1", AFE_CONN52_1, I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1", AFE_CONN52_1, I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN52,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN52,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN52,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH1", AFE_CONN52,
				    I_GAIN1_OUT_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN52,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN52,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch4_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN53, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN53, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH2", AFE_CONN53, I_DL12_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN53, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN53, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN53, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN53, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2", AFE_CONN53_1, I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2", AFE_CONN53_1, I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2", AFE_CONN53_1, I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN53,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN53,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN53,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("GAIN1_OUT_CH2", AFE_CONN53,
				    I_GAIN1_OUT_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN53,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN53,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH2", AFE_CONN53,
				    I_PCM_1_CAP_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH2", AFE_CONN53,
				    I_PCM_2_CAP_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_stf_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN19,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_stf_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN20,
				    I_ADDA_UL_CH2, 1, 0),
};

enum {
	SUPPLY_SEQ_ADDA_AFE_ON,
	SUPPLY_SEQ_ADDA_DL_ON,
	SUPPLY_SEQ_ADDA_AUD_PAD_TOP,
	SUPPLY_SEQ_ADDA_MTKAIF_CFG,
	SUPPLY_SEQ_ADDA6_MTKAIF_CFG,
	SUPPLY_SEQ_ADDA_FIFO,
	SUPPLY_SEQ_ADDA_AP_DMIC,
	SUPPLY_SEQ_ADDA_UL_ON,
};

static int mtk_adda_ul_src_dmic(struct mtk_base_afe *afe, int id)
{
	unsigned int reg;

	switch (id) {
	case MT8192_DAI_ADDA:
	case MT8192_DAI_AP_DMIC:
		reg = AFE_ADDA_UL_SRC_CON0;
		break;
	case MT8192_DAI_ADDA_CH34:
	case MT8192_DAI_AP_DMIC_CH34:
		reg = AFE_ADDA6_UL_SRC_CON0;
		break;
	default:
		return -EINVAL;
	}

	/* dmic mode, 3.25M*/
	regmap_update_bits(afe->regmap, reg,
			   DIGMIC_3P25M_1P625M_SEL_CTL_MASK_SFT,
			   0x0);
	regmap_update_bits(afe->regmap, reg,
			   DMIC_LOW_POWER_MODE_CTL_MASK_SFT,
			   0x0);

	/* turn on dmic, ch1, ch2 */
	regmap_update_bits(afe->regmap, reg,
			   UL_SDM_3_LEVEL_CTL_MASK_SFT,
			   0x1 << UL_SDM_3_LEVEL_CTL_SFT);
	regmap_update_bits(afe->regmap, reg,
			   UL_MODE_3P25M_CH1_CTL_MASK_SFT,
			   0x1 << UL_MODE_3P25M_CH1_CTL_SFT);
	regmap_update_bits(afe->regmap, reg,
			   UL_MODE_3P25M_CH2_CTL_MASK_SFT,
			   0x1 << UL_MODE_3P25M_CH2_CTL_SFT);
	return 0;
}

static int mtk_adda_ul_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int mtkaif_dmic = afe_priv->mtkaif_dmic;

	dev_info(afe->dev, "%s(), name %s, event 0x%x, mtkaif_dmic %d\n",
		 __func__, w->name, event, mtkaif_dmic);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8192_afe_gpio_request(afe->dev, true, MT8192_DAI_ADDA, 1);

		/* update setting to dmic */
		if (mtkaif_dmic) {
			/* mtkaif_rxif_data_mode = 1, dmic */
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG0,
					   0x1, 0x1);

			/* dmic mode, 3.25M*/
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_RX_CFG0,
					   MTKAIF_RXIF_VOICE_MODE_MASK_SFT,
					   0x0);
			mtk_adda_ul_src_dmic(afe, MT8192_DAI_ADDA);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		mt8192_afe_gpio_request(afe->dev, false, MT8192_DAI_ADDA, 1);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_adda_ch34_ul_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol,
				  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int mtkaif_dmic = afe_priv->mtkaif_dmic_ch34;
	int mtkaif_adda6_only = afe_priv->mtkaif_adda6_only;

	dev_info(afe->dev,
		 "%s(), name %s, event 0x%x, mtkaif_dmic %d, mtkaif_adda6_only %d\n",
		 __func__, w->name, event, mtkaif_dmic, mtkaif_adda6_only);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8192_afe_gpio_request(afe->dev, true, MT8192_DAI_ADDA_CH34,
					1);

		/* update setting to dmic */
		if (mtkaif_dmic) {
			/* mtkaif_rxif_data_mode = 1, dmic */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA6_MTKAIF_RX_CFG0,
					   0x1, 0x1);

			/* dmic mode, 3.25M*/
			regmap_update_bits(afe->regmap,
					   AFE_ADDA6_MTKAIF_RX_CFG0,
					   MTKAIF_RXIF_VOICE_MODE_MASK_SFT,
					   0x0);
			mtk_adda_ul_src_dmic(afe, MT8192_DAI_ADDA_CH34);
		}

		/* when using adda6 without adda enabled,
		 * RG_ADDA6_MTKAIF_RX_SYNC_WORD2_DISABLE_SFT need to be set or
		 * data cannot be received.
		 */
		if (mtkaif_adda6_only) {
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_MTKAIF_SYNCWORD_CFG,
					   0x1 << 23, 0x1 << 23);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		mt8192_afe_gpio_request(afe->dev, false, MT8192_DAI_ADDA_CH34,
					1);

		/* reset dmic */
		afe_priv->mtkaif_dmic_ch34 = 0;

		if (mtkaif_adda6_only) {
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_MTKAIF_SYNCWORD_CFG,
					   0x1 << 23, 0x0 << 23);
		}
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
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (afe_priv->mtkaif_protocol == MTKAIF_PROTOCOL_2_CLK_P2)
			regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x38);
		else if (afe_priv->mtkaif_protocol == MTKAIF_PROTOCOL_2)
			regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x30);
		else
			regmap_write(afe->regmap, AFE_AUD_PAD_TOP, 0x30);
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
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int delay_data;
	int delay_cycle;

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (afe_priv->mtkaif_protocol == MTKAIF_PROTOCOL_2_CLK_P2) {
			/* set protocol 2 */
			regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0,
				     0x00010000);
			regmap_write(afe->regmap, AFE_ADDA6_MTKAIF_CFG0,
				     0x00010000);

			if (strcmp(w->name, "ADDA_MTKAIF_CFG") == 0 &&
			    (afe_priv->mtkaif_chosen_phase[0] < 0 ||
			     afe_priv->mtkaif_chosen_phase[1] < 0)) {
				dev_warn(afe->dev,
					 "%s(), mtkaif_chosen_phase[0/1]:%d/%d\n",
					 __func__,
					 afe_priv->mtkaif_chosen_phase[0],
					 afe_priv->mtkaif_chosen_phase[1]);
				break;
			} else if (strcmp(w->name, "ADDA6_MTKAIF_CFG") == 0 &&
				   afe_priv->mtkaif_chosen_phase[2] < 0) {
				dev_warn(afe->dev,
					 "%s(), mtkaif_chosen_phase[2]:%d\n",
					 __func__,
					 afe_priv->mtkaif_chosen_phase[2]);
				break;
			}

			/* mtkaif_rxif_clkinv_adc inverse for calibration */
			regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIF_CFG0,
					   MTKAIF_RXIF_CLKINV_ADC_MASK_SFT,
					   0x1 << MTKAIF_RXIF_CLKINV_ADC_SFT);
			regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIF_CFG0,
					   MTKAIF_RXIF_CLKINV_ADC_MASK_SFT,
					   0x1 << MTKAIF_RXIF_CLKINV_ADC_SFT);

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

			/* set delay between ch3 and ch2 */
			if (afe_priv->mtkaif_phase_cycle[2] >=
			    afe_priv->mtkaif_phase_cycle[1]) {
				delay_data = DELAY_DATA_MISO1;	/* ch3 */
				delay_cycle = afe_priv->mtkaif_phase_cycle[2] -
					      afe_priv->mtkaif_phase_cycle[1];
			} else {
				delay_data = DELAY_DATA_MISO2;	/* ch2 */
				delay_cycle = afe_priv->mtkaif_phase_cycle[1] -
					      afe_priv->mtkaif_phase_cycle[2];
			}

			regmap_update_bits(afe->regmap,
					   AFE_ADDA6_MTKAIF_RX_CFG2,
					   MTKAIF_RXIF_DELAY_DATA_MASK_SFT,
					   delay_data <<
					   MTKAIF_RXIF_DELAY_DATA_SFT);
			regmap_update_bits(afe->regmap,
					   AFE_ADDA6_MTKAIF_RX_CFG2,
					   MTKAIF_RXIF_DELAY_CYCLE_MASK_SFT,
					   delay_cycle <<
					   MTKAIF_RXIF_DELAY_CYCLE_SFT);
		} else if (afe_priv->mtkaif_protocol == MTKAIF_PROTOCOL_2) {
			regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0,
				     0x00010000);
			regmap_write(afe->regmap, AFE_ADDA6_MTKAIF_CFG0,
				     0x00010000);
		} else {
			regmap_write(afe->regmap, AFE_ADDA_MTKAIF_CFG0, 0x0);
			regmap_write(afe->regmap, AFE_ADDA6_MTKAIF_CFG0, 0x0);
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

	dev_info(afe->dev, "%s(), name %s, event 0x%x\n",
		 __func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8192_afe_gpio_request(afe->dev, true, MT8192_DAI_ADDA, 0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		mt8192_afe_gpio_request(afe->dev, false, MT8192_DAI_ADDA, 0);
		break;
	default:
		break;
	}

	return 0;
}

static int mtk_adda_ch34_dl_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol,
				  int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_info(afe->dev, "%s(), name %s, event 0x%x\n",
		 __func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		mt8192_afe_gpio_request(afe->dev, true, MT8192_DAI_ADDA_CH34,
					0);
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		mt8192_afe_gpio_request(afe->dev, false, MT8192_DAI_ADDA_CH34,
					0);
		break;
	default:
		break;
	}

	return 0;
}

/* stf */
static int stf_positive_gain_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->stf_positive_gain_db;
	return 0;
}

static int stf_positive_gain_set(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int gain_db = ucontrol->value.integer.value[0];

	afe_priv->stf_positive_gain_db = gain_db;

	if (gain_db >= 0 && gain_db <= 24) {
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_GAIN,
				   POSITIVE_GAIN_MASK_SFT,
				   (gain_db / 6) << POSITIVE_GAIN_SFT);
	} else {
		dev_warn(afe->dev, "%s(), gain_db %d invalid\n",
			 __func__, gain_db);
	}
	return 0;
}

static int mt8192_adda_dmic_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->mtkaif_dmic;
	return 0;
}

static int mt8192_adda_dmic_set(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int dmic_on;

	dmic_on = ucontrol->value.integer.value[0];

	dev_info(afe->dev, "%s(), kcontrol name %s, dmic_on %d\n",
		 __func__, kcontrol->id.name, dmic_on);

	afe_priv->mtkaif_dmic = dmic_on;
	afe_priv->mtkaif_dmic_ch34 = dmic_on;
	return 0;
}

static int mt8192_adda6_only_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	ucontrol->value.integer.value[0] = afe_priv->mtkaif_adda6_only;
	return 0;
}

static int mt8192_adda6_only_set(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int mtkaif_adda6_only;

	mtkaif_adda6_only = ucontrol->value.integer.value[0];

	dev_info(afe->dev, "%s(), kcontrol name %s, mtkaif_adda6_only %d\n",
		 __func__, kcontrol->id.name, mtkaif_adda6_only);

	afe_priv->mtkaif_adda6_only = mtkaif_adda6_only;
	return 0;
}

static const struct snd_kcontrol_new mtk_adda_controls[] = {
	SOC_SINGLE("Sidetone_Gain", AFE_SIDETONE_GAIN,
		   SIDE_TONE_GAIN_SFT, SIDE_TONE_GAIN_MASK, 0),
	SOC_SINGLE_EXT("Sidetone_Positive_Gain_dB", SND_SOC_NOPM, 0, 100, 0,
		       stf_positive_gain_get, stf_positive_gain_set),
	SOC_SINGLE("ADDA_DL_GAIN", AFE_ADDA_DL_SRC2_CON1,
		   DL_2_GAIN_CTL_PRE_SFT, DL_2_GAIN_CTL_PRE_MASK, 0),
	SOC_SINGLE_BOOL_EXT("MTKAIF_DMIC Switch", 0,
			    mt8192_adda_dmic_get, mt8192_adda_dmic_set),
	SOC_SINGLE_BOOL_EXT("MTKAIF_ADDA6_ONLY Switch", 0,
			    mt8192_adda6_only_get, mt8192_adda6_only_set),
};

static const struct snd_kcontrol_new stf_ctl =
	SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0);

static const u16 stf_coeff_table_16k[] = {
	0x049C, 0x09E8, 0x09E0, 0x089C,
	0xFF54, 0xF488, 0xEAFC, 0xEBAC,
	0xfA40, 0x17AC, 0x3D1C, 0x6028,
	0x7538
};

static const u16 stf_coeff_table_32k[] = {
	0xFE52, 0x0042, 0x00C5, 0x0194,
	0x029A, 0x03B7, 0x04BF, 0x057D,
	0x05BE, 0x0555, 0x0426, 0x0230,
	0xFF92, 0xFC89, 0xF973, 0xF6C6,
	0xF500, 0xF49D, 0xF603, 0xF970,
	0xFEF3, 0x065F, 0x0F4F, 0x1928,
	0x2329, 0x2C80, 0x345E, 0x3A0D,
	0x3D08
};

static const u16 stf_coeff_table_48k[] = {
	0x0401, 0xFFB0, 0xFF5A, 0xFECE,
	0xFE10, 0xFD28, 0xFC21, 0xFB08,
	0xF9EF, 0xF8E8, 0xF80A, 0xF76C,
	0xF724, 0xF746, 0xF7E6, 0xF90F,
	0xFACC, 0xFD1E, 0xFFFF, 0x0364,
	0x0737, 0x0B62, 0x0FC1, 0x1431,
	0x188A, 0x1CA4, 0x2056, 0x237D,
	0x25F9, 0x27B0, 0x2890
};

static int mtk_stf_event(struct snd_soc_dapm_widget *w,
			 struct snd_kcontrol *kcontrol,
			 int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	size_t half_tap_num;
	const u16 *stf_coeff_table;
	unsigned int ul_rate, reg_value;
	size_t coef_addr;

	regmap_read(afe->regmap, AFE_ADDA_UL_SRC_CON0, &ul_rate);
	ul_rate = ul_rate >> UL_VOICE_MODE_CH1_CH2_CTL_SFT;
	ul_rate = ul_rate & UL_VOICE_MODE_CH1_CH2_CTL_MASK;

	if (ul_rate == MTK_AFE_ADDA_UL_RATE_48K) {
		half_tap_num = ARRAY_SIZE(stf_coeff_table_48k);
		stf_coeff_table = stf_coeff_table_48k;
	} else if (ul_rate == MTK_AFE_ADDA_UL_RATE_32K) {
		half_tap_num = ARRAY_SIZE(stf_coeff_table_32k);
		stf_coeff_table = stf_coeff_table_32k;
	} else {
		half_tap_num = ARRAY_SIZE(stf_coeff_table_16k);
		stf_coeff_table = stf_coeff_table_16k;
	}

	regmap_read(afe->regmap, AFE_SIDETONE_CON1, &reg_value);

	dev_info(afe->dev, "%s(), name %s, event 0x%x, ul_rate 0x%x, AFE_SIDETONE_CON1 0x%x\n",
		 __func__, w->name, event, ul_rate, reg_value);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		/* set side tone gain = 0 */
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_GAIN,
				   SIDE_TONE_GAIN_MASK_SFT,
				   0);
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_GAIN,
				   POSITIVE_GAIN_MASK_SFT,
				   0);
		/* don't bypass stf */
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_CON1,
				   0x1f << 27,
				   0x0);
		/* set stf half tap num */
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_CON1,
				   SIDE_TONE_HALF_TAP_NUM_MASK_SFT,
				   half_tap_num << SIDE_TONE_HALF_TAP_NUM_SFT);

		/* set side tone coefficient */
		regmap_read(afe->regmap, AFE_SIDETONE_CON0, &reg_value);
		for (coef_addr = 0; coef_addr < half_tap_num; coef_addr++) {
			bool old_w_ready = (reg_value >> W_RDY_SFT) & 0x1;
			bool new_w_ready = 0;
			int try_cnt = 0;

			regmap_update_bits(afe->regmap,
					   AFE_SIDETONE_CON0,
					   0x39FFFFF,
					   (1 << R_W_EN_SFT) |
					   (1 << R_W_SEL_SFT) |
					   (0 << SEL_CH2_SFT) |
					   (coef_addr <<
					   SIDE_TONE_COEFFICIENT_ADDR_SFT) |
					   stf_coeff_table[coef_addr]);

			/* wait until flag write_ready changed */
			for (try_cnt = 0; try_cnt < 10; try_cnt++) {
				regmap_read(afe->regmap,
					    AFE_SIDETONE_CON0, &reg_value);
				new_w_ready = (reg_value >> W_RDY_SFT) & 0x1;

				/* flip => ok */
				if (new_w_ready == old_w_ready) {
					udelay(3);
					if (try_cnt == 9) {
						dev_warn(afe->dev,
							 "%s(), write coeff not ready",
							 __func__);
					}
				} else {
					break;
				}
			}
			/* need write -> read -> write to write next coeff */
			regmap_update_bits(afe->regmap,
					   AFE_SIDETONE_CON0,
					   R_W_SEL_MASK_SFT,
					   0x0);
		}
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* bypass stf */
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_CON1,
				   0x1f << 27,
				   0x1f << 27);

		/* set side tone gain = 0 */
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_GAIN,
				   SIDE_TONE_GAIN_MASK_SFT,
				   0);
		regmap_update_bits(afe->regmap,
				   AFE_SIDETONE_GAIN,
				   POSITIVE_GAIN_MASK_SFT,
				   0);
		break;
	default:
		break;
	}

	return 0;
}

/* stf mux */
enum {
	STF_SRC_ADDA_ADDA6 = 0,
	STF_SRC_O19O20,
};

static const char *const stf_o19o20_mux_map[] = {
	"ADDA_ADDA6",
	"O19O20",
};

static int stf_o19o20_mux_map_value[] = {
	STF_SRC_ADDA_ADDA6,
	STF_SRC_O19O20,
};

static SOC_VALUE_ENUM_SINGLE_DECL(stf_o19o20_mux_map_enum,
				  AFE_SIDETONE_CON1,
				  STF_SOURCE_FROM_O19O20_SFT,
				  STF_SOURCE_FROM_O19O20_MASK,
				  stf_o19o20_mux_map,
				  stf_o19o20_mux_map_value);

static const struct snd_kcontrol_new stf_o19O20_mux_control =
	SOC_DAPM_ENUM("STF_O19O20_MUX", stf_o19o20_mux_map_enum);

enum {
	STF_SRC_ADDA = 0,
	STF_SRC_ADDA6,
};

static const char *const stf_adda_mux_map[] = {
	"ADDA",
	"ADDA6",
};

static int stf_adda_mux_map_value[] = {
	STF_SRC_ADDA,
	STF_SRC_ADDA6,
};

static SOC_VALUE_ENUM_SINGLE_DECL(stf_adda_mux_map_enum,
				  AFE_SIDETONE_CON1,
				  STF_O19O20_OUT_EN_SEL_SFT,
				  STF_O19O20_OUT_EN_SEL_MASK,
				  stf_adda_mux_map,
				  stf_adda_mux_map_value);

static const struct snd_kcontrol_new stf_adda_mux_control =
	SOC_DAPM_ENUM("STF_ADDA_MUX", stf_adda_mux_map_enum);

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

static const struct snd_kcontrol_new adda_ch34_ul_mux_control =
	SOC_DAPM_ENUM("ADDA_CH34_UL_MUX Select", adda_ul_mux_map_enum);

static const struct snd_soc_dapm_widget mtk_dai_adda_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("ADDA_DL_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch1_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch1_mix)),
	SND_SOC_DAPM_MIXER("ADDA_DL_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch2_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch2_mix)),

	SND_SOC_DAPM_MIXER("ADDA_DL_CH3", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch3_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch3_mix)),
	SND_SOC_DAPM_MIXER("ADDA_DL_CH4", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch4_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch4_mix)),

	SND_SOC_DAPM_SUPPLY_S("ADDA Enable", SUPPLY_SEQ_ADDA_AFE_ON,
			      AFE_ADDA_UL_DL_CON0, ADDA_AFE_ON_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADDA Playback Enable", SUPPLY_SEQ_ADDA_DL_ON,
			      AFE_ADDA_DL_SRC2_CON0,
			      DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0,
			      mtk_adda_dl_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("ADDA CH34 Playback Enable",
			      SUPPLY_SEQ_ADDA_DL_ON,
			      AFE_ADDA_3RD_DAC_DL_SRC2_CON0,
			      DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0,
			      mtk_adda_ch34_dl_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("ADDA Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA_UL_SRC_CON0,
			      UL_SRC_ON_TMP_CTL_SFT, 0,
			      mtk_adda_ul_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("ADDA CH34 Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA6_UL_SRC_CON0,
			      UL_SRC_ON_TMP_CTL_SFT, 0,
			      mtk_adda_ch34_ul_event,
			      SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("AUD_PAD_TOP", SUPPLY_SEQ_ADDA_AUD_PAD_TOP,
			      AFE_AUD_PAD_TOP,
			      RG_RX_FIFO_ON_SFT, 0,
			      mtk_adda_pad_top_event,
			      SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY_S("ADDA_MTKAIF_CFG", SUPPLY_SEQ_ADDA_MTKAIF_CFG,
			      SND_SOC_NOPM, 0, 0,
			      mtk_adda_mtkaif_cfg_event,
			      SND_SOC_DAPM_PRE_PMU),
	SND_SOC_DAPM_SUPPLY_S("ADDA6_MTKAIF_CFG", SUPPLY_SEQ_ADDA6_MTKAIF_CFG,
			      SND_SOC_NOPM, 0, 0,
			      mtk_adda_mtkaif_cfg_event,
			      SND_SOC_DAPM_PRE_PMU),

	SND_SOC_DAPM_SUPPLY_S("AP_DMIC_EN", SUPPLY_SEQ_ADDA_AP_DMIC,
			      AFE_ADDA_UL_SRC_CON0,
			      UL_AP_DMIC_ON_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("AP_DMIC_CH34_EN", SUPPLY_SEQ_ADDA_AP_DMIC,
			      AFE_ADDA6_UL_SRC_CON0,
			      UL_AP_DMIC_ON_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADDA_FIFO", SUPPLY_SEQ_ADDA_FIFO,
			      AFE_ADDA_UL_DL_CON0,
			      AFE_ADDA_FIFO_AUTO_RST_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADDA_CH34_FIFO", SUPPLY_SEQ_ADDA_FIFO,
			      AFE_ADDA_UL_DL_CON0,
			      AFE_ADDA6_FIFO_AUTO_RST_SFT, 1,
			      NULL, 0),

	SND_SOC_DAPM_MUX("ADDA_UL_Mux", SND_SOC_NOPM, 0, 0,
			 &adda_ul_mux_control),
	SND_SOC_DAPM_MUX("ADDA_CH34_UL_Mux", SND_SOC_NOPM, 0, 0,
			 &adda_ch34_ul_mux_control),

	SND_SOC_DAPM_INPUT("AP_DMIC_INPUT"),
	SND_SOC_DAPM_INPUT("AP_DMIC_CH34_INPUT"),

	/* stf */
	SND_SOC_DAPM_SWITCH_E("Sidetone Filter",
			      AFE_SIDETONE_CON1, SIDE_TONE_ON_SFT, 0,
			      &stf_ctl,
			      mtk_stf_event,
			      SND_SOC_DAPM_PRE_PMU |
			      SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_MUX("STF_O19O20_MUX", SND_SOC_NOPM, 0, 0,
			 &stf_o19O20_mux_control),
	SND_SOC_DAPM_MUX("STF_ADDA_MUX", SND_SOC_NOPM, 0, 0,
			 &stf_adda_mux_control),
	SND_SOC_DAPM_MIXER("STF_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_stf_ch1_mix,
			   ARRAY_SIZE(mtk_stf_ch1_mix)),
	SND_SOC_DAPM_MIXER("STF_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_stf_ch2_mix,
			   ARRAY_SIZE(mtk_stf_ch2_mix)),
	SND_SOC_DAPM_OUTPUT("STF_OUTPUT"),

	/* clock */
	SND_SOC_DAPM_CLOCK_SUPPLY("top_mux_audio_h"),

	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_dac_predis_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_3rd_dac_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_3rd_dac_predis_clk"),

	SND_SOC_DAPM_CLOCK_SUPPLY("aud_adc_clk"),
	SND_SOC_DAPM_CLOCK_SUPPLY("aud_adda6_adc_clk"),
};

static const struct snd_soc_dapm_route mtk_dai_adda_routes[] = {
	/* playback */
	{"ADDA_DL_CH1", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH2", "DL1"},

	{"ADDA_DL_CH1", "DL12_CH1", "DL12"},
	{"ADDA_DL_CH2", "DL12_CH2", "DL12"},

	{"ADDA_DL_CH1", "DL6_CH1", "DL6"},
	{"ADDA_DL_CH2", "DL6_CH2", "DL6"},

	{"ADDA_DL_CH1", "DL8_CH1", "DL8"},
	{"ADDA_DL_CH2", "DL8_CH2", "DL8"},

	{"ADDA_DL_CH1", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH2", "DL2"},

	{"ADDA_DL_CH1", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH2", "DL3"},

	{"ADDA_DL_CH1", "DL4_CH1", "DL4"},
	{"ADDA_DL_CH2", "DL4_CH2", "DL4"},

	{"ADDA_DL_CH1", "DL5_CH1", "DL5"},
	{"ADDA_DL_CH2", "DL5_CH2", "DL5"},

	{"ADDA Playback", NULL, "ADDA_DL_CH1"},
	{"ADDA Playback", NULL, "ADDA_DL_CH2"},

	{"ADDA Playback", NULL, "ADDA Enable"},
	{"ADDA Playback", NULL, "ADDA Playback Enable"},

	{"ADDA_DL_CH3", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH4", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH4", "DL1_CH2", "DL1"},

	{"ADDA_DL_CH3", "DL12_CH1", "DL12"},
	{"ADDA_DL_CH4", "DL12_CH2", "DL12"},

	{"ADDA_DL_CH3", "DL6_CH1", "DL6"},
	{"ADDA_DL_CH4", "DL6_CH2", "DL6"},

	{"ADDA_DL_CH3", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH4", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH4", "DL2_CH2", "DL2"},

	{"ADDA_DL_CH3", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH4", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH4", "DL3_CH2", "DL3"},

	{"ADDA_DL_CH3", "DL4_CH1", "DL4"},
	{"ADDA_DL_CH4", "DL4_CH2", "DL4"},

	{"ADDA_DL_CH3", "DL5_CH1", "DL5"},
	{"ADDA_DL_CH4", "DL5_CH2", "DL5"},

	{"ADDA CH34 Playback", NULL, "ADDA_DL_CH3"},
	{"ADDA CH34 Playback", NULL, "ADDA_DL_CH4"},

	{"ADDA CH34 Playback", NULL, "ADDA Enable"},
	{"ADDA CH34 Playback", NULL, "ADDA CH34 Playback Enable"},

	/* capture */
	{"ADDA_UL_Mux", "MTKAIF", "ADDA Capture"},
	{"ADDA_UL_Mux", "AP_DMIC", "AP DMIC Capture"},

	{"ADDA_CH34_UL_Mux", "MTKAIF", "ADDA CH34 Capture"},
	{"ADDA_CH34_UL_Mux", "AP_DMIC", "AP DMIC CH34 Capture"},

	{"ADDA Capture", NULL, "ADDA Enable"},
	{"ADDA Capture", NULL, "ADDA Capture Enable"},
	{"ADDA Capture", NULL, "AUD_PAD_TOP"},
	{"ADDA Capture", NULL, "ADDA_MTKAIF_CFG"},

	{"AP DMIC Capture", NULL, "ADDA Enable"},
	{"AP DMIC Capture", NULL, "ADDA Capture Enable"},
	{"AP DMIC Capture", NULL, "ADDA_FIFO"},
	{"AP DMIC Capture", NULL, "AP_DMIC_EN"},

	{"ADDA CH34 Capture", NULL, "ADDA Enable"},
	{"ADDA CH34 Capture", NULL, "ADDA CH34 Capture Enable"},
	{"ADDA CH34 Capture", NULL, "AUD_PAD_TOP"},
	{"ADDA CH34 Capture", NULL, "ADDA6_MTKAIF_CFG"},

	{"AP DMIC CH34 Capture", NULL, "ADDA Enable"},
	{"AP DMIC CH34 Capture", NULL, "ADDA CH34 Capture Enable"},
	{"AP DMIC CH34 Capture", NULL, "ADDA_CH34_FIFO"},
	{"AP DMIC CH34 Capture", NULL, "AP_DMIC_CH34_EN"},

	{"AP DMIC Capture", NULL, "AP_DMIC_INPUT"},
	{"AP DMIC CH34 Capture", NULL, "AP_DMIC_CH34_INPUT"},

	/* sidetone filter */
	{"STF_ADDA_MUX", "ADDA", "ADDA_UL_Mux"},
	{"STF_ADDA_MUX", "ADDA6", "ADDA_CH34_UL_Mux"},

	{"STF_O19O20_MUX", "ADDA_ADDA6", "STF_ADDA_MUX"},
	{"STF_O19O20_MUX", "O19O20", "STF_CH1"},
	{"STF_O19O20_MUX", "O19O20", "STF_CH2"},

	{"Sidetone Filter", "Switch", "STF_O19O20_MUX"},
	{"STF_OUTPUT", NULL, "Sidetone Filter"},
	{"ADDA Playback", NULL, "Sidetone Filter"},
	{"ADDA CH34 Playback", NULL, "Sidetone Filter"},

	/* clk */
	{"ADDA Playback", NULL, "aud_dac_clk"},
	{"ADDA Playback", NULL, "aud_dac_predis_clk"},

	{"ADDA CH34 Playback", NULL, "aud_3rd_dac_clk"},
	{"ADDA CH34 Playback", NULL, "aud_3rd_dac_predis_clk"},

	{"ADDA Capture Enable", NULL, "aud_adc_clk"},
	{"ADDA CH34 Capture Enable", NULL, "aud_adda6_adc_clk"},
};

/* dai ops */
static int mtk_dai_adda_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	unsigned int rate = params_rate(params);
	int id = dai->id;

	dev_info(afe->dev, "%s(), id %d, stream %d, rate %d\n",
		 __func__,
		 id,
		 substream->stream,
		 rate);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		unsigned int dl_src2_con0 = 0;
		unsigned int dl_src2_con1 = 0;

		/* set sampling rate */
		dl_src2_con0 = adda_dl_rate_transform(afe, rate) <<
			       DL_2_INPUT_MODE_CTL_SFT;

		/* set output mode, UP_SAMPLING_RATE_X8 */
		dl_src2_con0 |= (0x3 << DL_2_OUTPUT_SEL_CTL_SFT);

		/* turn off mute function */
		dl_src2_con0 |= (0x01 << DL_2_MUTE_CH2_OFF_CTL_PRE_SFT);
		dl_src2_con0 |= (0x01 << DL_2_MUTE_CH1_OFF_CTL_PRE_SFT);

		/* set voice input data if input sample rate is 8k or 16k */
		if (rate == 8000 || rate == 16000)
			dl_src2_con0 |= 0x01 << DL_2_VOICE_MODE_CTL_PRE_SFT;

		/* SA suggest apply -0.3db to audio/speech path */
		dl_src2_con1 = MTK_AFE_ADDA_DL_GAIN_NORMAL <<
			       DL_2_GAIN_CTL_PRE_SFT;

		/* turn on down-link gain */
		dl_src2_con0 |= (0x01 << DL_2_GAIN_ON_CTL_PRE_SFT);

		if (id == MT8192_DAI_ADDA) {
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

			/* 2nd sdm */
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
					   ADDA_SDM_AUTO_RESET_ONOFF_MASK_SFT,
					   0x1 << ADDA_SDM_AUTO_RESET_ONOFF_SFT);
		} else {
			/* clean predistortion */
			regmap_write(afe->regmap,
				     AFE_ADDA_3RD_DAC_PREDIS_CON0, 0);
			regmap_write(afe->regmap,
				     AFE_ADDA_3RD_DAC_PREDIS_CON1, 0);

			regmap_write(afe->regmap, AFE_ADDA_3RD_DAC_DL_SRC2_CON0,
				     dl_src2_con0);
			regmap_write(afe->regmap, AFE_ADDA_3RD_DAC_DL_SRC2_CON1,
				     dl_src2_con1);

			/* set sdm gain */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_3RD_DAC_DL_SDM_DCCOMP_CON,
					   ATTGAIN_CTL_MASK_SFT,
					   AUDIO_SDM_LEVEL_NORMAL <<
					   ATTGAIN_CTL_SFT);

			/* 2nd sdm */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_3RD_DAC_DL_SDM_DCCOMP_CON,
					   USE_3RD_SDM_MASK_SFT,
					   AUDIO_SDM_2ND << USE_3RD_SDM_SFT);

			/* sdm auto reset */
			regmap_write(afe->regmap,
				     AFE_ADDA_3RD_DAC_DL_SDM_AUTO_RESET_CON,
				     SDM_AUTO_RESET_THRESHOLD);
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_3RD_DAC_DL_SDM_AUTO_RESET_CON,
					   ADDA_3RD_DAC_SDM_AUTO_RESET_ONOFF_MASK_SFT,
					   0x1 << ADDA_3RD_DAC_SDM_AUTO_RESET_ONOFF_SFT);
		}
	} else {
		unsigned int voice_mode = 0;
		unsigned int ul_src_con0 = 0;	/* default value */

		voice_mode = adda_ul_rate_transform(afe, rate);

		ul_src_con0 |= (voice_mode << 17) & (0x7 << 17);

		/* enable iir */
		ul_src_con0 |= (1 << UL_IIR_ON_TMP_CTL_SFT) &
			       UL_IIR_ON_TMP_CTL_MASK_SFT;
		ul_src_con0 |= (UL_IIR_SW << UL_IIRMODE_CTL_SFT) &
			       UL_IIRMODE_CTL_MASK_SFT;

		switch (id) {
		case MT8192_DAI_ADDA:
		case MT8192_DAI_AP_DMIC:
			/* 35Hz @ 48k */
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_02_01, 0x00000000);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_04_03, 0x00003FB8);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_06_05, 0x3FB80000);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_08_07, 0x3FB80000);
			regmap_write(afe->regmap,
				     AFE_ADDA_IIR_COEF_10_09, 0x0000C048);

			regmap_write(afe->regmap,
				     AFE_ADDA_UL_SRC_CON0, ul_src_con0);

			/* Using Internal ADC */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_TOP_CON0,
					   0x1 << 0,
					   0x0 << 0);

			/* mtkaif_rxif_data_mode = 0, amic */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_MTKAIF_RX_CFG0,
					   0x1 << 0,
					   0x0 << 0);
			break;
		case MT8192_DAI_ADDA_CH34:
		case MT8192_DAI_AP_DMIC_CH34:
			/* 35Hz @ 48k */
			regmap_write(afe->regmap,
				     AFE_ADDA6_IIR_COEF_02_01, 0x00000000);
			regmap_write(afe->regmap,
				     AFE_ADDA6_IIR_COEF_04_03, 0x00003FB8);
			regmap_write(afe->regmap,
				     AFE_ADDA6_IIR_COEF_06_05, 0x3FB80000);
			regmap_write(afe->regmap,
				     AFE_ADDA6_IIR_COEF_08_07, 0x3FB80000);
			regmap_write(afe->regmap,
				     AFE_ADDA6_IIR_COEF_10_09, 0x0000C048);

			regmap_write(afe->regmap,
				     AFE_ADDA6_UL_SRC_CON0, ul_src_con0);

			/* Using Internal ADC */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA6_TOP_CON0,
					   0x1 << 0,
					   0x0 << 0);

			/* mtkaif_rxif_data_mode = 0, amic */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA6_MTKAIF_RX_CFG0,
					   0x1 << 0,
					   0x0 << 0);
			break;
		default:
			break;
		}

		/* ap dmic */
		switch (id) {
		case MT8192_DAI_AP_DMIC:
		case MT8192_DAI_AP_DMIC_CH34:
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
		.id = MT8192_DAI_ADDA,
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
		.name = "ADDA_CH34",
		.id = MT8192_DAI_ADDA_CH34,
		.playback = {
			.stream_name = "ADDA CH34 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_PLAYBACK_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.capture = {
			.stream_name = "ADDA CH34 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
	{
		.name = "AP_DMIC",
		.id = MT8192_DAI_AP_DMIC,
		.capture = {
			.stream_name = "AP DMIC Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
	{
		.name = "AP_DMIC_CH34",
		.id = MT8192_DAI_AP_DMIC_CH34,
		.capture = {
			.stream_name = "AP DMIC CH34 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
};

int mt8192_dai_adda_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	dev_info(afe->dev, "%s()\n", __func__);

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

	/* ap dmic priv share with adda */
	afe_priv->dai_priv[MT8192_DAI_AP_DMIC] =
		afe_priv->dai_priv[MT8192_DAI_ADDA];
	afe_priv->dai_priv[MT8192_DAI_AP_DMIC_CH34] =
		afe_priv->dai_priv[MT8192_DAI_ADDA_CH34];

	return 0;
}
