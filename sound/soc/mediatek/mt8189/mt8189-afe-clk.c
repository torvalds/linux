// SPDX-License-Identifier: GPL-2.0
/*
 *  mt8189-afe-clk.c  --  Mediatek 8189 afe clock ctrl
 *
 *  Copyright (c) 2025 MediaTek Inc.
 *  Author: Darren Ye <darren.ye@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "mt8189-afe-common.h"
#include "mt8189-afe-clk.h"

/* mck */
struct mt8189_mck_div {
	int m_sel_id;
	int div_clk_id;
};

static const struct mt8189_mck_div mck_div[MT8189_MCK_NUM] = {
	[MT8189_I2SIN0_MCK] = {
		.m_sel_id = MT8189_CLK_TOP_I2SIN0_M_SEL,
		.div_clk_id = MT8189_CLK_TOP_APLL12_DIV_I2SIN0,
	},
	[MT8189_I2SIN1_MCK] = {
		.m_sel_id = MT8189_CLK_TOP_I2SIN1_M_SEL,
		.div_clk_id = MT8189_CLK_TOP_APLL12_DIV_I2SIN1,
	},
	[MT8189_I2SOUT0_MCK] = {
		.m_sel_id = MT8189_CLK_TOP_I2SOUT0_M_SEL,
		.div_clk_id = MT8189_CLK_TOP_APLL12_DIV_I2SOUT0,
	},
	[MT8189_I2SOUT1_MCK] = {
		.m_sel_id = MT8189_CLK_TOP_I2SOUT1_M_SEL,
		.div_clk_id = MT8189_CLK_TOP_APLL12_DIV_I2SOUT1,
	},
	[MT8189_FMI2S_MCK] = {
		.m_sel_id = MT8189_CLK_TOP_FMI2S_M_SEL,
		.div_clk_id = MT8189_CLK_TOP_APLL12_DIV_FMI2S,
	},
	[MT8189_TDMOUT_MCK] = {
		.m_sel_id = MT8189_CLK_TOP_TDMOUT_M_SEL,
		.div_clk_id = MT8189_CLK_TOP_APLL12_DIV_TDMOUT_M,
	},
	[MT8189_TDMOUT_BCK] = {
		.m_sel_id = -1,
		.div_clk_id = MT8189_CLK_TOP_APLL12_DIV_TDMOUT_B,
	},
};

static const char *aud_clks[MT8189_CLK_NUM] = {
	[MT8189_CLK_TOP_MUX_AUDIOINTBUS] = "top_aud_intbus",
	[MT8189_CLK_TOP_MUX_AUD_ENG1] = "top_aud_eng1",
	[MT8189_CLK_TOP_MUX_AUD_ENG2] = "top_aud_eng2",
	[MT8189_CLK_TOP_MUX_AUDIO_H] = "top_aud_h",
	/* pll */
	[MT8189_CLK_TOP_APLL1_CK] = "apll1",
	[MT8189_CLK_TOP_APLL2_CK] = "apll2",
	/* divider */
	[MT8189_CLK_TOP_APLL1_D4] = "apll1_d4",
	[MT8189_CLK_TOP_APLL2_D4] = "apll2_d4",
	[MT8189_CLK_TOP_APLL12_DIV_I2SIN0] = "apll12_div_i2sin0",
	[MT8189_CLK_TOP_APLL12_DIV_I2SIN1] = "apll12_div_i2sin1",
	[MT8189_CLK_TOP_APLL12_DIV_I2SOUT0] = "apll12_div_i2sout0",
	[MT8189_CLK_TOP_APLL12_DIV_I2SOUT1] = "apll12_div_i2sout1",
	[MT8189_CLK_TOP_APLL12_DIV_FMI2S] = "apll12_div_fmi2s",
	[MT8189_CLK_TOP_APLL12_DIV_TDMOUT_M] = "apll12_div_tdmout_m",
	[MT8189_CLK_TOP_APLL12_DIV_TDMOUT_B] = "apll12_div_tdmout_b",
	/* mux */
	[MT8189_CLK_TOP_MUX_AUD_1] = "top_apll1",
	[MT8189_CLK_TOP_MUX_AUD_2] = "top_apll2",
	[MT8189_CLK_TOP_I2SIN0_M_SEL] = "top_i2sin0",
	[MT8189_CLK_TOP_I2SIN1_M_SEL] = "top_i2sin1",
	[MT8189_CLK_TOP_I2SOUT0_M_SEL] = "top_i2sout0",
	[MT8189_CLK_TOP_I2SOUT1_M_SEL] = "top_i2sout1",
	[MT8189_CLK_TOP_FMI2S_M_SEL] = "top_fmi2s",
	[MT8189_CLK_TOP_TDMOUT_M_SEL] = "top_dptx",
	/* top 26m*/
	[MT8189_CLK_TOP_CLK26M] = "clk26m",
	/* peri */
	[MT8189_CLK_PERAO_AUDIO_SLV_CK_PERI] = "aud_slv_ck_peri",
	[MT8189_CLK_PERAO_AUDIO_MST_CK_PERI] = "aud_mst_ck_peri",
	[MT8189_CLK_PERAO_INTBUS_CK_PERI] = "aud_intbus_ck_peri",
};

