// SPDX-License-Identifier: GPL-2.0
//
// mt6797-afe-clk.c  --  Mediatek 6797 afe clock ctrl
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/clk.h>

#include "mt6797-afe-common.h"
#include "mt6797-afe-clk.h"

enum {
	CLK_INFRA_SYS_AUD,
	CLK_INFRA_SYS_AUD_26M,
	CLK_TOP_MUX_AUD,
	CLK_TOP_MUX_AUD_BUS,
	CLK_TOP_SYSPLL3_D4,
	CLK_TOP_SYSPLL1_D4,
	CLK_CLK26M,
	CLK_NUM
};

static const char *aud_clks[CLK_NUM] = {
	[CLK_INFRA_SYS_AUD] = "infra_sys_audio_clk",
	[CLK_INFRA_SYS_AUD_26M] = "infra_sys_audio_26m",
	[CLK_TOP_MUX_AUD] = "top_mux_audio",
	[CLK_TOP_MUX_AUD_BUS] = "top_mux_aud_intbus",
	[CLK_TOP_SYSPLL3_D4] = "top_sys_pll3_d4",
	[CLK_TOP_SYSPLL1_D4] = "top_sys_pll1_d4",
	[CLK_CLK26M] = "top_clk26m_clk",
};

int mt6797_init_clock(struct mtk_base_afe *afe)
{
	struct mt6797_afe_private *afe_priv = afe->platform_priv;
	int i;

	afe_priv->clk = devm_kcalloc(afe->dev, CLK_NUM, sizeof(*afe_priv->clk),
				     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_err(afe->dev, "%s(), devm_clk_get %s fail, ret %ld\n",
				__func__, aud_clks[i],
				PTR_ERR(afe_priv->clk[i]));
			return PTR_ERR(afe_priv->clk[i]);
		}
	}

	return 0;
}

int mt6797_afe_enable_clock(struct mtk_base_afe *afe)
{
	struct mt6797_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_SYS_AUD]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_SYS_AUD], ret);
		goto CLK_INFRA_SYS_AUDIO_ERR;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_SYS_AUD_26M]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_SYS_AUD_26M], ret);
		goto CLK_INFRA_SYS_AUD_26M_ERR;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_TOP_MUX_AUD], ret);
		goto CLK_MUX_AUDIO_ERR;
	}

	ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD],
			     afe_priv->clk[CLK_CLK26M]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_TOP_MUX_AUD],
			aud_clks[CLK_CLK26M], ret);
		goto CLK_MUX_AUDIO_ERR;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_BUS]);
	if (ret) {
		dev_err(afe->dev, "%s(), clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_TOP_MUX_AUD_BUS], ret);
		goto CLK_MUX_AUDIO_INTBUS_ERR;
	}

	return ret;

CLK_MUX_AUDIO_INTBUS_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_BUS]);
CLK_MUX_AUDIO_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD]);
CLK_INFRA_SYS_AUD_26M_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD_26M]);
CLK_INFRA_SYS_AUDIO_ERR:
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD]);

	return 0;
}

int mt6797_afe_disable_clock(struct mtk_base_afe *afe)
{
	struct mt6797_afe_private *afe_priv = afe->platform_priv;

	clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_BUS]);
	clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD]);
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD_26M]);
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUD]);

	return 0;
}
