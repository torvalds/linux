// SPDX-License-Identifier: GPL-2.0
//
//  MediaTek ALSA SoC Audio DAI SRC Control
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/regmap.h>
#include "mt8186-afe-common.h"
#include "mt8186-interconnection.h"

struct mtk_afe_src_priv {
	int dl_rate;
	int ul_rate;
};

static const unsigned int src_iir_coeff_32_to_16[] = {
	0x0dbae6, 0xff9b0a, 0x0dbae6, 0x05e488, 0xe072b9, 0x000002,
	0x0dbae6, 0x000f3b, 0x0dbae6, 0x06a537, 0xe17d79, 0x000002,
	0x0dbae6, 0x01246a, 0x0dbae6, 0x087261, 0xe306be, 0x000002,
	0x0dbae6, 0x03437d, 0x0dbae6, 0x0bc16f, 0xe57c87, 0x000002,
	0x0dbae6, 0x072981, 0x0dbae6, 0x111dd3, 0xe94f2a, 0x000002,
	0x0dbae6, 0x0dc4a6, 0x0dbae6, 0x188611, 0xee85a0, 0x000002,
	0x0dbae6, 0x168b9a, 0x0dbae6, 0x200e8f, 0xf3ccf1, 0x000002,
	0x000000, 0x1b75cb, 0x1b75cb, 0x2374a2, 0x000000, 0x000001
};

static const unsigned int src_iir_coeff_44_to_16[] = {
	0x09ae28, 0xf7d97d, 0x09ae28, 0x212a3d, 0xe0ac3a, 0x000002,
	0x09ae28, 0xf8525a, 0x09ae28, 0x216d72, 0xe234be, 0x000002,
	0x09ae28, 0xf980f5, 0x09ae28, 0x22a057, 0xe45a81, 0x000002,
	0x09ae28, 0xfc0a08, 0x09ae28, 0x24d3bd, 0xe7752d, 0x000002,
	0x09ae28, 0x016162, 0x09ae28, 0x27da01, 0xeb6ea8, 0x000002,
	0x09ae28, 0x0b67df, 0x09ae28, 0x2aca4a, 0xef34c4, 0x000002,
	0x000000, 0x135c50, 0x135c50, 0x2c1079, 0x000000, 0x000001
};

static const unsigned int src_iir_coeff_44_to_32[] = {
	0x096966, 0x0c4d35, 0x096966, 0xedee81, 0xf05070, 0x000003,
	0x12d2cc, 0x193910, 0x12d2cc, 0xddbf4f, 0xe21e1d, 0x000002,
	0x12d2cc, 0x1a9e60, 0x12d2cc, 0xe18916, 0xe470fd, 0x000002,
	0x12d2cc, 0x1d06e0, 0x12d2cc, 0xe8a4a6, 0xe87b24, 0x000002,
	0x12d2cc, 0x207578, 0x12d2cc, 0xf4fe62, 0xef5917, 0x000002,
	0x12d2cc, 0x24055f, 0x12d2cc, 0x05ee2b, 0xf8b502, 0x000002,
	0x000000, 0x25a599, 0x25a599, 0x0fabe2, 0x000000, 0x000001
};

static const unsigned int src_iir_coeff_48_to_16[] = {
	0x0296a4, 0xfd69dd, 0x0296a4, 0x209439, 0xe01ff9, 0x000002,
	0x0f4ff3, 0xf0d6d4, 0x0f4ff3, 0x209bc9, 0xe076c3, 0x000002,
	0x0e8490, 0xf1fe63, 0x0e8490, 0x20cfd6, 0xe12124, 0x000002,
	0x14852f, 0xed794a, 0x14852f, 0x21503d, 0xe28b32, 0x000002,
	0x136222, 0xf17677, 0x136222, 0x225be1, 0xe56964, 0x000002,
	0x0a8d85, 0xfc4a97, 0x0a8d85, 0x24310c, 0xea6952, 0x000002,
	0x05eff5, 0x043455, 0x05eff5, 0x4ced8f, 0xe134d6, 0x000001,
	0x000000, 0x3aebe6, 0x3aebe6, 0x04f3b0, 0x000000, 0x000004
};