int mt8189_afe_enable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	int ret;

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(afe->dev, "failed to enable clk\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt8189_afe_enable_clk);

void mt8189_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	if (clk)
		clk_disable_unprepare(clk);
	else
		dev_dbg(afe->dev, "NULL clk\n");
}
EXPORT_SYMBOL_GPL(mt8189_afe_disable_clk);

static int mt8189_afe_set_clk_rate(struct mtk_base_afe *afe, struct clk *clk,
				   unsigned int rate)
{
	int ret;

	if (clk) {
		ret = clk_set_rate(clk, rate);
		if (ret) {
			dev_err(afe->dev, "failed to set clk rate\n");
			return ret;
		}
	}

	return 0;
}

static int mt8189_afe_set_clk_parent(struct mtk_base_afe *afe, struct clk *clk,
				     struct clk *parent)
{
	int ret;

	if (clk && parent) {
		ret = clk_set_parent(clk, parent);
		if (ret) {
			dev_dbg(afe->dev, "failed to set clk parent %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static unsigned int get_top_cg_reg(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8189_AUDIO_26M_EN_ON:
	case MT8189_AUDIO_F3P25M_EN_ON:
	case MT8189_AUDIO_APLL1_EN_ON:
	case MT8189_AUDIO_APLL2_EN_ON:
		return AUDIO_ENGEN_CON0;
	case MT8189_CG_AUDIO_HOPPING_CK:
	case MT8189_CG_AUDIO_F26M_CK:
	case MT8189_CG_APLL1_CK:
	case MT8189_CG_APLL2_CK:
	case MT8189_PDN_APLL_TUNER2:
	case MT8189_PDN_APLL_TUNER1:
		return AUDIO_TOP_CON4;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_mask(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8189_AUDIO_26M_EN_ON:
		return AUDIO_26M_EN_ON_MASK_SFT;
	case MT8189_AUDIO_F3P25M_EN_ON:
		return AUDIO_F3P25M_EN_ON_MASK_SFT;
	case MT8189_AUDIO_APLL1_EN_ON:
		return AUDIO_APLL1_EN_ON_MASK_SFT;
	case MT8189_AUDIO_APLL2_EN_ON:
		return AUDIO_APLL2_EN_ON_MASK_SFT;
	case MT8189_CG_AUDIO_HOPPING_CK:
		return CG_AUDIO_HOPPING_CK_MASK_SFT;
	case MT8189_CG_AUDIO_F26M_CK:
		return CG_AUDIO_F26M_CK_MASK_SFT;
	case MT8189_CG_APLL1_CK:
		return CG_APLL1_CK_MASK_SFT;
	case MT8189_CG_APLL2_CK:
		return CG_APLL2_CK_MASK_SFT;
	case MT8189_PDN_APLL_TUNER2:
		return PDN_APLL_TUNER2_MASK_SFT;
	case MT8189_PDN_APLL_TUNER1:
		return PDN_APLL_TUNER1_MASK_SFT;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_on_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8189_AUDIO_26M_EN_ON:
	case MT8189_AUDIO_F3P25M_EN_ON:
	case MT8189_AUDIO_APLL1_EN_ON:
	case MT8189_AUDIO_APLL2_EN_ON:
		return get_top_cg_mask(cg_type);
	case MT8189_CG_AUDIO_HOPPING_CK:
	case MT8189_CG_AUDIO_F26M_CK:
	case MT8189_CG_APLL1_CK:
	case MT8189_CG_APLL2_CK:
	case MT8189_PDN_APLL_TUNER2:
	case MT8189_PDN_APLL_TUNER1:
		return 0;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_off_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8189_AUDIO_26M_EN_ON:
	case MT8189_AUDIO_F3P25M_EN_ON:
	case MT8189_AUDIO_APLL1_EN_ON:
	case MT8189_AUDIO_APLL2_EN_ON:
		return 0;
	case MT8189_CG_AUDIO_HOPPING_CK:
	case MT8189_CG_AUDIO_F26M_CK:
	case MT8189_CG_APLL1_CK:
	case MT8189_CG_APLL2_CK:
	case MT8189_PDN_APLL_TUNER2:
	case MT8189_PDN_APLL_TUNER1:
		return get_top_cg_mask(cg_type);
	default:
		return get_top_cg_mask(cg_type);
	}
}

static int mt8189_afe_enable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_on_val(cg_type);

	if (!afe->regmap) {
		dev_err(afe->dev, "afe regmap is null !!!\n");
		return 0;
	}

	dev_dbg(afe->dev, "reg: 0x%x, mask: 0x%x, val: 0x%x\n", reg, mask, val);

	return regmap_update_bits(afe->regmap, reg, mask, val);
}

static void mt8189_afe_disable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_off_val(cg_type);

	if (!afe->regmap) {
		dev_warn(afe->dev, "skip regmap\n");
		return;
	}

	dev_dbg(afe->dev, "reg: 0x%x, mask: 0x%x, val: 0x%x\n", reg, mask, val);
	regmap_update_bits(afe->regmap, reg, mask, val);
}

static int apll1_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int ret;

