// SPDX-License-Identifier: GPL-2.0
/*
 *  mt8196-afe-clk.c  --  Mediatek 8196 afe clock ctrl
 *
 *  Copyright (c) 2025 MediaTek Inc.
 *  Author: Darren Ye <darren.ye@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "mt8196-afe-clk.h"
#include "mt8196-afe-common.h"

static const char *aud_clks[MT8196_CLK_NUM] = {
	/* vlp clk */
	[MT8196_CLK_VLP_MUX_AUDIOINTBUS] = "top_aud_intbus",
	[MT8196_CLK_VLP_MUX_AUD_ENG1] = "top_aud_eng1",
	[MT8196_CLK_VLP_MUX_AUD_ENG2] = "top_aud_eng2",
	[MT8196_CLK_VLP_MUX_AUDIO_H] = "top_aud_h",
	/* pll */
	[MT8196_CLK_TOP_APLL1_CK] = "apll1",
	[MT8196_CLK_TOP_APLL2_CK] = "apll2",
	/* divider */
	[MT8196_CLK_TOP_APLL12_DIV_I2SIN0] = "apll12_div_i2sin0",
	[MT8196_CLK_TOP_APLL12_DIV_I2SIN1] = "apll12_div_i2sin1",
	[MT8196_CLK_TOP_APLL12_DIV_FMI2S] = "apll12_div_fmi2s",
	[MT8196_CLK_TOP_APLL12_DIV_TDMOUT_M] = "apll12_div_tdmout_m",
	[MT8196_CLK_TOP_APLL12_DIV_TDMOUT_B] = "apll12_div_tdmout_b",
	/* mux */
	[MT8196_CLK_TOP_ADSP_SEL] = "top_adsp",
};

int mt8196_afe_enable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	int ret;

	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(afe->dev, "failed to enable clk\n");
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt8196_afe_enable_clk);

void mt8196_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	if (clk)
		clk_disable_unprepare(clk);
	else
		dev_err(afe->dev, "NULL clk\n");
}
EXPORT_SYMBOL_GPL(mt8196_afe_disable_clk);

static int mt8196_afe_set_clk_rate(struct mtk_base_afe *afe, struct clk *clk,
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

static unsigned int get_top_cg_reg(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8196_AUDIO_26M_EN_ON:
	case MT8196_AUDIO_F3P25M_EN_ON:
	case MT8196_AUDIO_APLL1_EN_ON:
	case MT8196_AUDIO_APLL2_EN_ON:
		return AUDIO_ENGEN_CON0;
	case MT8196_CG_AUDIO_HOPPING_CK:
	case MT8196_CG_AUDIO_F26M_CK:
	case MT8196_CG_APLL1_CK:
	case MT8196_CG_APLL2_CK:
	case MT8196_PDN_APLL_TUNER2:
	case MT8196_PDN_APLL_TUNER1:
		return AUDIO_TOP_CON4;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_mask(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8196_AUDIO_26M_EN_ON:
		return AUDIO_26M_EN_ON_MASK_SFT;
	case MT8196_AUDIO_F3P25M_EN_ON:
		return AUDIO_F3P25M_EN_ON_MASK_SFT;
	case MT8196_AUDIO_APLL1_EN_ON:
		return AUDIO_APLL1_EN_ON_MASK_SFT;
	case MT8196_AUDIO_APLL2_EN_ON:
		return AUDIO_APLL2_EN_ON_MASK_SFT;
	case MT8196_CG_AUDIO_HOPPING_CK:
		return CG_AUDIO_HOPPING_CK_MASK_SFT;
	case MT8196_CG_AUDIO_F26M_CK:
		return CG_AUDIO_F26M_CK_MASK_SFT;
	case MT8196_CG_APLL1_CK:
		return CG_APLL1_CK_MASK_SFT;
	case MT8196_CG_APLL2_CK:
		return CG_APLL2_CK_MASK_SFT;
	case MT8196_PDN_APLL_TUNER2:
		return PDN_APLL_TUNER2_MASK_SFT;
	case MT8196_PDN_APLL_TUNER1:
		return PDN_APLL_TUNER1_MASK_SFT;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_on_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8196_AUDIO_26M_EN_ON:
	case MT8196_AUDIO_F3P25M_EN_ON:
	case MT8196_AUDIO_APLL1_EN_ON:
	case MT8196_AUDIO_APLL2_EN_ON:
		return get_top_cg_mask(cg_type);
	case MT8196_CG_AUDIO_HOPPING_CK:
	case MT8196_CG_AUDIO_F26M_CK:
	case MT8196_CG_APLL1_CK:
	case MT8196_CG_APLL2_CK:
	case MT8196_PDN_APLL_TUNER2:
	case MT8196_PDN_APLL_TUNER1:
		return 0;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_off_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8196_AUDIO_26M_EN_ON:
	case MT8196_AUDIO_F3P25M_EN_ON:
	case MT8196_AUDIO_APLL1_EN_ON:
	case MT8196_AUDIO_APLL2_EN_ON:
		return 0;
	case MT8196_CG_AUDIO_HOPPING_CK:
	case MT8196_CG_AUDIO_F26M_CK:
	case MT8196_CG_APLL1_CK:
	case MT8196_CG_APLL2_CK:
	case MT8196_PDN_APLL_TUNER2:
	case MT8196_PDN_APLL_TUNER1:
		return get_top_cg_mask(cg_type);
	default:
		return get_top_cg_mask(cg_type);
	}
}

static int mt8196_afe_enable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	int ret;
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_on_val(cg_type);

	if (!afe->regmap) {
		dev_err(afe->dev, "afe regmap is null !!!\n");
		return 0;
	}

	dev_dbg(afe->dev, "reg: 0x%x, mask: 0x%x, val: 0x%x\n", reg, mask, val);

	ret = regmap_update_bits(afe->regmap, reg, mask, val);
	if (ret)
		dev_err(afe->dev, "regmap_update_bits failed: %d\n", ret);

	return ret;
}

