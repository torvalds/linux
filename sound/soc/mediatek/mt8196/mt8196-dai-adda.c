// SPDX-License-Identifier: GPL-2.0
/*
 *  MediaTek ALSA SoC Audio DAI ADDA Control
 *
 *  Copyright (c) 2025 MediaTek Inc.
 *  Author: Darren Ye <darren.ye@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/regmap.h>

#include "mt8196-afe-clk.h"
#include "mt8196-afe-common.h"
#include "mt8196-interconnection.h"

enum {
	UL_IIR_SW,
	UL_IIR_5HZ,
	UL_IIR_10HZ,
	UL_IIR_25HZ,
	UL_IIR_50HZ,
	UL_IIR_75HZ,
};

enum {
	MTK_AFE_ADDA_UL_RATE_8K,
	MTK_AFE_ADDA_UL_RATE_16K,
	MTK_AFE_ADDA_UL_RATE_32K,
	MTK_AFE_ADDA_UL_RATE_48K,
	MTK_AFE_ADDA_UL_RATE_96K,
	MTK_AFE_ADDA_UL_RATE_192K,
	MTK_AFE_ADDA_UL_RATE_48K_HD,
};

enum {
	MTK_AFE_MTKAIF_RATE_8K,
	MTK_AFE_MTKAIF_RATE_12K,
	MTK_AFE_MTKAIF_RATE_16K,
	MTK_AFE_MTKAIF_RATE_24K,
	MTK_AFE_MTKAIF_RATE_32K,
	MTK_AFE_MTKAIF_RATE_48K,
	MTK_AFE_MTKAIF_RATE_64K,
	MTK_AFE_MTKAIF_RATE_96K,
	MTK_AFE_MTKAIF_RATE_128K,
	MTK_AFE_MTKAIF_RATE_192K,
	MTK_AFE_MTKAIF_RATE_256K,
	MTK_AFE_MTKAIF_RATE_384K,
	MTK_AFE_MTKAIF_RATE_11K = 0x10,
	MTK_AFE_MTKAIF_RATE_22K,
	MTK_AFE_MTKAIF_RATE_44K,
	MTK_AFE_MTKAIF_RATE_88K,
	MTK_AFE_MTKAIF_RATE_176K,
	MTK_AFE_MTKAIF_RATE_352K,
};

struct mtk_afe_adda_priv {
	int dl_rate;
	int ul_rate;
};

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
		dev_warn(afe->dev, "rate %d invalid, use 48kHz!!!\n", rate);
		return MTK_AFE_ADDA_UL_RATE_48K;
	}
}

static unsigned int mtkaif_rate_transform(struct mtk_base_afe *afe,
					  unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_MTKAIF_RATE_8K;
	case 11025:
		return MTK_AFE_MTKAIF_RATE_11K;
	case 12000:
		return MTK_AFE_MTKAIF_RATE_12K;
	case 16000:
		return MTK_AFE_MTKAIF_RATE_16K;
	case 22050:
		return MTK_AFE_MTKAIF_RATE_22K;
	case 24000:
		return MTK_AFE_MTKAIF_RATE_24K;
	case 32000:
		return MTK_AFE_MTKAIF_RATE_32K;
	case 44100:
		return MTK_AFE_MTKAIF_RATE_44K;
	case 48000:
		return MTK_AFE_MTKAIF_RATE_48K;
	case 96000:
		return MTK_AFE_MTKAIF_RATE_96K;
	case 192000:
		return MTK_AFE_MTKAIF_RATE_192K;
	default:
		dev_warn(afe->dev, "rate %d invalid, use 48kHz!!!\n", rate);
		return MTK_AFE_MTKAIF_RATE_48K;
	}
}

enum {
	SUPPLY_SEQ_ADDA_AFE_ON,
	SUPPLY_SEQ_ADDA_FIFO,
	SUPPLY_SEQ_ADDA_AP_DMIC,
	SUPPLY_SEQ_ADDA_UL_ON,
};

static int mtk_adda_ul_src_set_dmic_phase_sync(struct mtk_base_afe *afe)
{
	dev_dbg(afe->dev, "set dmic phase sync\n");
	// ul0~1
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   UL0_PHASE_SYNC_HCLK_SET_MASK_SFT,
			   0x1 << UL0_PHASE_SYNC_HCLK_SET_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   UL0_PHASE_SYNC_FCLK_SET_MASK_SFT,
			   0x1 << UL0_PHASE_SYNC_FCLK_SET_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   UL1_PHASE_SYNC_HCLK_SET_MASK_SFT,
			   0x1 << UL1_PHASE_SYNC_HCLK_SET_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   UL1_PHASE_SYNC_FCLK_SET_MASK_SFT,
			   0x1 << UL1_PHASE_SYNC_FCLK_SET_SFT);
	// dmic 0
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   DMIC0_PHASE_SYNC_FCLK_SET_MASK_SFT,
			   0x1 << DMIC0_PHASE_SYNC_FCLK_SET_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   DMIC0_PHASE_SYNC_HCLK_SET_MASK_SFT,
			   0x1 << DMIC0_PHASE_SYNC_HCLK_SET_SFT);
	// dmic 1
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   DMIC1_PHASE_SYNC_FCLK_SET_MASK_SFT,
			   0x1 << DMIC1_PHASE_SYNC_FCLK_SET_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
			   DMIC1_PHASE_SYNC_HCLK_SET_MASK_SFT,
			   0x1 << DMIC1_PHASE_SYNC_HCLK_SET_SFT);
	// ul0~1 phase sync clock
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   DMIC1_PHASE_HCLK_SEL_MASK_SFT,
			   0x1 << DMIC1_PHASE_HCLK_SEL_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   DMIC1_PHASE_FCLK_SEL_MASK_SFT,
			   0x1 << DMIC1_PHASE_FCLK_SEL_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   DMIC0_PHASE_HCLK_SEL_MASK_SFT,
			   0x1 << DMIC0_PHASE_HCLK_SEL_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   DMIC0_PHASE_FCLK_SEL_MASK_SFT,
			   0x1 << DMIC0_PHASE_FCLK_SEL_SFT);
	// dmic 0
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL1_PHASE_HCLK_SEL_MASK_SFT,
			   0x2 << UL1_PHASE_HCLK_SEL_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL1_PHASE_FCLK_SEL_MASK_SFT,
			   0x2 << UL1_PHASE_FCLK_SEL_SFT);
	// dmic 1
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL0_PHASE_HCLK_SEL_MASK_SFT,
			   0x2 << UL0_PHASE_HCLK_SEL_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL0_PHASE_FCLK_SEL_MASK_SFT,
			   0x2 << UL0_PHASE_FCLK_SEL_SFT);

	return 0;
}

static int mtk_adda_ul_src_set_dmic_phase_sync_clock(struct mtk_base_afe *afe)
{
	dev_dbg(afe->dev, "dmic turn on phase sync clk\n");
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL_PHASE_SYNC_HCLK_1_ON_MASK_SFT,
			   0x1 << UL_PHASE_SYNC_HCLK_1_ON_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL_PHASE_SYNC_HCLK_0_ON_MASK_SFT,
			   0x1 << UL_PHASE_SYNC_HCLK_0_ON_SFT);

	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL_PHASE_SYNC_FCLK_1_ON_MASK_SFT,
			   0x1 << UL_PHASE_SYNC_FCLK_1_ON_SFT);
	regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON0,
			   UL_PHASE_SYNC_FCLK_0_ON_MASK_SFT,
			   0x1 << UL_PHASE_SYNC_FCLK_0_ON_SFT);

	return 0;
}

static int mtk_adda_ul_src_enable_dmic(struct mtk_base_afe *afe, int id)
{
	unsigned int reg_con0 = 0, reg_con1 = 0;

	dev_dbg(afe->dev, "id: %d\n", id);

	switch (id) {
	case MT8196_DAI_ADDA:
	case MT8196_DAI_AP_DMIC:
		reg_con0 = AFE_ADDA_UL0_SRC_CON0;
		reg_con1 = AFE_ADDA_UL0_SRC_CON1;
		break;
	case MT8196_DAI_ADDA_CH34:
	case MT8196_DAI_AP_DMIC_CH34:
		reg_con0 = AFE_ADDA_UL1_SRC_CON0;
		reg_con1 = AFE_ADDA_UL1_SRC_CON1;
		break;
	default:
		return -EINVAL;
	}

	switch (id) {
	case MT8196_DAI_AP_DMIC:
		dev_dbg(afe->dev, "clear mtkaifv4 ul ch1ch2 mux\n");
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_UL_CH1CH2_IN_EN_SEL_MASK_SFT,
				   0x0 << MTKAIFV4_UL_CH1CH2_IN_EN_SEL_SFT);
		break;
	case MT8196_DAI_AP_DMIC_CH34:
		dev_dbg(afe->dev, "clear mtkaifv4 ul ch3ch4 mux\n");
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_UL_CH3CH4_IN_EN_SEL_MASK_SFT,
				   0x0 << MTKAIFV4_UL_CH3CH4_IN_EN_SEL_SFT);
		break;
	default:
		return -EINVAL;
	}

	/* choose Phase */
	regmap_update_bits(afe->regmap, reg_con0,
			   UL_DMIC_PHASE_SEL_CH1_MASK_SFT,
			   0x0 << UL_DMIC_PHASE_SEL_CH1_SFT);
	regmap_update_bits(afe->regmap, reg_con0,
			   UL_DMIC_PHASE_SEL_CH2_MASK_SFT,
			   0x4 << UL_DMIC_PHASE_SEL_CH2_SFT);

	/* dmic mode, 3.25M*/
	regmap_update_bits(afe->regmap, reg_con0,
			   DIGMIC_3P25M_1P625M_SEL_CTL_MASK_SFT,
			   0x0);
	regmap_update_bits(afe->regmap, reg_con0,
			   DMIC_LOW_POWER_MODE_CTL_MASK_SFT,
			   0x0);

	/* turn on dmic, ch1, ch2 */
	regmap_update_bits(afe->regmap, reg_con0,
			   UL_SDM_3_LEVEL_CTL_MASK_SFT,
			   0x1 << UL_SDM_3_LEVEL_CTL_SFT);
	regmap_update_bits(afe->regmap, reg_con0,
			   UL_MODE_3P25M_CH1_CTL_MASK_SFT,
			   0x1 << UL_MODE_3P25M_CH1_CTL_SFT);
	regmap_update_bits(afe->regmap, reg_con0,
			   UL_MODE_3P25M_CH2_CTL_MASK_SFT,
			   0x1 << UL_MODE_3P25M_CH2_CTL_SFT);

	/* ul gain:  gain = 0x7fff/positive_gain = 0x0/gain_mode = 0x10 */
	regmap_update_bits(afe->regmap, reg_con1,
			   ADDA_UL_GAIN_VALUE_MASK_SFT,
			   0x7fff << ADDA_UL_GAIN_VALUE_SFT);
	regmap_update_bits(afe->regmap, reg_con1,
			   ADDA_UL_POSTIVEGAIN_MASK_SFT,
			   0x0 << ADDA_UL_POSTIVEGAIN_SFT);
	/* gain_mode = 0x10: Add 0.5 gain at CIC output */
	regmap_update_bits(afe->regmap, reg_con1,
			   GAIN_MODE_MASK_SFT,
			   0x02 << GAIN_MODE_SFT);
	return 0;
}