static const unsigned int src_iir_coeff_48_to_32[] = {
	0x10c1b8, 0x10a7df, 0x10c1b8, 0xe7514e, 0xe0b41f, 0x000002,
	0x10c1b8, 0x116257, 0x10c1b8, 0xe9402f, 0xe25aaa, 0x000002,
	0x10c1b8, 0x130c89, 0x10c1b8, 0xed3cc3, 0xe4dddb, 0x000002,
	0x10c1b8, 0x1600dd, 0x10c1b8, 0xf48000, 0xe90c55, 0x000002,
	0x10c1b8, 0x1a672e, 0x10c1b8, 0x00494c, 0xefa807, 0x000002,
	0x10c1b8, 0x1f38e6, 0x10c1b8, 0x0ee076, 0xf7c5f3, 0x000002,
	0x000000, 0x218370, 0x218370, 0x168b40, 0x000000, 0x000001
};

static const unsigned int src_iir_coeff_48_to_44[] = {
	0x0bf71c, 0x170f3f, 0x0bf71c, 0xe3a4c8, 0xf096cb, 0x000003,
	0x0bf71c, 0x17395e, 0x0bf71c, 0xe58085, 0xf210c8, 0x000003,
	0x0bf71c, 0x1782bd, 0x0bf71c, 0xe95ef6, 0xf4c899, 0x000003,
	0x0bf71c, 0x17cd97, 0x0bf71c, 0xf1608a, 0xfa3b18, 0x000003,
	0x000000, 0x2fdc6f, 0x2fdc6f, 0xf15663, 0x000000, 0x000001
};

static const unsigned int src_iir_coeff_96_to_16[] = {
	0x0805a1, 0xf21ae3, 0x0805a1, 0x3840bb, 0xe02a2e, 0x000002,
	0x0d5dd8, 0xe8f259, 0x0d5dd8, 0x1c0af6, 0xf04700, 0x000003,
	0x0bb422, 0xec08d9, 0x0bb422, 0x1bfccc, 0xf09216, 0x000003,
	0x08fde6, 0xf108be, 0x08fde6, 0x1bf096, 0xf10ae0, 0x000003,
	0x0ae311, 0xeeeda3, 0x0ae311, 0x37c646, 0xe385f5, 0x000002,
	0x044089, 0xfa7242, 0x044089, 0x37a785, 0xe56526, 0x000002,
	0x00c75c, 0xffb947, 0x00c75c, 0x378ba3, 0xe72c5f, 0x000002,
	0x000000, 0x0ef76e, 0x0ef76e, 0x377fda, 0x000000, 0x000001,
};

static const unsigned int src_iir_coeff_96_to_44[] = {
	0x08b543, 0xfd80f4, 0x08b543, 0x0e2332, 0xe06ed0, 0x000002,
	0x1b6038, 0xf90e7e, 0x1b6038, 0x0ec1ac, 0xe16f66, 0x000002,
	0x188478, 0xfbb921, 0x188478, 0x105859, 0xe2e596, 0x000002,
	0x13eff3, 0xffa707, 0x13eff3, 0x13455c, 0xe533b7, 0x000002,
	0x0dc239, 0x03d458, 0x0dc239, 0x17f120, 0xe8b617, 0x000002,
	0x0745f1, 0x05d790, 0x0745f1, 0x1e3d75, 0xed5f18, 0x000002,
	0x05641f, 0x085e2b, 0x05641f, 0x48efd0, 0xe3e9c8, 0x000001,
	0x000000, 0x28f632, 0x28f632, 0x273905, 0x000000, 0x000001,
};

