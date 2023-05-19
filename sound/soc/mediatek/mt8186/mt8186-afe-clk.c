// SPDX-License-Identifier: GPL-2.0
//
// mt8186-afe-clk.c  --  Mediatek 8186 afe clock ctrl
//
// Copyright (c) 2022 MediaTek Inc.
// Author: Jiaxin Yu <jiaxin.yu@mediatek.com>

#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "mt8186-afe-common.h"
#include "mt8186-afe-clk.h"
#include "mt8186-audsys-clk.h"

static DEFINE_MUTEX(mutex_request_dram);

static const char *aud_clks[CLK_NUM] = {
	[CLK_AFE] = "aud_afe_clk",
	[CLK_DAC] = "aud_dac_clk",
	[CLK_DAC_PREDIS] = "aud_dac_predis_clk",
	[CLK_ADC] = "aud_adc_clk",
	[CLK_TML] = "aud_tml_clk",
	[CLK_APLL22M] = "aud_apll22m_clk",
	[CLK_APLL24M] = "aud_apll24m_clk",
	[CLK_APLL1_TUNER] = "aud_apll_tuner_clk",
	[CLK_APLL2_TUNER] = "aud_apll2_tuner_clk",
	[CLK_TDM] = "aud_tdm_clk",
	[CLK_NLE] = "aud_nle_clk",
	[CLK_DAC_HIRES] = "aud_dac_hires_clk",
	[CLK_ADC_HIRES] = "aud_adc_hires_clk",
	[CLK_I2S1_BCLK] = "aud_i2s1_bclk",
	[CLK_I2S2_BCLK] = "aud_i2s2_bclk",
	[CLK_I2S3_BCLK] = "aud_i2s3_bclk",
	[CLK_I2S4_BCLK] = "aud_i2s4_bclk",
	[CLK_CONNSYS_I2S_ASRC] = "aud_connsys_i2s_asrc",
	[CLK_GENERAL1_ASRC] = "aud_general1_asrc",
	[CLK_GENERAL2_ASRC] = "aud_general2_asrc",
	[CLK_ADC_HIRES_TML] = "aud_adc_hires_tml",
	[CLK_ADDA6_ADC] = "aud_adda6_adc",
	[CLK_ADDA6_ADC_HIRES] = "aud_adda6_adc_hires",
	[CLK_3RD_DAC] = "aud_3rd_dac",
	[CLK_3RD_DAC_PREDIS] = "aud_3rd_dac_predis",
	[CLK_3RD_DAC_TML] = "aud_3rd_dac_tml",
	[CLK_3RD_DAC_HIRES] = "aud_3rd_dac_hires",
	[CLK_ETDM_IN1_BCLK] = "aud_etdm_in1_bclk",
	[CLK_ETDM_OUT1_BCLK] = "aud_etdm_out1_bclk",
	[CLK_INFRA_SYS_AUDIO] = "aud_infra_clk",
	[CLK_INFRA_AUDIO_26M] = "mtkaif_26m_clk",
	[CLK_MUX_AUDIO] = "top_mux_audio",
	[CLK_MUX_AUDIOINTBUS] = "top_mux_audio_int",
	[CLK_TOP_MAINPLL_D2_D4] = "top_mainpll_d2_d4",
	[CLK_TOP_MUX_AUD_1] = "top_mux_aud_1",
	[CLK_TOP_APLL1_CK] = "top_apll1_ck",
	[CLK_TOP_MUX_AUD_2] = "top_mux_aud_2",
	[CLK_TOP_APLL2_CK] = "top_apll2_ck",
	[CLK_TOP_MUX_AUD_ENG1] = "top_mux_aud_eng1",
	[CLK_TOP_APLL1_D8] = "top_apll1_d8",
	[CLK_TOP_MUX_AUD_ENG2] = "top_mux_aud_eng2",
	[CLK_TOP_APLL2_D8] = "top_apll2_d8",
	[CLK_TOP_MUX_AUDIO_H] = "top_mux_audio_h",
	[CLK_TOP_I2S0_M_SEL] = "top_i2s0_m_sel",
	[CLK_TOP_I2S1_M_SEL] = "top_i2s1_m_sel",
	[CLK_TOP_I2S2_M_SEL] = "top_i2s2_m_sel",
	[CLK_TOP_I2S4_M_SEL] = "top_i2s4_m_sel",
	[CLK_TOP_TDM_M_SEL] = "top_tdm_m_sel",
	[CLK_TOP_APLL12_DIV0] = "top_apll12_div0",
	[CLK_TOP_APLL12_DIV1] = "top_apll12_div1",
	[CLK_TOP_APLL12_DIV2] = "top_apll12_div2",
	[CLK_TOP_APLL12_DIV4] = "top_apll12_div4",
	[CLK_TOP_APLL12_DIV_TDM] = "top_apll12_div_tdm",
	[CLK_CLK26M] = "top_clk26m_clk",
};