static int mtk_adda_sleep_on_pmd_event(struct snd_soc_dapm_widget *w,
				       struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(afe->dev, "name %s, event 0x%x\n", w->name, event);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		break;
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(120, 130);
		break;
	default:
		break;
	}

	return 0;
}

/* ADDA UL MUX */
#define ADDA_UL_MUX_MASK 0x3
enum {
	ADDA_UL_MUX_MTKAIF = 0,
	ADDA_UL_MUX_AP_DMIC,
	ADDA_UL_MUX_AP_DMIC_MULTICH,
};

static const char *const adda_ul_mux_map[] = {
	"MTKAIF", "AP_DMIC", "AP_DMIC_MULTI_CH",
};

static int adda_ul_map_value[] = {
	ADDA_UL_MUX_MTKAIF,
	ADDA_UL_MUX_AP_DMIC,
	ADDA_UL_MUX_AP_DMIC_MULTICH,
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
	SND_SOC_DAPM_SUPPLY_S("ADDA Enable", SUPPLY_SEQ_ADDA_AFE_ON,
			      AUDIO_ENGEN_CON0, AUDIO_F3P25M_EN_ON_SFT, 0,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADDA Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA_UL0_SRC_CON0,
			      UL_SRC_ON_TMP_CTL_SFT, 0,
			      mtk_adda_sleep_on_pmd_event,
			      SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("ADDA CH34 Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA_UL1_SRC_CON0,
			      UL_SRC_ON_TMP_CTL_SFT, 0,
			      mtk_adda_sleep_on_pmd_event,
			      SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("AP_DMIC_EN", SUPPLY_SEQ_ADDA_AP_DMIC,
			      AFE_ADDA_UL0_SRC_CON0,
			      UL_AP_DMIC_ON_SFT, 0,
			      mtk_adda_sleep_on_pmd_event,
			      SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("AP_DMIC_CH34_EN", SUPPLY_SEQ_ADDA_AP_DMIC,
			      AFE_ADDA_UL1_SRC_CON0,
			      UL_AP_DMIC_ON_SFT, 0,
			      mtk_adda_sleep_on_pmd_event,
			      SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SUPPLY_S("ADDA_FIFO", SUPPLY_SEQ_ADDA_FIFO,
			      AFE_ADDA_UL0_SRC_CON1,
			      FIFO_SOFT_RST_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("ADDA_CH34_FIFO", SUPPLY_SEQ_ADDA_FIFO,
			      AFE_ADDA_UL1_SRC_CON1,
			      FIFO_SOFT_RST_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_MUX("ADDA_UL_Mux", SND_SOC_NOPM, 0, 0,
			 &adda_ul_mux_control),
	SND_SOC_DAPM_MUX("ADDA_CH34_UL_Mux", SND_SOC_NOPM, 0, 0,
			 &adda_ch34_ul_mux_control),
	SND_SOC_DAPM_INPUT("AP_DMIC_INPUT"),
};

static const struct snd_soc_dapm_route mtk_dai_adda_routes[] = {
	/* capture */
	{"ADDA_UL_Mux", "MTKAIF", "ADDA Capture"},
	{"ADDA_UL_Mux", "AP_DMIC", "AP DMIC Capture"},
	{"ADDA_UL_Mux", "AP_DMIC_MULTI_CH", "AP DMIC MULTICH Capture"},

	{"ADDA_CH34_UL_Mux", "MTKAIF", "ADDA CH34 Capture"},
	{"ADDA_CH34_UL_Mux", "AP_DMIC", "AP DMIC CH34 Capture"},
	{"ADDA_CH34_UL_Mux", "AP_DMIC_MULTI_CH", "AP DMIC MULTICH Capture"},

	{"AP DMIC Capture", NULL, "ADDA Enable"},
	{"AP DMIC Capture", NULL, "ADDA Capture Enable"},
	{"AP DMIC Capture", NULL, "ADDA_FIFO"},
	{"AP DMIC Capture", NULL, "AP_DMIC_EN"},

	{"AP DMIC CH34 Capture", NULL, "ADDA Enable"},
	{"AP DMIC CH34 Capture", NULL, "ADDA CH34 Capture Enable"},
	{"AP DMIC CH34 Capture", NULL, "ADDA_CH34_FIFO"},
	{"AP DMIC CH34 Capture", NULL, "AP_DMIC_CH34_EN"},

	{"AP DMIC MULTICH Capture", NULL, "ADDA Enable"},
	{"AP DMIC MULTICH Capture", NULL, "ADDA Capture Enable"},
	{"AP DMIC MULTICH Capture", NULL, "ADDA CH34 Capture Enable"},
	{"AP DMIC MULTICH Capture", NULL, "ADDA_FIFO"},
	{"AP DMIC MULTICH Capture", NULL, "ADDA_CH34_FIFO"},
	{"AP DMIC MULTICH Capture", NULL, "AP_DMIC_EN"},
	{"AP DMIC MULTICH Capture", NULL, "AP_DMIC_CH34_EN"},

	{"AP DMIC Capture", NULL, "AP_DMIC_INPUT"},
	{"AP DMIC CH34 Capture", NULL, "AP_DMIC_INPUT"},
	{"AP DMIC MULTICH Capture", NULL, "AP_DMIC_INPUT"},
};

/* dai ops */
static int set_playback_hw_params(struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	unsigned int rate = params_rate(params);
	struct mtk_afe_adda_priv *adda_priv;
	unsigned int mtkaif_rate = 0;
	int id = dai->id;

	adda_priv = afe_priv->dai_priv[id];
	if (!adda_priv)
		return -EINVAL;

	adda_priv->dl_rate = rate;

	/* get mtkaif dl rate */
	mtkaif_rate = mtkaif_rate_transform(afe, adda_priv->dl_rate);

	if (id == MT8196_DAI_ADDA) {
		/* MTKAIF sample rate config */
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_TX_CFG0,
				   MTKAIFV4_TXIF_INPUT_MODE_MASK_SFT,
				   mtkaif_rate << MTKAIFV4_TXIF_INPUT_MODE_SFT);
		/* AFE_ADDA_MTKAIFV4_TX_CFG0 */
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_TX_CFG0,
				   MTKAIFV4_TXIF_FOUR_CHANNEL_MASK_SFT,
				   0x0 << MTKAIFV4_TXIF_FOUR_CHANNEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_TX_CFG0,
				   MTKAIFV4_ADDA_OUT_EN_SEL_MASK_SFT,
				   0x1 << MTKAIFV4_ADDA_OUT_EN_SEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_TX_CFG0,
				   MTKAIFV4_ADDA6_OUT_EN_SEL_MASK_SFT,
				   0x1 << MTKAIFV4_ADDA6_OUT_EN_SEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_TX_CFG0,
				   MTKAIFV4_TXIF_V4_MASK_SFT,
				   0x1 << MTKAIFV4_TXIF_V4_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_TX_CFG0,
				   MTKAIFV4_TXIF_EN_SEL_MASK_SFT,
				   0x0 << MTKAIFV4_TXIF_EN_SEL_SFT);
		/* clean predistortion */
	} else {
		/* MTKAIF sample rate config */
		regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIFV4_TX_CFG0,
				   ADDA6_MTKAIFV4_TXIF_INPUT_MODE_MASK_SFT,
				   mtkaif_rate << ADDA6_MTKAIFV4_TXIF_INPUT_MODE_SFT);
		/* AFE_ADDA6_MTKAIFV4_TX_CFG0 */
		regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIFV4_TX_CFG0,
				   ADDA6_MTKAIFV4_TXIF_FOUR_CHANNEL_MASK_SFT,
				   0x0 << ADDA6_MTKAIFV4_TXIF_FOUR_CHANNEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIFV4_TX_CFG0,
				   ADDA6_MTKAIFV4_TXIF_EN_SEL_MASK_SFT,
				   0x1 << ADDA6_MTKAIFV4_TXIF_EN_SEL_SFT);
	}

	return 0;
};

static int set_capture_hw_params(struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	unsigned int rate = params_rate(params);
	struct mtk_afe_adda_priv *adda_priv;
	unsigned int voice_mode = 0;
	unsigned int ul_src_con0 = 0;
	unsigned int mtkaif_rate = 0;
	int id = dai->id;

	adda_priv = afe_priv->dai_priv[id];
	if (!adda_priv)
		return -EINVAL;

	adda_priv->ul_rate = rate;

	/* get mtkaif dl rate */
	mtkaif_rate = mtkaif_rate_transform(afe, adda_priv->ul_rate);

	voice_mode = adda_ul_rate_transform(afe, rate);

	ul_src_con0 |= (voice_mode << 17) & (0x7 << 17);

	/* enable iir */
	ul_src_con0 |= (1 << UL_IIR_ON_TMP_CTL_SFT) &
			UL_IIR_ON_TMP_CTL_MASK_SFT;
	ul_src_con0 |= (UL_IIR_SW << UL_IIRMODE_CTL_SFT) &
			UL_IIRMODE_CTL_MASK_SFT;

	regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
			   MTKAIFV4_RXIF_INPUT_MODE_MASK_SFT,
			   mtkaif_rate << MTKAIFV4_RXIF_INPUT_MODE_SFT);

	regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIFV4_RX_CFG0,
			   ADDA6_MTKAIFV4_RXIF_INPUT_MODE_MASK_SFT,
			   mtkaif_rate << ADDA6_MTKAIFV4_RXIF_INPUT_MODE_SFT);

	switch (id) {
	case MT8196_DAI_ADDA:
	case MT8196_DAI_AP_DMIC:
	case MT8196_DAI_AP_DMIC_MULTICH:
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_RXIF_INPUT_MODE_MASK_SFT,
				   mtkaif_rate << MTKAIFV4_RXIF_INPUT_MODE_SFT);
		/* AFE_ADDA_MTKAIFV4_RX_CFG0 */
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_RXIF_FOUR_CHANNEL_MASK_SFT,
				   0x1 << MTKAIFV4_RXIF_FOUR_CHANNEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_RXIF_EN_SEL_MASK_SFT,
				   0x0 << MTKAIFV4_RXIF_EN_SEL_SFT);
		/* [28] loopback mode
		 * 0: loopback adda tx to adda rx
		 * 1: loopback adda6 tx to adda rx
		 */
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_TXIF_EN_SEL_MASK_SFT,
				   0x0 << MTKAIFV4_TXIF_EN_SEL_SFT);

		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_UL_CH1CH2_IN_EN_SEL_MASK_SFT,
				   0x1 << MTKAIFV4_UL_CH1CH2_IN_EN_SEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_UL_CH3CH4_IN_EN_SEL_MASK_SFT,
				   0x1 << MTKAIFV4_UL_CH3CH4_IN_EN_SEL_SFT);

		/* 35Hz @ 48k */
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_02_01, 0x00000000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_04_03, 0x00003FB8);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_06_05, 0x3FB80000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_08_07, 0x3FB80000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_10_09, 0x0000C048);

		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_SRC_CON0, ul_src_con0);

		/* mtkaif_rxif_data_mode = 0, amic */
		regmap_update_bits(afe->regmap,
				   AFE_MTKAIF1_RX_CFG0,
				   0x1 << 0,
				   0x0 << 0);

		/* 35Hz @ 48k */
		regmap_write(afe->regmap,
			     AFE_ADDA_UL0_IIR_COEF_02_01, 0x00000000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL0_IIR_COEF_04_03, 0x00003FB8);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL0_IIR_COEF_06_05, 0x3FB80000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL0_IIR_COEF_08_07, 0x3FB80000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL0_IIR_COEF_10_09, 0x0000C048);

		regmap_write(afe->regmap,
			     AFE_ADDA_UL0_SRC_CON0, ul_src_con0);

		/* mtkaif_rxif_data_mode = 0, amic */
		regmap_update_bits(afe->regmap,
				   AFE_MTKAIF0_RX_CFG0,
				   0x1 << 0,
				   0x0 << 0);
		break;
	case MT8196_DAI_ADDA_CH34:
	case MT8196_DAI_AP_DMIC_CH34:
		/* AFE_ADDA_MTKAIFV4_RX_CFG0 */
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_RXIF_FOUR_CHANNEL_MASK_SFT,
				   0x1 << MTKAIFV4_RXIF_FOUR_CHANNEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_RXIF_EN_SEL_MASK_SFT,
				   0x0 << MTKAIFV4_RXIF_EN_SEL_SFT);

		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_UL_CH1CH2_IN_EN_SEL_MASK_SFT,
				   0x1 << MTKAIFV4_UL_CH1CH2_IN_EN_SEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_UL_CH3CH4_IN_EN_SEL_MASK_SFT,
				   0x1 << MTKAIFV4_UL_CH3CH4_IN_EN_SEL_SFT);

		/* 35Hz @ 48k */
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_02_01, 0x00000000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_04_03, 0x00003FB8);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_06_05, 0x3FB80000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_08_07, 0x3FB80000);
		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_IIR_COEF_10_09, 0x0000C048);

		regmap_write(afe->regmap,
			     AFE_ADDA_UL1_SRC_CON0, ul_src_con0);

		/* mtkaif_rxif_data_mode = 0, amic */
		regmap_update_bits(afe->regmap,
				   AFE_MTKAIF1_RX_CFG0,
				   0x1 << 0,
				   0x0 << 0);
		break;
	case MT8196_DAI_ADDA_CH56:
		regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIFV4_RX_CFG0,
				   ADDA6_MTKAIFV4_RXIF_INPUT_MODE_MASK_SFT,
				   mtkaif_rate << ADDA6_MTKAIFV4_RXIF_INPUT_MODE_SFT);
		/* AFE_ADDA6_MTKAIFV4_RX_CFG0 */
		regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIFV4_RX_CFG0,
				   ADDA6_MTKAIFV4_RXIF_FOUR_CHANNEL_MASK_SFT,
				   0x1 << ADDA6_MTKAIFV4_RXIF_FOUR_CHANNEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA_MTKAIFV4_RX_CFG0,
				   MTKAIFV4_UL_CH5CH6_IN_EN_SEL_MASK_SFT,
				   0x1 << MTKAIFV4_UL_CH5CH6_IN_EN_SEL_SFT);
		regmap_update_bits(afe->regmap, AFE_ADDA6_MTKAIFV4_RX_CFG0,
				   ADDA6_MTKAIFV4_RXIF_EN_SEL_MASK_SFT,
				   0x1 << ADDA6_MTKAIFV4_RXIF_EN_SEL_SFT);
		break;
	default:
		break;
	}

	/* ap dmic */
	switch (id) {
	case MT8196_DAI_AP_DMIC:
	case MT8196_DAI_AP_DMIC_CH34:
		mtk_adda_ul_src_enable_dmic(afe, id);
		break;
	case MT8196_DAI_AP_DMIC_MULTICH:
		regmap_update_bits(afe->regmap, AFE_ADDA_ULSRC_PHASE_CON1,
				   DMIC_CLK_PHASE_SYNC_SET_MASK_SFT,
				   0x1 << DMIC_CLK_PHASE_SYNC_SET_SFT);
		mtk_adda_ul_src_set_dmic_phase_sync(afe);
		mtk_adda_ul_src_enable_dmic(afe, MT8196_DAI_AP_DMIC);
		mtk_adda_ul_src_enable_dmic(afe, MT8196_DAI_AP_DMIC_CH34);
		mtk_adda_ul_src_set_dmic_phase_sync_clock(afe);
		break;
	default:
		break;
	}

	return 0;
};