static unsigned int mtk_get_src_freq_mode(struct mtk_base_afe *afe, int rate)
{
	switch (rate) {
	case 8000:
		return 0x50000;
	case 11025:
		return 0x6e400;
	case 12000:
		return 0x78000;
	case 16000:
		return 0xa0000;
	case 22050:
		return 0xdc800;
	case 24000:
		return 0xf0000;
	case 32000:
		return 0x140000;
	case 44100:
		return 0x1b9000;
	case 48000:
		return 0x1e0000;
	case 88200:
		return 0x372000;
	case 96000:
		return 0x3c0000;
	case 176400:
		return 0x6e4000;
	case 192000:
		return 0x780000;
	default:
		dev_err(afe->dev, "%s(), rate %d invalid!!!\n",
			__func__, rate);
		return 0;
	}
}

static const unsigned int *get_iir_coeff(unsigned int rate_in,
					 unsigned int rate_out,
					 unsigned int *param_num)
{
	if (rate_in == 32000 && rate_out == 16000) {
		*param_num = ARRAY_SIZE(src_iir_coeff_32_to_16);
		return src_iir_coeff_32_to_16;
	} else if (rate_in == 44100 && rate_out == 16000) {
		*param_num = ARRAY_SIZE(src_iir_coeff_44_to_16);
		return src_iir_coeff_44_to_16;
	} else if (rate_in == 44100 && rate_out == 32000) {
		*param_num = ARRAY_SIZE(src_iir_coeff_44_to_32);
		return src_iir_coeff_44_to_32;
	} else if ((rate_in == 48000 && rate_out == 16000) ||
		   (rate_in == 96000 && rate_out == 32000)) {
		*param_num = ARRAY_SIZE(src_iir_coeff_48_to_16);
		return src_iir_coeff_48_to_16;
	} else if (rate_in == 48000 && rate_out == 32000) {
		*param_num = ARRAY_SIZE(src_iir_coeff_48_to_32);
		return src_iir_coeff_48_to_32;
	} else if (rate_in == 48000 && rate_out == 44100) {
		*param_num = ARRAY_SIZE(src_iir_coeff_48_to_44);
		return src_iir_coeff_48_to_44;
	} else if (rate_in == 96000 && rate_out == 16000) {
		*param_num = ARRAY_SIZE(src_iir_coeff_96_to_16);
		return src_iir_coeff_96_to_16;
	} else if ((rate_in == 96000 && rate_out == 44100) ||
		   (rate_in == 48000 && rate_out == 22050)) {
		*param_num = ARRAY_SIZE(src_iir_coeff_96_to_44);
		return src_iir_coeff_96_to_44;
	}

	*param_num = 0;
	return NULL;
}