	dev_dbg(afe->dev, "enable: %d\n", enable);

	if (enable) {
		ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1]);
		if (ret)
			return ret;

		ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1],
						afe_priv->clk[MT8189_CLK_TOP_APLL1_CK]);
		if (ret)
			goto clk_ck_mux_aud1_parent_err;

		/* 180.6336 / 4 = 45.1584MHz */
		ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG1]);
		if (ret)
			goto clk_ck_mux_eng1_err;

		ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG1],
						afe_priv->clk[MT8189_CLK_TOP_APLL1_D4]);
		if (ret)
			goto clk_ck_mux_eng1_parent_err;

		ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
		if (ret)
			goto clk_ck_mux_audio_h_err;

		ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H],
						afe_priv->clk[MT8189_CLK_TOP_APLL1_CK]);
		if (ret)
			goto clk_ck_mux_audio_h_parent_err;
	} else {
		mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG1],
					  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);

		mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG1]);

		mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1],
					  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);

		mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1]);
		mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H],
					  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
		mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
	}

	return 0;

clk_ck_mux_audio_h_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
clk_ck_mux_audio_h_err:
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG1],
				  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
clk_ck_mux_eng1_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG1]);
clk_ck_mux_eng1_err:
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1],
				  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
clk_ck_mux_aud1_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1]);

	return ret;
}

static int apll2_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int ret;

	dev_dbg(afe->dev, "enable: %d\n", enable);

	if (enable) {
		ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2]);
		if (ret)
			return ret;

		ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2],
						afe_priv->clk[MT8189_CLK_TOP_APLL2_CK]);
		if (ret)
			goto clk_ck_mux_aud2_parent_err;

		/* 196.608 / 4 = 49.152MHz */
		ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG2]);
		if (ret)
			goto clk_ck_mux_eng2_err;

		ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG2],
						afe_priv->clk[MT8189_CLK_TOP_APLL2_D4]);
		if (ret)
			goto clk_ck_mux_eng2_parent_err;

		ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
		if (ret)
			goto clk_ck_mux_audio_h_err;

		ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H],
						afe_priv->clk[MT8189_CLK_TOP_APLL2_CK]);
		if (ret)
			goto clk_ck_mux_audio_h_parent_err;
	} else {
		mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG2],
					  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);

		mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG2]);

		mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2],
					  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);

		mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2]);
		mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H],
					  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
		mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
	}

	return 0;

clk_ck_mux_audio_h_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
clk_ck_mux_audio_h_err:
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG2],
				  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
clk_ck_mux_eng2_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_ENG2]);
clk_ck_mux_eng2_err:
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2],
				  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
clk_ck_mux_aud2_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2]);

	return ret;
}

static int mt8189_afe_disable_apll(struct mtk_base_afe *afe)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
	if (ret)
		return ret;

	ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1]);
	if (ret)
		goto clk_ck_mux_aud1_err;

	ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1],
					afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
	if (ret)
		goto clk_ck_mux_aud1_parent_err;

	ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2]);
	if (ret)
		goto clk_ck_mux_aud2_err;

	ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2],
					afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
	if (ret)
		goto clk_ck_mux_aud2_parent_err;

	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1]);
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2]);
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H],
				  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);

	return 0;

clk_ck_mux_aud2_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_2]);
clk_ck_mux_aud2_err:
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1],
				  afe_priv->clk[MT8189_CLK_TOP_APLL1_CK]);
clk_ck_mux_aud1_parent_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUD_1]);
clk_ck_mux_aud1_err:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);

	return ret;
}