static int mtk_dai_adda_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int id = dai->id;

	if (id >= MT8196_DAI_NUM || id < 0)
		return -EINVAL;

	dev_dbg(afe->dev, "id %d, stream %d, rate %d\n",
		id, substream->stream, params_rate(params));

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return set_playback_hw_params(params, dai);
	else
		return set_capture_hw_params(params, dai);

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
		.id = MT8196_DAI_ADDA,
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
		.id = MT8196_DAI_ADDA_CH34,
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
		.name = "ADDA_CH56",
		.id = MT8196_DAI_ADDA_CH56,
		.capture = {
			.stream_name = "ADDA CH56 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
	{
		.name = "AP_DMIC",
		.id = MT8196_DAI_AP_DMIC,
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
		.id = MT8196_DAI_AP_DMIC_CH34,
		.capture = {
			.stream_name = "AP DMIC CH34 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
	/*
	 * Multich DMIC combines two DMIC controllers for use together,
	 * so AP_DMIC and Multich DMIC cannot be used at the same time.
	 */
	{
		.name = "AP_DMIC_MULTICH",
		.id = MT8196_DAI_AP_DMIC_MULTICH,
		.capture = {
			.stream_name = "AP DMIC MULTICH Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
};

static int init_adda_priv_data(struct mtk_base_afe *afe)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_adda_priv *adda_priv;
	static const int adda_dai_list[] = {
		MT8196_DAI_ADDA,
		MT8196_DAI_ADDA_CH34,
		MT8196_DAI_ADDA_CH56,
		MT8196_DAI_AP_DMIC_MULTICH
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(adda_dai_list); i++) {
		adda_priv = devm_kzalloc(afe->dev,
					 sizeof(struct mtk_afe_adda_priv),
					 GFP_KERNEL);
		if (!adda_priv)
			return -ENOMEM;

		afe_priv->dai_priv[adda_dai_list[i]] = adda_priv;
	}

	/* ap dmic priv share with adda */
	afe_priv->dai_priv[MT8196_DAI_AP_DMIC] =
		afe_priv->dai_priv[MT8196_DAI_ADDA];
	afe_priv->dai_priv[MT8196_DAI_AP_DMIC_CH34] =
		afe_priv->dai_priv[MT8196_DAI_ADDA_CH34];

	return 0;
}

int mt8196_dai_adda_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;
	int ret;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	dai->dai_drivers = mtk_dai_adda_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_adda_driver);
	dai->dapm_widgets = mtk_dai_adda_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_adda_widgets);
	dai->dapm_routes = mtk_dai_adda_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_adda_routes);

	ret = init_adda_priv_data(afe);
	if (ret)
		return ret;

	list_add(&dai->list, &afe->sub_dais);

	return 0;
}

