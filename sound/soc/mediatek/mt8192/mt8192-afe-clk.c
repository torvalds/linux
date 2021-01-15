// SPDX-License-Identifier: GPL-2.0
//
// mt8192-afe-clk.c  --  Mediatek 8192 afe clock ctrl
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Shane Chien <shane.chien@mediatek.com>
//

#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "mt8192-afe-clk.h"
#include "mt8192-afe-common.h"

static const char *aud_clks[CLK_NUM] = {
	[CLK_AFE] = "aud_afe_clk",
	[CLK_TML] = "aud_tml_clk",
	[CLK_APLL22M] = "aud_apll22m_clk",
	[CLK_APLL24M] = "aud_apll24m_clk",
	[CLK_APLL1_TUNER] = "aud_apll1_tuner_clk",
	[CLK_APLL2_TUNER] = "aud_apll2_tuner_clk",
	[CLK_NLE] = "aud_nle",
	[CLK_INFRA_SYS_AUDIO] = "aud_infra_clk",
	[CLK_INFRA_AUDIO_26M] = "aud_infra_26m_clk",
	[CLK_MUX_AUDIO] = "top_mux_audio",
	[CLK_MUX_AUDIOINTBUS] = "top_mux_audio_int",
	[CLK_TOP_MAINPLL_D4_D4] = "top_mainpll_d4_d4",
	[CLK_TOP_MUX_AUD_1] = "top_mux_aud_1",
	[CLK_TOP_APLL1_CK] = "top_apll1_ck",
	[CLK_TOP_MUX_AUD_2] = "top_mux_aud_2",
	[CLK_TOP_APLL2_CK] = "top_apll2_ck",
	[CLK_TOP_MUX_AUD_ENG1] = "top_mux_aud_eng1",
	[CLK_TOP_APLL1_D4] = "top_apll1_d4",
	[CLK_TOP_MUX_AUD_ENG2] = "top_mux_aud_eng2",
	[CLK_TOP_APLL2_D4] = "top_apll2_d4",
	[CLK_TOP_MUX_AUDIO_H] = "top_mux_audio_h",
	[CLK_TOP_I2S0_M_SEL] = "top_i2s0_m_sel",
	[CLK_TOP_I2S1_M_SEL] = "top_i2s1_m_sel",
	[CLK_TOP_I2S2_M_SEL] = "top_i2s2_m_sel",
	[CLK_TOP_I2S3_M_SEL] = "top_i2s3_m_sel",
	[CLK_TOP_I2S4_M_SEL] = "top_i2s4_m_sel",
	[CLK_TOP_I2S5_M_SEL] = "top_i2s5_m_sel",
	[CLK_TOP_I2S6_M_SEL] = "top_i2s6_m_sel",
	[CLK_TOP_I2S7_M_SEL] = "top_i2s7_m_sel",
	[CLK_TOP_I2S8_M_SEL] = "top_i2s8_m_sel",
	[CLK_TOP_I2S9_M_SEL] = "top_i2s9_m_sel",
	[CLK_TOP_APLL12_DIV0] = "top_apll12_div0",
	[CLK_TOP_APLL12_DIV1] = "top_apll12_div1",
	[CLK_TOP_APLL12_DIV2] = "top_apll12_div2",
	[CLK_TOP_APLL12_DIV3] = "top_apll12_div3",
	[CLK_TOP_APLL12_DIV4] = "top_apll12_div4",
	[CLK_TOP_APLL12_DIVB] = "top_apll12_divb",
	[CLK_TOP_APLL12_DIV5] = "top_apll12_div5",
	[CLK_TOP_APLL12_DIV6] = "top_apll12_div6",
	[CLK_TOP_APLL12_DIV7] = "top_apll12_div7",
	[CLK_TOP_APLL12_DIV8] = "top_apll12_div8",
	[CLK_TOP_APLL12_DIV9] = "top_apll12_div9",
	[CLK_CLK26M] = "top_clk26m_clk",
};

int mt8192_set_audio_int_bus_parent(struct mtk_base_afe *afe,
				    int clk_id)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = clk_set_parent(afe_priv->clk[CLK_MUX_AUDIOINTBUS],
			     afe_priv->clk[clk_id]);
	if (ret) {
		dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIOINTBUS],
			aud_clks[clk_id], ret);
	}

	return ret;
}