static int mt8196_afe_disable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	int ret;
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_off_val(cg_type);

	if (!afe->regmap) {
		dev_err(afe->dev, "afe regmap is null !!!\n");
		return 0;
	}

	dev_dbg(afe->dev, "reg: 0x%x, mask: 0x%x, val: 0x%x\n", reg, mask, val);

	ret = regmap_update_bits(afe->regmap, reg, mask, val);
	if (ret)
		dev_err(afe->dev, "regmap_update_bits failed: %d\n", ret);

	return ret;
}

static int apll1_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	int apll_rate;
	int ret;

	dev_dbg(afe->dev, "enable: %d\n", enable);

	if (enable) {
		apll_rate = mt8196_get_apll_rate(afe, MT8196_APLL1);

		/* 180.6336 / 4 = 45.1584MHz */
		ret = mt8196_afe_enable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG1]);
		if (ret)
			return ret;

		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG1],
					      MT8196_AUD_ENG1_CLK);
		if (ret)
			return ret;

		ret = mt8196_afe_enable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H]);
		if (ret)
			return ret;

		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H],
					      apll_rate);
		if (ret)
			return ret;
	} else {
		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG1],
					      MT8196_AFE_26M);
		if (ret)
			return ret;

		mt8196_afe_disable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG1]);

		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H],
					      MT8196_AFE_26M);
		if (ret)
			return ret;

		mt8196_afe_disable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H]);
	}

	return 0;
}

static int apll2_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	int apll_rate;
	int ret;

	dev_dbg(afe->dev, "enable: %d\n", enable);

	if (enable) {
		apll_rate = mt8196_get_apll_rate(afe, MT8196_APLL2);

		/* 196.608 / 4 = 49.152MHz */
		ret = mt8196_afe_enable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG2]);
		if (ret)
			return ret;

		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG2],
					      MT8196_AUD_ENG2_CLK);
		if (ret)
			return ret;

		ret = mt8196_afe_enable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H]);
		if (ret)
			return ret;

		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H],
					      apll_rate);
		if (ret)
			return ret;
	} else {
		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG2],
					      MT8196_AFE_26M);
		if (ret)
			return ret;

		mt8196_afe_disable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUD_ENG2]);

		ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H],
					      MT8196_AFE_26M);
		if (ret)
			return ret;

		mt8196_afe_disable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H]);
	}

	return 0;
}