static int mtk_set_src_1_param(struct mtk_base_afe *afe, int id)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_src_priv *src_priv = afe_priv->dai_priv[id];
	unsigned int iir_coeff_num;
	unsigned int iir_stage;
	int rate_in = src_priv->dl_rate;
	int rate_out = src_priv->ul_rate;
	unsigned int out_freq_mode = mtk_get_src_freq_mode(afe, rate_out);
	unsigned int in_freq_mode = mtk_get_src_freq_mode(afe, rate_in);

	/* set out freq mode */
	regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON3,
			   G_SRC_ASM_FREQ_4_MASK_SFT,
			   out_freq_mode << G_SRC_ASM_FREQ_4_SFT);

	/* set in freq mode */
	regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON4,
			   G_SRC_ASM_FREQ_5_MASK_SFT,
			   in_freq_mode << G_SRC_ASM_FREQ_5_SFT);

	regmap_write(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON5, 0x3f5986);
	regmap_write(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON5, 0x3f5987);
	regmap_write(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON6, 0x1fbd);
	regmap_write(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON2, 0);

	/* set iir if in_rate > out_rate */
	if (rate_in > rate_out) {
		int i;
		const unsigned int *iir_coeff = get_iir_coeff(rate_in, rate_out,
							      &iir_coeff_num);

		if (iir_coeff_num == 0 || !iir_coeff) {
			dev_err(afe->dev, "%s(), iir coeff error, num %d, coeff %p\n",
				__func__, iir_coeff_num, iir_coeff);
			return -EINVAL;
		}

		/* COEFF_SRAM_CTRL */
		regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON0,
				   G_SRC_COEFF_SRAM_CTRL_MASK_SFT,
				   BIT(G_SRC_COEFF_SRAM_CTRL_SFT));
		/* Clear coeff history to r/w coeff from the first position */
		regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON13,
				   G_SRC_COEFF_SRAM_ADR_MASK_SFT, 0);
		/* Write SRC coeff, should not read the reg during write */
		for (i = 0; i < iir_coeff_num; i++)
			regmap_write(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON12,
				     iir_coeff[i]);
		/* disable sram access */
		regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON0,
				   G_SRC_COEFF_SRAM_CTRL_MASK_SFT, 0);
		/* CHSET_IIR_STAGE */
		iir_stage = (iir_coeff_num / 6) - 1;
		regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON2,
				   G_SRC_CHSET_IIR_STAGE_MASK_SFT,
				   iir_stage << G_SRC_CHSET_IIR_STAGE_SFT);
		/* CHSET_IIR_EN */
		regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON2,
				   G_SRC_CHSET_IIR_EN_MASK_SFT,
				   BIT(G_SRC_CHSET_IIR_EN_SFT));
	} else {
		/* CHSET_IIR_EN off */
		regmap_update_bits(afe->regmap, AFE_GENERAL1_ASRC_2CH_CON2,
				   G_SRC_CHSET_IIR_EN_MASK_SFT, 0);
	}

	return 0;
}

static int mtk_set_src_2_param(struct mtk_base_afe *afe, int id)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_src_priv *src_priv = afe_priv->dai_priv[id];
	unsigned int iir_coeff_num;
	unsigned int iir_stage;
	int rate_in = src_priv->dl_rate;
	int rate_out = src_priv->ul_rate;
	unsigned int out_freq_mode = mtk_get_src_freq_mode(afe, rate_out);
	unsigned int in_freq_mode = mtk_get_src_freq_mode(afe, rate_in);

	/* set out freq mode */
	regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON3,
			   G_SRC_ASM_FREQ_4_MASK_SFT,
			   out_freq_mode << G_SRC_ASM_FREQ_4_SFT);

	/* set in freq mode */
	regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON4,
			   G_SRC_ASM_FREQ_5_MASK_SFT,
			   in_freq_mode << G_SRC_ASM_FREQ_5_SFT);

	regmap_write(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON5, 0x3f5986);
	regmap_write(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON5, 0x3f5987);
	regmap_write(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON6, 0x1fbd);
	regmap_write(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON2, 0);

	/* set iir if in_rate > out_rate */
	if (rate_in > rate_out) {
		int i;
		const unsigned int *iir_coeff = get_iir_coeff(rate_in, rate_out,
							      &iir_coeff_num);

		if (iir_coeff_num == 0 || !iir_coeff) {
			dev_err(afe->dev, "%s(), iir coeff error, num %d, coeff %p\n",
				 __func__, iir_coeff_num, iir_coeff);
			return -EINVAL;
		}

		/* COEFF_SRAM_CTRL */
		regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON0,
				   G_SRC_COEFF_SRAM_CTRL_MASK_SFT,
				   BIT(G_SRC_COEFF_SRAM_CTRL_SFT));
		/* Clear coeff history to r/w coeff from the first position */
		regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON13,
				   G_SRC_COEFF_SRAM_ADR_MASK_SFT, 0);
		/* Write SRC coeff, should not read the reg during write */
		for (i = 0; i < iir_coeff_num; i++)
			regmap_write(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON12,
				     iir_coeff[i]);
		/* disable sram access */
		regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON0,
				   G_SRC_COEFF_SRAM_CTRL_MASK_SFT, 0);
		/* CHSET_IIR_STAGE */
		iir_stage = (iir_coeff_num / 6) - 1;
		regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON2,
				   G_SRC_CHSET_IIR_STAGE_MASK_SFT,
				   iir_stage << G_SRC_CHSET_IIR_STAGE_SFT);
		/* CHSET_IIR_EN */
		regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON2,
				   G_SRC_CHSET_IIR_EN_MASK_SFT,
				   BIT(G_SRC_CHSET_IIR_EN_SFT));
	} else {
		/* CHSET_IIR_EN off */
		regmap_update_bits(afe->regmap, AFE_GENERAL2_ASRC_2CH_CON2,
				   G_SRC_CHSET_IIR_EN_MASK_SFT, 0);
	}

	return 0;
}