static int apll1_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int ret;

	if (enable) {
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_1]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_1], ret);
			goto EXIT;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_1],
				     afe_priv->clk[CLK_TOP_APLL1_CK]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_1],
				aud_clks[CLK_TOP_APLL1_CK], ret);
			goto EXIT;
		}

		/* 180.6336 / 4 = 45.1584MHz */
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG1], ret);
			goto EXIT;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1],
				     afe_priv->clk[CLK_TOP_APLL1_D4]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG1],
				aud_clks[CLK_TOP_APLL1_D4], ret);
			goto EXIT;
		}
	} else {
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG1],
				aud_clks[CLK_CLK26M], ret);
			goto EXIT;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1]);

		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_1],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_1],
				aud_clks[CLK_CLK26M], ret);
			goto EXIT;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_1]);
	}

EXIT:
	return ret;
}

static int apll2_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int ret;

	if (enable) {
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_2]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_2], ret);
			goto EXIT;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_2],
				     afe_priv->clk[CLK_TOP_APLL2_CK]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_2],
				aud_clks[CLK_TOP_APLL2_CK], ret);
			goto EXIT;
		}

		/* 196.608 / 4 = 49.152MHz */
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG2], ret);
			goto EXIT;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2],
				     afe_priv->clk[CLK_TOP_APLL2_D4]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG2],
				aud_clks[CLK_TOP_APLL2_D4], ret);
			goto EXIT;
		}
	} else {
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG2],
				aud_clks[CLK_CLK26M], ret);
			goto EXIT;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2]);

		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_2],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_2],
				aud_clks[CLK_CLK26M], ret);
			goto EXIT;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_2]);
	}

EXIT:
	return ret;
}

int mt8192_afe_enable_clock(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int ret;

	dev_info(afe->dev, "%s()\n", __func__);

	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_SYS_AUDIO]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_SYS_AUDIO], ret);
		goto EXIT;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_AUDIO_26M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_AUDIO_26M], ret);
		goto EXIT;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_MUX_AUDIO]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIO], ret);
		goto EXIT;
	}
	ret = clk_set_parent(afe_priv->clk[CLK_MUX_AUDIO],
			     afe_priv->clk[CLK_CLK26M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIO],
			aud_clks[CLK_CLK26M], ret);
		goto EXIT;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIOINTBUS], ret);
		goto EXIT;
	}

	ret = mt8192_set_audio_int_bus_parent(afe, CLK_CLK26M);
	if (ret) {
		dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIOINTBUS],
			aud_clks[CLK_CLK26M], ret);
		goto EXIT;
	}

	ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUDIO_H],
			     afe_priv->clk[CLK_TOP_APLL2_CK]);
	if (ret) {
		dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_TOP_MUX_AUDIO_H],
			aud_clks[CLK_TOP_APLL2_CK], ret);
		goto EXIT;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_AFE]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_AFE], ret);
		goto EXIT;
	}

EXIT:
	return ret;
}

void mt8192_afe_disable_clock(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	dev_info(afe->dev, "%s()\n", __func__);

	clk_disable_unprepare(afe_priv->clk[CLK_AFE]);
	mt8192_set_audio_int_bus_parent(afe, CLK_CLK26M);
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIO]);
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_AUDIO_26M]);
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUDIO]);
}

int mt8192_apll1_enable(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* setting for APLL */
	apll1_mux_setting(afe, true);

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL22M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL22M], ret);
		goto EXIT;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL1_TUNER]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL1_TUNER], ret);
		goto EXIT;
	}

	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
			   0x0000FFF7, 0x00000832);
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG, 0x1, 0x1);

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_22M_ON_MASK_SFT,
			   0x1 << AFE_22M_ON_SFT);

EXIT:
	return ret;
}

void mt8192_apll1_disable(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_22M_ON_MASK_SFT,
			   0x0 << AFE_22M_ON_SFT);

	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG, 0x1, 0x0);

	clk_disable_unprepare(afe_priv->clk[CLK_APLL1_TUNER]);
	clk_disable_unprepare(afe_priv->clk[CLK_APLL22M]);

	apll1_mux_setting(afe, false);
}

int mt8192_apll2_enable(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* setting for APLL */
	apll2_mux_setting(afe, true);

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL24M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL24M], ret);
		goto EXIT;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL2_TUNER]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL2_TUNER], ret);
		goto EXIT;
	}

	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
			   0x0000FFF7, 0x00000634);
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG, 0x1, 0x1);

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_24M_ON_MASK_SFT,
			   0x1 << AFE_24M_ON_SFT);

EXIT:
	return ret;
}

void mt8192_apll2_disable(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_24M_ON_MASK_SFT,
			   0x0 << AFE_24M_ON_SFT);

	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG, 0x1, 0x0);

	clk_disable_unprepare(afe_priv->clk[CLK_APLL2_TUNER]);
	clk_disable_unprepare(afe_priv->clk[CLK_APLL24M]);

	apll2_mux_setting(afe, false);
}