int mt8196_apll1_enable(struct mtk_base_afe *afe)
{
	int ret;

	/* setting for APLL */
	apll1_mux_setting(afe, true);

	ret = mt8196_afe_enable_top_cg(afe, MT8196_CG_APLL1_CK);
	if (ret)
		goto err_clk_apll1;

	ret = mt8196_afe_enable_top_cg(afe, MT8196_PDN_APLL_TUNER1);
	if (ret)
		goto err_clk_apll1_tuner;

	/* sel 44.1kHz:1, apll_div:7, upper bound:3 */
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
			   XTAL_EN_128FS_SEL_MASK_SFT | APLL_DIV_MASK_SFT | UPPER_BOUND_MASK_SFT,
			   (0x1 << XTAL_EN_128FS_SEL_SFT) | (7 << APLL_DIV_SFT) |
			   (3 << UPPER_BOUND_SFT));

	/* apll1 freq tuner enable */
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x1 << FREQ_TUNER_EN_SFT);

	/* audio apll1 on */
	mt8196_afe_enable_top_cg(afe, MT8196_AUDIO_APLL1_EN_ON);

	return 0;

err_clk_apll1_tuner:
	mt8196_afe_disable_top_cg(afe, MT8196_PDN_APLL_TUNER1);
err_clk_apll1:
	mt8196_afe_disable_top_cg(afe, MT8196_CG_APLL1_CK);
	return ret;
}

void mt8196_apll1_disable(struct mtk_base_afe *afe)
{
	/* audio apll1 off */
	mt8196_afe_disable_top_cg(afe, MT8196_AUDIO_APLL1_EN_ON);

	/* apll1 freq tuner disable */
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x0);

	mt8196_afe_disable_top_cg(afe, MT8196_PDN_APLL_TUNER1);
	mt8196_afe_disable_top_cg(afe, MT8196_CG_APLL1_CK);
	apll1_mux_setting(afe, false);
}

int mt8196_apll2_enable(struct mtk_base_afe *afe)
{
	int ret;

	/* setting for APLL */
	apll2_mux_setting(afe, true);

	ret = mt8196_afe_enable_top_cg(afe, MT8196_CG_APLL2_CK);
	if (ret)
		goto err_clk_apll2;

	ret = mt8196_afe_enable_top_cg(afe, MT8196_PDN_APLL_TUNER2);
	if (ret)
		goto err_clk_apll2_tuner;

	/* sel 48kHz: 2, apll_div: 7, upper bound: 3*/
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
			   XTAL_EN_128FS_SEL_MASK_SFT | APLL_DIV_MASK_SFT | UPPER_BOUND_MASK_SFT,
			   (0x2 << XTAL_EN_128FS_SEL_SFT) | (7 << APLL_DIV_SFT) |
			   (3 << UPPER_BOUND_SFT));

	/* apll2 freq tuner enable */
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x1 << FREQ_TUNER_EN_SFT);

	/* audio apll2 on */
	mt8196_afe_enable_top_cg(afe, MT8196_AUDIO_APLL2_EN_ON);
	return 0;

err_clk_apll2_tuner:
	mt8196_afe_disable_top_cg(afe, MT8196_PDN_APLL_TUNER2);
err_clk_apll2:
	mt8196_afe_disable_top_cg(afe, MT8196_CG_APLL2_CK);
	return 0;
}

void mt8196_apll2_disable(struct mtk_base_afe *afe)
{
	/* audio apll2 off */
	mt8196_afe_disable_top_cg(afe, MT8196_AUDIO_APLL2_EN_ON);

	/* apll2 freq tuner disable */
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
			   FREQ_TUNER_EN_MASK_SFT,
			   0x0);

	mt8196_afe_disable_top_cg(afe, MT8196_PDN_APLL_TUNER2);
	mt8196_afe_disable_top_cg(afe, MT8196_CG_APLL2_CK);
	apll2_mux_setting(afe, false);
}

int mt8196_get_apll_rate(struct mtk_base_afe *afe, int apll)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	int clk_id = 0;

	if (apll < MT8196_APLL1 || apll > MT8196_APLL2) {
		dev_warn(afe->dev, "invalid clk id %d\n", apll);
		return 0;
	}

	if (apll == MT8196_APLL1)
		clk_id = MT8196_CLK_TOP_APLL1_CK;
	else
		clk_id = MT8196_CLK_TOP_APLL2_CK;

	return clk_get_rate(afe_priv->clk[clk_id]);
}

/* 48K: select APLL2; 44.1k: select APLL1 */
int mt8196_get_apll_by_rate(struct mtk_base_afe *afe, int rate)
{
	return (rate % 8000) ? MT8196_APLL1 : MT8196_APLL2;
}

int mt8196_get_apll_by_name(struct mtk_base_afe *afe, const char *name)
{
	if (strcmp(name, APLL1_W_NAME) == 0)
		return MT8196_APLL1;

	return MT8196_APLL2;
}