int mt8186_set_audio_int_bus_parent(struct mtk_base_afe *afe,
				    int clk_id)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = clk_set_parent(afe_priv->clk[CLK_MUX_AUDIOINTBUS],
			     afe_priv->clk[clk_id]);
	if (ret) {
		dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIOINTBUS],
			aud_clks[clk_id], ret);
		return ret;
	}

	return 0;
}

static int apll1_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

	if (enable) {
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_1]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_1], ret);
			return ret;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_1],
				     afe_priv->clk[CLK_TOP_APLL1_CK]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_1],
				aud_clks[CLK_TOP_APLL1_CK], ret);
			return ret;
		}

		/* 180.6336 / 8 = 22.5792MHz */
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG1], ret);
			return ret;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1],
				     afe_priv->clk[CLK_TOP_APLL1_D8]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG1],
				aud_clks[CLK_TOP_APLL1_D8], ret);
			return ret;
		}
	} else {
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG1],
				aud_clks[CLK_CLK26M], ret);
			return ret;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_ENG1]);

		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_1],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_1],
				aud_clks[CLK_CLK26M], ret);
			return ret;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_1]);
	}

	return 0;
}

static int apll2_mux_setting(struct mtk_base_afe *afe, bool enable)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

	if (enable) {
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_2]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_2], ret);
			return ret;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_2],
				     afe_priv->clk[CLK_TOP_APLL2_CK]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_2],
				aud_clks[CLK_TOP_APLL2_CK], ret);
			return ret;
		}

		/* 196.608 / 8 = 24.576MHz */
		ret = clk_prepare_enable(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG2], ret);
			return ret;
		}
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2],
				     afe_priv->clk[CLK_TOP_APLL2_D8]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG2],
				aud_clks[CLK_TOP_APLL2_D8], ret);
			return ret;
		}
	} else {
		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_ENG2],
				aud_clks[CLK_CLK26M], ret);
			return ret;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_ENG2]);

		ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUD_2],
				     afe_priv->clk[CLK_CLK26M]);
		if (ret) {
			dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
				__func__, aud_clks[CLK_TOP_MUX_AUD_2],
				aud_clks[CLK_CLK26M], ret);
			return ret;
		}
		clk_disable_unprepare(afe_priv->clk[CLK_TOP_MUX_AUD_2]);
	}

	return 0;
}

int mt8186_afe_enable_cgs(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret = 0;
	int i;

	for (i = CLK_I2S1_BCLK; i <= CLK_ETDM_OUT1_BCLK; i++) {
		ret = clk_prepare_enable(afe_priv->clk[i]);
		if (ret) {
			dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
				__func__, aud_clks[i], ret);
			return ret;
		}
	}

	return 0;
}

void mt8186_afe_disable_cgs(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int i;

	for (i = CLK_I2S1_BCLK; i <= CLK_ETDM_OUT1_BCLK; i++)
		clk_disable_unprepare(afe_priv->clk[i]);
}