#define HW_SRC_1_EN_W_NAME "HW_SRC_1_Enable"
#define HW_SRC_2_EN_W_NAME "HW_SRC_2_Enable"

static int mtk_hw_src_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int id;
	struct mtk_afe_src_priv *src_priv;
	unsigned int reg;

	if (snd_soc_dapm_widget_name_cmp(w, HW_SRC_1_EN_W_NAME) == 0)
		id = MT8186_DAI_SRC_1;
	else
		id = MT8186_DAI_SRC_2;

	src_priv = afe_priv->dai_priv[id];

	dev_dbg(afe->dev,
		"%s(), name %s, event 0x%x, id %d, src_priv %p, dl_rate %d, ul_rate %d\n",
		__func__, w->name, event, id, src_priv,
		src_priv->dl_rate, src_priv->ul_rate);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		if (id == MT8186_DAI_SRC_1)
			mtk_set_src_1_param(afe, id);
		else
			mtk_set_src_2_param(afe, id);
		break;
	case SND_SOC_DAPM_POST_PMU:
		reg = (id == MT8186_DAI_SRC_1) ?
		      AFE_GENERAL1_ASRC_2CH_CON0 : AFE_GENERAL2_ASRC_2CH_CON0;
		/* ASM_ON */
		regmap_update_bits(afe->regmap, reg,
				   G_SRC_ASM_ON_MASK_SFT,
				   BIT(G_SRC_ASM_ON_SFT));
		/* CHSET_ON */
		regmap_update_bits(afe->regmap, reg,
				   G_SRC_CHSET_ON_MASK_SFT,
				   BIT(G_SRC_CHSET_ON_SFT));
		/* CHSET_STR_CLR */
		regmap_update_bits(afe->regmap, reg,
				   G_SRC_CHSET_STR_CLR_MASK_SFT,
				   BIT(G_SRC_CHSET_STR_CLR_SFT));
		break;
	case SND_SOC_DAPM_PRE_PMD:
		reg = (id == MT8186_DAI_SRC_1) ?
		      AFE_GENERAL1_ASRC_2CH_CON0 : AFE_GENERAL2_ASRC_2CH_CON0;
		/* ASM_OFF */
		regmap_update_bits(afe->regmap, reg, G_SRC_ASM_ON_MASK_SFT, 0);
		/* CHSET_OFF */
		regmap_update_bits(afe->regmap, reg, G_SRC_CHSET_ON_MASK_SFT, 0);
		/* CHSET_STR_CLR */
		regmap_update_bits(afe->regmap, reg, G_SRC_CHSET_STR_CLR_MASK_SFT, 0);
		break;
	default:
		break;
	}

	return 0;
}