static const int mck_div[MT8196_MCK_NUM] = {
	[MT8196_I2SIN0_MCK] = MT8196_CLK_TOP_APLL12_DIV_I2SIN0,
	[MT8196_I2SIN1_MCK] = MT8196_CLK_TOP_APLL12_DIV_I2SIN1,
	[MT8196_FMI2S_MCK] = MT8196_CLK_TOP_APLL12_DIV_FMI2S,
	[MT8196_TDMOUT_MCK] = MT8196_CLK_TOP_APLL12_DIV_TDMOUT_M,
	[MT8196_TDMOUT_BCK] = MT8196_CLK_TOP_APLL12_DIV_TDMOUT_B,
};

int mt8196_mck_enable(struct mtk_base_afe *afe, int mck_id, int rate)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	int div_clk_id;
	int ret;

	dev_dbg(afe->dev, "mck_id: %d, rate: %d\n", mck_id, rate);

	if (mck_id >= MT8196_MCK_NUM || mck_id < 0)
		return -EINVAL;

	div_clk_id = mck_div[mck_id];

	/* enable div, set rate */
	if (div_clk_id < 0) {
		dev_err(afe->dev, "invalid div_clk_id %d\n", div_clk_id);
		return -EINVAL;
	}

	if (div_clk_id == MT8196_CLK_TOP_APLL12_DIV_TDMOUT_B)
		rate *= 16;

	ret = mt8196_afe_enable_clk(afe, afe_priv->clk[div_clk_id]);
	if (ret)
		return ret;

	ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[div_clk_id], rate);
	if (ret)
		return ret;

	return 0;
}

int mt8196_mck_disable(struct mtk_base_afe *afe, int mck_id)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	int div_clk_id;
	int ret;

	dev_dbg(afe->dev, "mck_id: %d.\n", mck_id);

	if (mck_id < 0) {
		dev_err(afe->dev, "mck_id = %d < 0\n", mck_id);
		return -EINVAL;
	}

	div_clk_id = mck_div[mck_id];

	if (div_clk_id < 0) {
		dev_err(afe->dev, "div_clk_id = %d < 0\n",
			div_clk_id);
		return -EINVAL;
	}

	ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[div_clk_id], MT8196_AFE_26M);
	if (ret)
		return ret;

	mt8196_afe_disable_clk(afe, afe_priv->clk[div_clk_id]);

	return 0;
}

int mt8196_afe_enable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* bus clock for AFE external access, like DRAM */
	mt8196_afe_enable_clk(afe, afe_priv->clk[MT8196_CLK_TOP_ADSP_SEL]);

	/* bus clock for AFE internal access, like AFE SRAM */
	mt8196_afe_enable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIOINTBUS]);
	ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIOINTBUS],
				      MT8196_AFE_26M);
	if (ret)
		return ret;

	/* enable audio h clock */
	mt8196_afe_enable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H]);
	ret = mt8196_afe_set_clk_rate(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H],
				      MT8196_AFE_26M);
	if (ret)
		return ret;

	/* AFE hw clock */
	/* IPM2.0: USE HOPPING & 26M */
	/* set in the regmap_register_patch */
	return 0;
}

int mt8196_afe_disable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;

	/* IPM2.0: Use HOPPING & 26M */
	/* set in the regmap_register_patch */

	mt8196_afe_disable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIO_H]);
	mt8196_afe_disable_clk(afe, afe_priv->clk[MT8196_CLK_VLP_MUX_AUDIOINTBUS]);
	mt8196_afe_disable_clk(afe, afe_priv->clk[MT8196_CLK_TOP_ADSP_SEL]);
	return 0;
}

int mt8196_afe_enable_main_clock(struct mtk_base_afe *afe)
{
	mt8196_afe_enable_top_cg(afe, MT8196_AUDIO_26M_EN_ON);
	return 0;
}

int mt8196_afe_disable_main_clock(struct mtk_base_afe *afe)
{
	mt8196_afe_disable_top_cg(afe, MT8196_AUDIO_26M_EN_ON);
	return 0;
}

int mt8196_init_clock(struct mtk_base_afe *afe)
{
	struct mt8196_afe_private *afe_priv = afe->platform_priv;
	int i;

	afe_priv->clk = devm_kcalloc(afe->dev, MT8196_CLK_NUM, sizeof(*afe_priv->clk),
				     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < MT8196_CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_err(afe->dev, "devm_clk_get %s fail\n", aud_clks[i]);
			return PTR_ERR(afe_priv->clk[i]);
		}
	}

	return 0;
}

