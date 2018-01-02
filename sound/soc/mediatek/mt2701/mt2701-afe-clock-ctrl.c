/*
 * mt2701-afe-clock-ctrl.c  --  Mediatek 2701 afe clock ctrl
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "mt2701-afe-common.h"
#include "mt2701-afe-clock-ctrl.h"

static const char *const base_clks[] = {
	[MT2701_TOP_AUD_MCLK_SRC0] = "top_audio_mux1_sel",
	[MT2701_TOP_AUD_MCLK_SRC1] = "top_audio_mux2_sel",
	[MT2701_AUDSYS_AFE] = "audio_afe_pd",
	[MT2701_AUDSYS_AFE_CONN] = "audio_afe_conn_pd",
	[MT2701_AUDSYS_A1SYS] = "audio_a1sys_pd",
	[MT2701_AUDSYS_A2SYS] = "audio_a2sys_pd",
};

int mt2701_init_clock(struct mtk_base_afe *afe)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int i;

	for (i = 0; i < MT2701_BASE_CLK_NUM; i++) {
		afe_priv->base_ck[i] = devm_clk_get(afe->dev, base_clks[i]);
		if (IS_ERR(afe_priv->base_ck[i])) {
			dev_err(afe->dev, "failed to get %s\n", base_clks[i]);
			return PTR_ERR(afe_priv->base_ck[i]);
		}
	}

	/* Get I2S related clocks */
	for (i = 0; i < MT2701_I2S_NUM; i++) {
		struct mt2701_i2s_path *i2s_path = &afe_priv->i2s_path[i];
		char name[13];

		snprintf(name, sizeof(name), "i2s%d_src_sel", i);
		i2s_path->sel_ck = devm_clk_get(afe->dev, name);
		if (IS_ERR(i2s_path->sel_ck)) {
			dev_err(afe->dev, "failed to get %s\n", name);
			return PTR_ERR(i2s_path->sel_ck);
		}

		snprintf(name, sizeof(name), "i2s%d_src_div", i);
		i2s_path->div_ck = devm_clk_get(afe->dev, name);
		if (IS_ERR(i2s_path->div_ck)) {
			dev_err(afe->dev, "failed to get %s\n", name);
			return PTR_ERR(i2s_path->div_ck);
		}

		snprintf(name, sizeof(name), "i2s%d_mclk_en", i);
		i2s_path->mclk_ck = devm_clk_get(afe->dev, name);
		if (IS_ERR(i2s_path->mclk_ck)) {
			dev_err(afe->dev, "failed to get %s\n", name);
			return PTR_ERR(i2s_path->mclk_ck);
		}

		snprintf(name, sizeof(name), "i2so%d_hop_ck", i);
		i2s_path->hop_ck[I2S_OUT] = devm_clk_get(afe->dev, name);
		if (IS_ERR(i2s_path->hop_ck[I2S_OUT])) {
			dev_err(afe->dev, "failed to get %s\n", name);
			return PTR_ERR(i2s_path->hop_ck[I2S_OUT]);
		}

		snprintf(name, sizeof(name), "i2si%d_hop_ck", i);
		i2s_path->hop_ck[I2S_IN] = devm_clk_get(afe->dev, name);
		if (IS_ERR(i2s_path->hop_ck[I2S_IN])) {
			dev_err(afe->dev, "failed to get %s\n", name);
			return PTR_ERR(i2s_path->hop_ck[I2S_IN]);
		}

		snprintf(name, sizeof(name), "asrc%d_out_ck", i);
		i2s_path->asrco_ck = devm_clk_get(afe->dev, name);
		if (IS_ERR(i2s_path->asrco_ck)) {
			dev_err(afe->dev, "failed to get %s\n", name);
			return PTR_ERR(i2s_path->asrco_ck);
		}
	}

	/* Some platforms may support BT path */
	afe_priv->mrgif_ck = devm_clk_get(afe->dev, "audio_mrgif_pd");
	if (IS_ERR(afe_priv->mrgif_ck)) {
		if (PTR_ERR(afe_priv->mrgif_ck) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		afe_priv->mrgif_ck = NULL;
	}

	return 0;
}

int mt2701_afe_enable_i2s(struct mtk_base_afe *afe, int id, int dir)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	struct mt2701_i2s_path *i2s_path = &afe_priv->i2s_path[id];
	int ret;

	ret = clk_prepare_enable(i2s_path->asrco_ck);
	if (ret) {
		dev_err(afe->dev, "failed to enable ASRC clock %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(i2s_path->hop_ck[dir]);
	if (ret) {
		dev_err(afe->dev, "failed to enable I2S clock %d\n", ret);
		goto err_hop_ck;
	}

	return 0;

err_hop_ck:
	clk_disable_unprepare(i2s_path->asrco_ck);

	return ret;
}

void mt2701_afe_disable_i2s(struct mtk_base_afe *afe, int id, int dir)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	struct mt2701_i2s_path *i2s_path = &afe_priv->i2s_path[id];

	clk_disable_unprepare(i2s_path->hop_ck[dir]);
	clk_disable_unprepare(i2s_path->asrco_ck);
}