/* dai component */
static const struct snd_kcontrol_new mtk_hw_src_1_in_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1 Switch", AFE_CONN40,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1 Switch", AFE_CONN40,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1 Switch", AFE_CONN40,
				    I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1 Switch", AFE_CONN40_1,
				    I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1 Switch", AFE_CONN40_1,
				    I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH1 Switch", AFE_CONN40,
				    I_I2S0_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1 Switch", AFE_CONN40_1,
				    I_DL5_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_hw_src_1_in_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2 Switch", AFE_CONN41,
				    I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2 Switch", AFE_CONN41,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2 Switch", AFE_CONN41,
				    I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2 Switch", AFE_CONN41_1,
				    I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2 Switch", AFE_CONN41_1,
				    I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH2 Switch", AFE_CONN41,
				    I_I2S0_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2 Switch", AFE_CONN41_1,
				    I_DL5_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_hw_src_2_in_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1 Switch", AFE_CONN42,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1 Switch", AFE_CONN42,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1 Switch", AFE_CONN42,
				    I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1 Switch", AFE_CONN42,
				    I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1 Switch", AFE_CONN42_1,
				    I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1 Switch", AFE_CONN42_1,
				    I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN2_OUT_CH1 Switch", AFE_CONN42,
				    I_GAIN2_OUT_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_hw_src_2_in_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2 Switch", AFE_CONN43,
				    I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2 Switch", AFE_CONN43,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2 Switch", AFE_CONN43,
				    I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2 Switch", AFE_CONN43,
				    I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2 Switch", AFE_CONN43_1,
				    I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2 Switch", AFE_CONN43_1,
				    I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("HW_GAIN2_OUT_CH2 Switch", AFE_CONN43,
				    I_GAIN2_OUT_CH2, 1, 0),
};