int mt8189_apll1_enable(struct mtk_base_afe *afe)
{
	int ret;

	/* setting for APLL */
	ret = apll1_mux_setting(afe, true);
	if (ret)
		return ret;

	ret = mt8189_afe_enable_top_cg(afe, MT8189_CG_APLL1_CK);
	if (ret)
		return ret;

	ret = mt8189_afe_enable_top_cg(afe, MT8189_PDN_APLL_TUNER1);
	if (ret)
		return ret;

	/* sel 44.1kHz:1, apll_div:7, upper bound:3 */
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
			   XTAL_EN_128FS_SEL_MASK_SFT | APLL_DIV_MASK_SFT |
			   UPPER_BOUND_MASK_SFT,
			   (0x1 << XTAL_EN_128FS_SEL_SFT) | (7 << APLL_DIV_SFT) |
			   (3 << UPPER_BOUND_SFT));

	/* apll1 freq tuner enable */
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x1 << FREQ_TUNER_EN_SFT);

	/* audio apll1 on */
	ret = mt8189_afe_enable_top_cg(afe, MT8189_AUDIO_APLL1_EN_ON);
	if (ret)
		return ret;

	return 0;
}

void mt8189_apll1_disable(struct mtk_base_afe *afe)
{
	/* audio apll1 off */
	mt8189_afe_disable_top_cg(afe, MT8189_AUDIO_APLL1_EN_ON);

	/* apll1 freq tuner disable */
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x0);

	mt8189_afe_disable_top_cg(afe, MT8189_PDN_APLL_TUNER1);
	mt8189_afe_disable_top_cg(afe, MT8189_CG_APLL1_CK);
	apll1_mux_setting(afe, false);
}

int mt8189_apll2_enable(struct mtk_base_afe *afe)
{
	int ret;

	/* setting for APLL */
	ret = apll2_mux_setting(afe, true);
	if (ret)
		return ret;

	ret = mt8189_afe_enable_top_cg(afe, MT8189_CG_APLL2_CK);
	if (ret)
		return ret;

	ret = mt8189_afe_enable_top_cg(afe, MT8189_PDN_APLL_TUNER2);
	if (ret)
		return ret;

	/* sel 48kHz: 2, apll_div: 7, upper bound: 3*/
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
			   XTAL_EN_128FS_SEL_MASK_SFT | APLL_DIV_MASK_SFT |
			   UPPER_BOUND_MASK_SFT,
			   (0x2 << XTAL_EN_128FS_SEL_SFT) | (7 << APLL_DIV_SFT) |
			   (3 << UPPER_BOUND_SFT));

	/* apll2 freq tuner enable */
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x1 << FREQ_TUNER_EN_SFT);

	/* audio apll2 on */
	ret = mt8189_afe_enable_top_cg(afe, MT8189_AUDIO_APLL2_EN_ON);
	if (ret)
		return ret;

	return 0;
}

void mt8189_apll2_disable(struct mtk_base_afe *afe)
{
	/* audio apll2 off */
	mt8189_afe_disable_top_cg(afe, MT8189_AUDIO_APLL2_EN_ON);

	/* apll2 freq tuner disable */
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x0);

	mt8189_afe_disable_top_cg(afe, MT8189_PDN_APLL_TUNER2);
	mt8189_afe_disable_top_cg(afe, MT8189_CG_APLL2_CK);
	apll2_mux_setting(afe, false);
}

int mt8189_get_apll_rate(struct mtk_base_afe *afe, int apll)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int clk_id;

	if (apll < MT8189_APLL1 || apll > MT8189_APLL2) {
		dev_warn(afe->dev, "invalid clk id %d\n", apll);
		return 0;
	}

	if (apll == MT8189_APLL1)
		clk_id = MT8189_CLK_TOP_APLL1_CK;
	else
		clk_id = MT8189_CLK_TOP_APLL2_CK;

	return clk_get_rate(afe_priv->clk[clk_id]);
}

int mt8189_get_apll_by_rate(struct mtk_base_afe *afe, int rate)
{
	return (rate % 8000) ? MT8189_APLL1 : MT8189_APLL2;
}

int mt8189_get_apll_by_name(struct mtk_base_afe *afe, const char *name)
{
	if (strcmp(name, APLL1_W_NAME) == 0)
		return MT8189_APLL1;

	return MT8189_APLL2;
}