int mt8186_afe_enable_clock(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret = 0;

	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_SYS_AUDIO]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_SYS_AUDIO], ret);
		goto clk_infra_sys_audio_err;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_INFRA_AUDIO_26M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_INFRA_AUDIO_26M], ret);
		goto clk_infra_audio_26m_err;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_MUX_AUDIO]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIO], ret);
		goto clk_mux_audio_err;
	}
	ret = clk_set_parent(afe_priv->clk[CLK_MUX_AUDIO],
			     afe_priv->clk[CLK_CLK26M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIO],
			aud_clks[CLK_CLK26M], ret);
		goto clk_mux_audio_err;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_MUX_AUDIOINTBUS], ret);
		goto clk_mux_audio_intbus_err;
	}
	ret = mt8186_set_audio_int_bus_parent(afe,
					      CLK_TOP_MAINPLL_D2_D4);
	if (ret)
		goto clk_mux_audio_intbus_parent_err;

	ret = clk_set_parent(afe_priv->clk[CLK_TOP_MUX_AUDIO_H],
			     afe_priv->clk[CLK_TOP_APLL2_CK]);
	if (ret) {
		dev_err(afe->dev, "%s clk_set_parent %s-%s fail %d\n",
			__func__, aud_clks[CLK_TOP_MUX_AUDIO_H],
			aud_clks[CLK_TOP_APLL2_CK], ret);
		goto clk_mux_audio_h_parent_err;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_AFE]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_AFE], ret);
		goto clk_afe_err;
	}

	return 0;

clk_afe_err:
	clk_disable_unprepare(afe_priv->clk[CLK_AFE]);
clk_mux_audio_h_parent_err:
clk_mux_audio_intbus_parent_err:
	mt8186_set_audio_int_bus_parent(afe, CLK_CLK26M);
clk_mux_audio_intbus_err:
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
clk_mux_audio_err:
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIO]);
clk_infra_sys_audio_err:
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUDIO]);
clk_infra_audio_26m_err:
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_AUDIO_26M]);

	return ret;
}

void mt8186_afe_disable_clock(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	clk_disable_unprepare(afe_priv->clk[CLK_AFE]);
	mt8186_set_audio_int_bus_parent(afe, CLK_CLK26M);
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIO]);
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_AUDIO_26M]);
	clk_disable_unprepare(afe_priv->clk[CLK_INFRA_SYS_AUDIO]);
}

int mt8186_afe_suspend_clock(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* set audio int bus to 26M */
	ret = clk_prepare_enable(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	if (ret) {
		dev_info(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			 __func__, aud_clks[CLK_MUX_AUDIOINTBUS], ret);
		goto clk_mux_audio_intbus_err;
	}
	ret = mt8186_set_audio_int_bus_parent(afe, CLK_CLK26M);
	if (ret)
		goto clk_mux_audio_intbus_parent_err;

	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);

	return 0;

clk_mux_audio_intbus_parent_err:
	mt8186_set_audio_int_bus_parent(afe, CLK_TOP_MAINPLL_D2_D4);
clk_mux_audio_intbus_err:
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	return ret;
}

int mt8186_afe_resume_clock(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* set audio int bus to normal working clock */
	ret = clk_prepare_enable(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	if (ret) {
		dev_info(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			 __func__, aud_clks[CLK_MUX_AUDIOINTBUS], ret);
		goto clk_mux_audio_intbus_err;
	}
	ret = mt8186_set_audio_int_bus_parent(afe,
					      CLK_TOP_MAINPLL_D2_D4);
	if (ret)
		goto clk_mux_audio_intbus_parent_err;

	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);

	return 0;

clk_mux_audio_intbus_parent_err:
	mt8186_set_audio_int_bus_parent(afe, CLK_CLK26M);
clk_mux_audio_intbus_err:
	clk_disable_unprepare(afe_priv->clk[CLK_MUX_AUDIOINTBUS]);
	return ret;
}

int mt8186_apll1_enable(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* setting for APLL */
	apll1_mux_setting(afe, true);

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL22M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL22M], ret);
		goto err_clk_apll22m;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL1_TUNER]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL1_TUNER], ret);
		goto err_clk_apll1_tuner;
	}

	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG, 0xfff7, 0x832);
	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG, 0x1, 0x1);

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_22M_ON_MASK_SFT, BIT(AFE_22M_ON_SFT));

	return 0;

err_clk_apll1_tuner:
	clk_disable_unprepare(afe_priv->clk[CLK_APLL1_TUNER]);
err_clk_apll22m:
	clk_disable_unprepare(afe_priv->clk[CLK_APLL22M]);

	return ret;
}

void mt8186_apll1_disable(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_22M_ON_MASK_SFT, 0);

	regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG, 0x1, 0);

	clk_disable_unprepare(afe_priv->clk[CLK_APLL1_TUNER]);
	clk_disable_unprepare(afe_priv->clk[CLK_APLL22M]);

	apll1_mux_setting(afe, false);
}