static const struct snd_soc_dapm_widget mtk_dai_src_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("HW_SRC_1_IN_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_hw_src_1_in_ch1_mix,
			   ARRAY_SIZE(mtk_hw_src_1_in_ch1_mix)),
	SND_SOC_DAPM_MIXER("HW_SRC_1_IN_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_hw_src_1_in_ch2_mix,
			   ARRAY_SIZE(mtk_hw_src_1_in_ch2_mix)),
	SND_SOC_DAPM_MIXER("HW_SRC_2_IN_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_hw_src_2_in_ch1_mix,
			   ARRAY_SIZE(mtk_hw_src_2_in_ch1_mix)),
	SND_SOC_DAPM_MIXER("HW_SRC_2_IN_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_hw_src_2_in_ch2_mix,
			   ARRAY_SIZE(mtk_hw_src_2_in_ch2_mix)),

	SND_SOC_DAPM_SUPPLY(HW_SRC_1_EN_W_NAME,
			    GENERAL_ASRC_EN_ON, GENERAL1_ASRC_EN_ON_SFT, 0,
			    mtk_hw_src_event,
			    SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_SUPPLY(HW_SRC_2_EN_W_NAME,
			    GENERAL_ASRC_EN_ON, GENERAL2_ASRC_EN_ON_SFT, 0,
			    mtk_hw_src_event,
			    SND_SOC_DAPM_PRE_PMU |
			    SND_SOC_DAPM_POST_PMU |
			    SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_INPUT("HW SRC 1 Out Endpoint"),
	SND_SOC_DAPM_INPUT("HW SRC 2 Out Endpoint"),
	SND_SOC_DAPM_OUTPUT("HW SRC 1 In Endpoint"),
	SND_SOC_DAPM_OUTPUT("HW SRC 2 In Endpoint"),
};

static int mtk_afe_src_en_connect(struct snd_soc_dapm_widget *source,
				  struct snd_soc_dapm_widget *sink)
{
	struct snd_soc_dapm_widget *w = source;
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct mtk_afe_src_priv *src_priv;

	if (snd_soc_dapm_widget_name_cmp(w, HW_SRC_1_EN_W_NAME) == 0)
		src_priv = afe_priv->dai_priv[MT8186_DAI_SRC_1];
	else
		src_priv = afe_priv->dai_priv[MT8186_DAI_SRC_2];

	dev_dbg(afe->dev,
		"%s(), source %s, sink %s, dl_rate %d, ul_rate %d\n",
		__func__, source->name, sink->name,
		src_priv->dl_rate, src_priv->ul_rate);

	return (src_priv->dl_rate > 0 && src_priv->ul_rate > 0) ? 1 : 0;
}

static const struct snd_soc_dapm_route mtk_dai_src_routes[] = {
	{"HW_SRC_1_IN_CH1", "DL1_CH1 Switch", "DL1"},
	{"HW_SRC_1_IN_CH2", "DL1_CH2 Switch", "DL1"},
	{"HW_SRC_2_IN_CH1", "DL1_CH1 Switch", "DL1"},
	{"HW_SRC_2_IN_CH2", "DL1_CH2 Switch", "DL1"},
	{"HW_SRC_1_IN_CH1", "DL2_CH1 Switch", "DL2"},
	{"HW_SRC_1_IN_CH2", "DL2_CH2 Switch", "DL2"},
	{"HW_SRC_2_IN_CH1", "DL2_CH1 Switch", "DL2"},
	{"HW_SRC_2_IN_CH2", "DL2_CH2 Switch", "DL2"},
	{"HW_SRC_1_IN_CH1", "DL3_CH1 Switch", "DL3"},
	{"HW_SRC_1_IN_CH2", "DL3_CH2 Switch", "DL3"},
	{"HW_SRC_2_IN_CH1", "DL3_CH1 Switch", "DL3"},
	{"HW_SRC_2_IN_CH2", "DL3_CH2 Switch", "DL3"},
	{"HW_SRC_1_IN_CH1", "DL6_CH1 Switch", "DL6"},
	{"HW_SRC_1_IN_CH2", "DL6_CH2 Switch", "DL6"},
	{"HW_SRC_2_IN_CH1", "DL6_CH1 Switch", "DL6"},
	{"HW_SRC_2_IN_CH2", "DL6_CH2 Switch", "DL6"},
	{"HW_SRC_1_IN_CH1", "DL5_CH1 Switch", "DL5"},
	{"HW_SRC_1_IN_CH2", "DL5_CH2 Switch", "DL5"},
	{"HW_SRC_2_IN_CH1", "DL5_CH1 Switch", "DL5"},
	{"HW_SRC_2_IN_CH2", "DL5_CH2 Switch", "DL5"},
	{"HW_SRC_1_IN_CH1", "DL4_CH1 Switch", "DL4"},
	{"HW_SRC_1_IN_CH2", "DL4_CH2 Switch", "DL4"},
	{"HW_SRC_2_IN_CH1", "DL4_CH1 Switch", "DL4"},
	{"HW_SRC_2_IN_CH2", "DL4_CH2 Switch", "DL4"},

	{"HW_SRC_1_In", NULL, "HW_SRC_1_IN_CH1"},
	{"HW_SRC_1_In", NULL, "HW_SRC_1_IN_CH2"},

	{"HW_SRC_2_In", NULL, "HW_SRC_2_IN_CH1"},
	{"HW_SRC_2_In", NULL, "HW_SRC_2_IN_CH2"},

	{"HW_SRC_1_In", NULL, HW_SRC_1_EN_W_NAME, mtk_afe_src_en_connect},
	{"HW_SRC_1_Out", NULL, HW_SRC_1_EN_W_NAME, mtk_afe_src_en_connect},
	{"HW_SRC_2_In", NULL, HW_SRC_2_EN_W_NAME, mtk_afe_src_en_connect},
	{"HW_SRC_2_Out", NULL, HW_SRC_2_EN_W_NAME, mtk_afe_src_en_connect},

	{"HW SRC 1 In Endpoint", NULL, "HW_SRC_1_In"},
	{"HW SRC 2 In Endpoint", NULL, "HW_SRC_2_In"},
	{"HW_SRC_1_Out", NULL, "HW SRC 1 Out Endpoint"},
	{"HW_SRC_2_Out", NULL, "HW SRC 2 Out Endpoint"},
};

/* dai ops */
static int mtk_dai_src_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int id = dai->id;
	struct mtk_afe_src_priv *src_priv = afe_priv->dai_priv[id];
	unsigned int sft, mask;
	unsigned int rate = params_rate(params);
	unsigned int rate_reg = mt8186_rate_transform(afe->dev, rate, id);

	dev_dbg(afe->dev, "%s(), id %d, stream %d, rate %d\n",
		__func__, id, substream->stream, rate);

	/* rate */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		src_priv->dl_rate = rate;
		if (id == MT8186_DAI_SRC_1) {
			sft = GENERAL1_ASRCIN_MODE_SFT;
			mask = GENERAL1_ASRCIN_MODE_MASK;
		} else {
			sft = GENERAL2_ASRCIN_MODE_SFT;
			mask = GENERAL2_ASRCIN_MODE_MASK;
		}
	} else {
		src_priv->ul_rate = rate;
		if (id == MT8186_DAI_SRC_1) {
			sft = GENERAL1_ASRCOUT_MODE_SFT;
			mask = GENERAL1_ASRCOUT_MODE_MASK;
		} else {
			sft = GENERAL2_ASRCOUT_MODE_SFT;
			mask = GENERAL2_ASRCOUT_MODE_MASK;
		}
	}

	regmap_update_bits(afe->regmap, GENERAL_ASRC_MODE, mask << sft, rate_reg << sft);

	return 0;
}