int mt8189_mck_enable(struct mtk_base_afe *afe, int mck_id, int rate)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int apll = mt8189_get_apll_by_rate(afe, rate);
	int apll_clk_id = apll == MT8189_APLL1 ?
			  MT8189_CLK_TOP_MUX_AUD_1 : MT8189_CLK_TOP_MUX_AUD_2;
	int m_sel_id;
	int div_clk_id;
	int ret;

	dev_dbg(afe->dev, "mck_id: %d, rate: %d\n", mck_id, rate);

	if (mck_id >= MT8189_MCK_NUM || mck_id < 0)
		return -EINVAL;

	m_sel_id = mck_div[mck_id].m_sel_id;
	div_clk_id = mck_div[mck_id].div_clk_id;

	/* select apll */
	if (m_sel_id >= 0) {
		ret = mt8189_afe_enable_clk(afe, afe_priv->clk[m_sel_id]);
		if (ret)
			return ret;

		ret = mt8189_afe_set_clk_parent(afe, afe_priv->clk[m_sel_id],
						afe_priv->clk[apll_clk_id]);
		if (ret)
			return ret;
	}

	/* enable div, set rate */
	if (div_clk_id < 0) {
		dev_err(afe->dev, "invalid div_clk_id %d\n", div_clk_id);
		return -EINVAL;
	}

	ret = mt8189_afe_enable_clk(afe, afe_priv->clk[div_clk_id]);
	if (ret)
		return ret;

	ret = mt8189_afe_set_clk_rate(afe, afe_priv->clk[div_clk_id], rate);
	if (ret)
		return ret;

	return 0;
}

int mt8189_mck_disable(struct mtk_base_afe *afe, int mck_id)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int m_sel_id;
	int div_clk_id;

	dev_dbg(afe->dev, "mck_id: %d.\n", mck_id);

	if (mck_id < 0) {
		dev_err(afe->dev, "mck_id = %d < 0\n", mck_id);
		return -EINVAL;
	}

	m_sel_id = mck_div[mck_id].m_sel_id;
	div_clk_id = mck_div[mck_id].div_clk_id;

	if (div_clk_id < 0) {
		dev_err(afe->dev, "div_clk_id = %d < 0\n",
			div_clk_id);
		return -EINVAL;
	}

	mt8189_afe_disable_clk(afe, afe_priv->clk[div_clk_id]);

	if (m_sel_id >= 0)
		mt8189_afe_disable_clk(afe, afe_priv->clk[m_sel_id]);

	return 0;
}

int mt8189_afe_enable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;

	/* bus clock for AFE internal access, like AFE SRAM */
	mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIOINTBUS]);
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIOINTBUS],
				  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);
	/* enable audio clock source */
	mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
	mt8189_afe_set_clk_parent(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H],
				  afe_priv->clk[MT8189_CLK_TOP_CLK26M]);

	return 0;
}

int mt8189_afe_disable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;

	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIO_H]);
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_TOP_MUX_AUDIOINTBUS]);

	return 0;
}

int mt8189_afe_enable_main_clock(struct mtk_base_afe *afe)
{
	return mt8189_afe_enable_top_cg(afe, MT8189_AUDIO_26M_EN_ON);
}

void mt8189_afe_disable_main_clock(struct mtk_base_afe *afe)
{
	mt8189_afe_disable_top_cg(afe, MT8189_AUDIO_26M_EN_ON);
}

static int mt8189_afe_enable_ao_clock(struct mtk_base_afe *afe)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* Peri clock AO enable */
	ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_PERAO_INTBUS_CK_PERI]);
	if (ret)
		return ret;

	ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_PERAO_AUDIO_SLV_CK_PERI]);
	if (ret)
		goto err_clk_perao_slv;

	ret = mt8189_afe_enable_clk(afe, afe_priv->clk[MT8189_CLK_PERAO_AUDIO_MST_CK_PERI]);
	if (ret)
		goto err_clk_perao_mst;

	return 0;

err_clk_perao_mst:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_PERAO_AUDIO_SLV_CK_PERI]);
err_clk_perao_slv:
	mt8189_afe_disable_clk(afe, afe_priv->clk[MT8189_CLK_PERAO_INTBUS_CK_PERI]);

	return ret;
}

int mt8189_init_clock(struct mtk_base_afe *afe)
{
	struct mt8189_afe_private *afe_priv = afe->platform_priv;
	int ret;
	int i;

	afe_priv->clk = devm_kcalloc(afe->dev, MT8189_CLK_NUM, sizeof(*afe_priv->clk),
				     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < MT8189_CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_err(afe->dev, "devm_clk_get %s fail\n", aud_clks[i]);
			return PTR_ERR(afe_priv->clk[i]);
		}
	}

	ret = mt8189_afe_disable_apll(afe);
	if (ret)
		return ret;

	ret = mt8189_afe_enable_ao_clock(afe);
	if (ret)
		return ret;

	return 0;
}