int mt2701_afe_enable_mclk(struct mtk_base_afe *afe, int id)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	struct mt2701_i2s_path *i2s_path = &afe_priv->i2s_path[id];

	return clk_prepare_enable(i2s_path->mclk_ck);
}

void mt2701_afe_disable_mclk(struct mtk_base_afe *afe, int id)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	struct mt2701_i2s_path *i2s_path = &afe_priv->i2s_path[id];

	clk_disable_unprepare(i2s_path->mclk_ck);
}

int mt2701_enable_btmrg_clk(struct mtk_base_afe *afe)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;

	return clk_prepare_enable(afe_priv->mrgif_ck);
}

void mt2701_disable_btmrg_clk(struct mtk_base_afe *afe)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;

	clk_disable_unprepare(afe_priv->mrgif_ck);
}

static int mt2701_afe_enable_audsys(struct mtk_base_afe *afe)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = clk_prepare_enable(afe_priv->base_ck[MT2701_AUDSYS_AFE]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(afe_priv->base_ck[MT2701_AUDSYS_A1SYS]);
	if (ret)
		goto err_audio_a1sys;

	ret = clk_prepare_enable(afe_priv->base_ck[MT2701_AUDSYS_A2SYS]);
	if (ret)
		goto err_audio_a2sys;

	ret = clk_prepare_enable(afe_priv->base_ck[MT2701_AUDSYS_AFE_CONN]);
	if (ret)
		goto err_afe_conn;

	return 0;

err_afe_conn:
	clk_disable_unprepare(afe_priv->base_ck[MT2701_AUDSYS_A2SYS]);
err_audio_a2sys:
	clk_disable_unprepare(afe_priv->base_ck[MT2701_AUDSYS_A1SYS]);
err_audio_a1sys:
	clk_disable_unprepare(afe_priv->base_ck[MT2701_AUDSYS_AFE]);

	return ret;
}

static void mt2701_afe_disable_audsys(struct mtk_base_afe *afe)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;

	clk_disable_unprepare(afe_priv->base_ck[MT2701_AUDSYS_AFE_CONN]);
	clk_disable_unprepare(afe_priv->base_ck[MT2701_AUDSYS_A2SYS]);
	clk_disable_unprepare(afe_priv->base_ck[MT2701_AUDSYS_A1SYS]);
	clk_disable_unprepare(afe_priv->base_ck[MT2701_AUDSYS_AFE]);
}

int mt2701_afe_enable_clock(struct mtk_base_afe *afe)
{
	int ret;

	/* Enable audio system */
	ret = mt2701_afe_enable_audsys(afe);
	if (ret) {
		dev_err(afe->dev, "failed to enable audio system %d\n", ret);
		return ret;
	}

	regmap_update_bits(afe->regmap, ASYS_TOP_CON,
			   ASYS_TOP_CON_ASYS_TIMING_ON,
			   ASYS_TOP_CON_ASYS_TIMING_ON);
	regmap_update_bits(afe->regmap, AFE_DAC_CON0,
			   AFE_DAC_CON0_AFE_ON,
			   AFE_DAC_CON0_AFE_ON);

	/* Configure ASRC */
	regmap_write(afe->regmap, PWR1_ASM_CON1, PWR1_ASM_CON1_INIT_VAL);
	regmap_write(afe->regmap, PWR2_ASM_CON1, PWR2_ASM_CON1_INIT_VAL);

	return 0;
}

int mt2701_afe_disable_clock(struct mtk_base_afe *afe)
{
	regmap_update_bits(afe->regmap, ASYS_TOP_CON,
			   ASYS_TOP_CON_ASYS_TIMING_ON, 0);
	regmap_update_bits(afe->regmap, AFE_DAC_CON0,
			   AFE_DAC_CON0_AFE_ON, 0);

	mt2701_afe_disable_audsys(afe);

	return 0;
}

void mt2701_mclk_configuration(struct mtk_base_afe *afe, int id, int domain,
			       int mclk)
{
	struct mt2701_afe_private *priv = afe->platform_priv;
	struct mt2701_i2s_path *i2s_path = &priv->i2s_path[id];
	int ret;

	/* Set mclk source */
	if (domain == 0)
		ret = clk_set_parent(i2s_path->sel_ck,
				     priv->base_ck[MT2701_TOP_AUD_MCLK_SRC0]);
	else
		ret = clk_set_parent(i2s_path->sel_ck,
				     priv->base_ck[MT2701_TOP_AUD_MCLK_SRC1]);

	if (ret)
		dev_err(afe->dev, "failed to set domain%d mclk source %d\n",
			domain, ret);

	/* Set mclk divider */
	ret = clk_set_rate(i2s_path->div_ck, mclk);
	if (ret)
		dev_err(afe->dev, "failed to set mclk divider %d\n", ret);
}