static int mtk_dai_src_hw_free(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int id = dai->id;
	struct mtk_afe_src_priv *src_priv = afe_priv->dai_priv[id];

	dev_dbg(afe->dev, "%s(), id %d, stream %d\n",
		__func__, id, substream->stream);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		src_priv->dl_rate = 0;
	else
		src_priv->ul_rate = 0;

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_src_ops = {
	.hw_params = mtk_dai_src_hw_params,
	.hw_free = mtk_dai_src_hw_free,
};

/* dai driver */
#define MTK_SRC_RATES (SNDRV_PCM_RATE_8000_48000 |\
		       SNDRV_PCM_RATE_88200 |\
		       SNDRV_PCM_RATE_96000 |\
		       SNDRV_PCM_RATE_176400 |\
		       SNDRV_PCM_RATE_192000)

#define MTK_SRC_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mtk_dai_src_driver[] = {
	{
		.name = "HW_SRC_1",
		.id = MT8186_DAI_SRC_1,
		.playback = {
			.stream_name = "HW_SRC_1_In",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_SRC_RATES,
			.formats = MTK_SRC_FORMATS,
		},
		.capture = {
			.stream_name = "HW_SRC_1_Out",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_SRC_RATES,
			.formats = MTK_SRC_FORMATS,
		},
		.ops = &mtk_dai_src_ops,
	},
	{
		.name = "HW_SRC_2",
		.id = MT8186_DAI_SRC_2,
		.playback = {
			.stream_name = "HW_SRC_2_In",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_SRC_RATES,
			.formats = MTK_SRC_FORMATS,
		},
		.capture = {
			.stream_name = "HW_SRC_2_Out",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_SRC_RATES,
			.formats = MTK_SRC_FORMATS,
		},
		.ops = &mtk_dai_src_ops,
	},
};

int mt8186_dai_src_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;
	int ret;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mtk_dai_src_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mtk_dai_src_driver);

	dai->dapm_widgets = mtk_dai_src_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mtk_dai_src_widgets);
	dai->dapm_routes = mtk_dai_src_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mtk_dai_src_routes);

	/* set dai priv */
	ret = mt8186_dai_set_priv(afe, MT8186_DAI_SRC_1,
				  sizeof(struct mtk_afe_src_priv), NULL);
	if (ret)
		return ret;

	ret = mt8186_dai_set_priv(afe, MT8186_DAI_SRC_2,
				  sizeof(struct mtk_afe_src_priv), NULL);
	if (ret)
		return ret;

	return 0;
}