int mt8192_get_apll_rate(struct mtk_base_afe *afe, int apll)
{
	return (apll == MT8192_APLL1) ? 180633600 : 196608000;
}

int mt8192_get_apll_by_rate(struct mtk_base_afe *afe, int rate)
{
	return ((rate % 8000) == 0) ? MT8192_APLL2 : MT8192_APLL1;
}

int mt8192_get_apll_by_name(struct mtk_base_afe *afe, const char *name)
{
	if (strcmp(name, APLL1_W_NAME) == 0)
		return MT8192_APLL1;
	else
		return MT8192_APLL2;
}

/* mck */
struct mt8192_mck_div {
	int m_sel_id;
	int div_clk_id;
	/* below will be deprecated */
	int div_pdn_reg;
	int div_pdn_mask_sft;
	int div_reg;
	int div_mask_sft;
	int div_mask;
	int div_sft;
	int div_apll_sel_reg;
	int div_apll_sel_mask_sft;
	int div_apll_sel_sft;
};

static const struct mt8192_mck_div mck_div[MT8192_MCK_NUM] = {
	[MT8192_I2S0_MCK] = {
		.m_sel_id = CLK_TOP_I2S0_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV0,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV0_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_2,
		.div_mask_sft = APLL12_CK_DIV0_MASK_SFT,
		.div_mask = APLL12_CK_DIV0_MASK,
		.div_sft = APLL12_CK_DIV0_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S0_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S0_MCK_SEL_SFT,
	},
	[MT8192_I2S1_MCK] = {
		.m_sel_id = CLK_TOP_I2S1_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV1,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV1_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_2,
		.div_mask_sft = APLL12_CK_DIV1_MASK_SFT,
		.div_mask = APLL12_CK_DIV1_MASK,
		.div_sft = APLL12_CK_DIV1_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S1_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S1_MCK_SEL_SFT,
	},
	[MT8192_I2S2_MCK] = {
		.m_sel_id = CLK_TOP_I2S2_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV2,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV2_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_2,
		.div_mask_sft = APLL12_CK_DIV2_MASK_SFT,
		.div_mask = APLL12_CK_DIV2_MASK,
		.div_sft = APLL12_CK_DIV2_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S2_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S2_MCK_SEL_SFT,
	},
	[MT8192_I2S3_MCK] = {
		.m_sel_id = CLK_TOP_I2S3_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV3,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV3_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_2,
		.div_mask_sft = APLL12_CK_DIV3_MASK_SFT,
		.div_mask = APLL12_CK_DIV3_MASK,
		.div_sft = APLL12_CK_DIV3_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S3_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S3_MCK_SEL_SFT,
	},
	[MT8192_I2S4_MCK] = {
		.m_sel_id = CLK_TOP_I2S4_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV4,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV4_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_3,
		.div_mask_sft = APLL12_CK_DIV4_MASK_SFT,
		.div_mask = APLL12_CK_DIV4_MASK,
		.div_sft = APLL12_CK_DIV4_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S4_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S4_MCK_SEL_SFT,
	},
	[MT8192_I2S4_BCK] = {
		.m_sel_id = -1,
		.div_clk_id = CLK_TOP_APLL12_DIVB,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIVB_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_2,
		.div_mask_sft = APLL12_CK_DIVB_MASK_SFT,
		.div_mask = APLL12_CK_DIVB_MASK,
		.div_sft = APLL12_CK_DIVB_SFT,
	},
	[MT8192_I2S5_MCK] = {
		.m_sel_id = CLK_TOP_I2S5_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV5,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV5_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_3,
		.div_mask_sft = APLL12_CK_DIV5_MASK_SFT,
		.div_mask = APLL12_CK_DIV5_MASK,
		.div_sft = APLL12_CK_DIV5_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S5_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S5_MCK_SEL_SFT,
	},
	[MT8192_I2S6_MCK] = {
		.m_sel_id = CLK_TOP_I2S6_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV6,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV6_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_3,
		.div_mask_sft = APLL12_CK_DIV6_MASK_SFT,
		.div_mask = APLL12_CK_DIV6_MASK,
		.div_sft = APLL12_CK_DIV6_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S6_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S6_MCK_SEL_SFT,
	},
	[MT8192_I2S7_MCK] = {
		.m_sel_id = CLK_TOP_I2S7_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV7,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV7_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_4,
		.div_mask_sft = APLL12_CK_DIV7_MASK_SFT,
		.div_mask = APLL12_CK_DIV7_MASK,
		.div_sft = APLL12_CK_DIV7_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S7_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S7_MCK_SEL_SFT,
	},
	[MT8192_I2S8_MCK] = {
		.m_sel_id = CLK_TOP_I2S8_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV8,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV8_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_4,
		.div_mask_sft = APLL12_CK_DIV8_MASK_SFT,
		.div_mask = APLL12_CK_DIV8_MASK,
		.div_sft = APLL12_CK_DIV8_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S8_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S8_MCK_SEL_SFT,
	},
	[MT8192_I2S9_MCK] = {
		.m_sel_id = CLK_TOP_I2S9_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV9,
		.div_pdn_reg = CLK_AUDDIV_0,
		.div_pdn_mask_sft = APLL12_DIV9_PDN_MASK_SFT,
		.div_reg = CLK_AUDDIV_4,
		.div_mask_sft = APLL12_CK_DIV9_MASK_SFT,
		.div_mask = APLL12_CK_DIV9_MASK,
		.div_sft = APLL12_CK_DIV9_SFT,
		.div_apll_sel_reg = CLK_AUDDIV_0,
		.div_apll_sel_mask_sft = APLL_I2S9_MCK_SEL_MASK_SFT,
		.div_apll_sel_sft = APLL_I2S9_MCK_SEL_SFT,
	},
};