int mt8186_apll2_enable(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int ret;

	/* setting for APLL */
	apll2_mux_setting(afe, true);

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL24M]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL24M], ret);
		goto err_clk_apll24m;
	}

	ret = clk_prepare_enable(afe_priv->clk[CLK_APLL2_TUNER]);
	if (ret) {
		dev_err(afe->dev, "%s clk_prepare_enable %s fail %d\n",
			__func__, aud_clks[CLK_APLL2_TUNER], ret);
		goto err_clk_apll2_tuner;
	}

	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG, 0xfff7, 0x634);
	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG, 0x1, 0x1);

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_24M_ON_MASK_SFT, BIT(AFE_24M_ON_SFT));

	return 0;

err_clk_apll2_tuner:
	clk_disable_unprepare(afe_priv->clk[CLK_APLL2_TUNER]);
err_clk_apll24m:
	clk_disable_unprepare(afe_priv->clk[CLK_APLL24M]);

	return ret;
}

void mt8186_apll2_disable(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;

	regmap_update_bits(afe->regmap, AFE_HD_ENGEN_ENABLE,
			   AFE_24M_ON_MASK_SFT, 0);

	regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG, 0x1, 0);

	clk_disable_unprepare(afe_priv->clk[CLK_APLL2_TUNER]);
	clk_disable_unprepare(afe_priv->clk[CLK_APLL24M]);

	apll2_mux_setting(afe, false);
}

int mt8186_get_apll_rate(struct mtk_base_afe *afe, int apll)
{
	return (apll == MT8186_APLL1) ? 180633600 : 196608000;
}

int mt8186_get_apll_by_rate(struct mtk_base_afe *afe, int rate)
{
	return ((rate % 8000) == 0) ? MT8186_APLL2 : MT8186_APLL1;
}

int mt8186_get_apll_by_name(struct mtk_base_afe *afe, const char *name)
{
	if (strcmp(name, APLL1_W_NAME) == 0)
		return MT8186_APLL1;

	return MT8186_APLL2;
}

/* mck */
struct mt8186_mck_div {
	u32 m_sel_id;
	u32 div_clk_id;
};

static const struct mt8186_mck_div mck_div[MT8186_MCK_NUM] = {
	[MT8186_I2S0_MCK] = {
		.m_sel_id = CLK_TOP_I2S0_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV0,
	},
	[MT8186_I2S1_MCK] = {
		.m_sel_id = CLK_TOP_I2S1_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV1,
	},
	[MT8186_I2S2_MCK] = {
		.m_sel_id = CLK_TOP_I2S2_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV2,
	},
	[MT8186_I2S4_MCK] = {
		.m_sel_id = CLK_TOP_I2S4_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV4,
	},
	[MT8186_TDM_MCK] = {
		.m_sel_id = CLK_TOP_TDM_M_SEL,
		.div_clk_id = CLK_TOP_APLL12_DIV_TDM,
	},
};

int mt8186_mck_enable(struct mtk_base_afe *afe, int mck_id, int rate)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int apll = mt8186_get_apll_by_rate(afe, rate);
	int apll_clk_id = apll == MT8186_APLL1 ?
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
			__func__, aud_clks[div_clk_id], rate, ret);
		return ret;
	}

	return 0;
}

void mt8186_mck_disable(struct mtk_base_afe *afe, int mck_id)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	int m_sel_id = mck_div[mck_id].m_sel_id;
	int div_clk_id = mck_div[mck_id].div_clk_id;

	clk_disable_unprepare(afe_priv->clk[div_clk_id]);
	if (m_sel_id >= 0)
		clk_disable_unprepare(afe_priv->clk[m_sel_id]);
}

int mt8186_init_clock(struct mtk_base_afe *afe)
{
	struct mt8186_afe_private *afe_priv = afe->platform_priv;
	struct device_node *of_node = afe->dev->of_node;
	int i = 0;

	mt8186_audsys_clk_register(afe);

	afe_priv->clk = devm_kcalloc(afe->dev, CLK_NUM, sizeof(*afe_priv->clk),
				     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_err(afe->dev, "%s devm_clk_get %s fail, ret %ld\n",
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