int mt8192_mck_enable(struct mtk_base_afe *afe, int mck_id, int rate)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int apll = mt8192_get_apll_by_rate(afe, rate);
	int apll_clk_id = apll == MT8192_APLL1 ?
			  CLK_TOP_MUX_AUD_1 : CLK_TOP_MUX_AUD_2;
	int m_sel_id = mck_div[mck_id].m_sel_id;
	int div_clk_id = mck_div[mck_id].div_clk_id;
	int ret;

	/* select apll */
	if (m_sel_id >= 0) {
		ret = clk_prepare_enable(afe_priv->clk[m_sel_id]);
		if (ret) {
			dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[m_sel_id], ret);
			return ret;
		}
		ret = clk_set_parent(afe_priv->clk[m_sel_id],
				     afe_priv->clk[apll_clk_id]);
		if (ret) {
			dev_err(afe->dev, "%s(), clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[m_sel_id],
				aud_clks[apll_clk_id], ret);
			return ret;
		}
	}

	/* enable div, set rate */
	ret = clk_prepare_enable(afe_priv->clk[div_clk_id]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[div_clk_id], ret);
		return ret;
	}
	ret = clk_set_rate(afe_priv->clk[div_clk_id], rate);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_set_rate %s, rate %d, fail %d\n",
			__func__, aud_clks[div_clk_id],
			rate, ret);
		return ret;
	}

	return 0;
}

void mt8192_mck_disable(struct mtk_base_afe *afe, int mck_id)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int m_sel_id = mck_div[mck_id].m_sel_id;
	int div_clk_id = mck_div[mck_id].div_clk_id;

	clk_disable_unprepare(afe_priv->clk[div_clk_id]);
	if (m_sel_id >= 0)
		clk_disable_unprepare(afe_priv->clk[m_sel_id]);
}

int mt8192_init_clock(struct mtk_base_afe *afe)
{
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	struct device_node *of_node = afe->dev->of_node;
	int i = 0;

	afe_priv->clk = devm_kcalloc(afe->dev, CLK_NUM, sizeof(*afe_priv->clk),
				     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_warn(afe->dev, "%s devm_clk_get %s fail, ret %ld\n",
				 __func__,
				 aud_clks[i], PTR_ERR(afe_priv->clk[i]));
			afe_priv->clk[i] = NULL;
		}
	}

	afe_priv->apmixedsys = syscon_regmap_lookup_by_phandle(of_node,
							       "mediatek,apmixedsys");
	if (IS_ERR(afe_priv->apmixedsys)) {
		dev_err(afe->dev, "%s() Cannot find apmixedsys controller: %ld\n",
			__func__, PTR_ERR(afe_priv->apmixedsys));
		return PTR_ERR(afe_priv->apmixedsys);
	}

	afe_priv->topckgen = syscon_regmap_lookup_by_phandle(of_node,
							     "mediatek,topckgen");
	if (IS_ERR(afe_priv->topckgen)) {
		dev_err(afe->dev, "%s() Cannot find topckgen controller: %ld\n",
			__func__, PTR_ERR(afe_priv->topckgen));
		return PTR_ERR(afe_priv->topckgen);
	}

	afe_priv->infracfg = syscon_regmap_lookup_by_phandle(of_node,
							     "mediatek,infracfg");
	if (IS_ERR(afe_priv->infracfg)) {
		dev_err(afe->dev, "%s() Cannot find infracfg: %ld\n",
			__func__, PTR_ERR(afe_priv->infracfg));
		return PTR_ERR(afe_priv->infracfg);
	}

	return 0;
}
