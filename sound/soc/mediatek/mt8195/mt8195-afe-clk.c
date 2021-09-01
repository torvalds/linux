// SPDX-License-Identifier: GPL-2.0
/*
 * mt8195-afe-clk.c  --  Mediatek 8195 afe clock ctrl
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 */

#include <linux/clk.h>

#include "mt8195-afe-common.h"
#include "mt8195-afe-clk.h"
#include "mt8195-reg.h"
#include "mt8195-audsys-clk.h"

static const char *aud_clks[MT8195_CLK_NUM] = {
	/* xtal */
	[MT8195_CLK_XTAL_26M] = "clk26m",
	/* divider */
	[MT8195_CLK_TOP_APLL1] = "apll1_ck",
	[MT8195_CLK_TOP_APLL2] = "apll2_ck",
	[MT8195_CLK_TOP_APLL12_DIV0] = "apll12_div0",
	[MT8195_CLK_TOP_APLL12_DIV1] = "apll12_div1",
	[MT8195_CLK_TOP_APLL12_DIV2] = "apll12_div2",
	[MT8195_CLK_TOP_APLL12_DIV3] = "apll12_div3",
	[MT8195_CLK_TOP_APLL12_DIV9] = "apll12_div9",
	/* mux */
	[MT8195_CLK_TOP_A1SYS_HP_SEL] = "a1sys_hp_sel",
	[MT8195_CLK_TOP_AUD_INTBUS_SEL] = "aud_intbus_sel",
	[MT8195_CLK_TOP_AUDIO_H_SEL] = "audio_h_sel",
	[MT8195_CLK_TOP_AUDIO_LOCAL_BUS_SEL] = "audio_local_bus_sel",
	[MT8195_CLK_TOP_DPTX_M_SEL] = "dptx_m_sel",
	[MT8195_CLK_TOP_I2SO1_M_SEL] = "i2so1_m_sel",
	[MT8195_CLK_TOP_I2SO2_M_SEL] = "i2so2_m_sel",
	[MT8195_CLK_TOP_I2SI1_M_SEL] = "i2si1_m_sel",
	[MT8195_CLK_TOP_I2SI2_M_SEL] = "i2si2_m_sel",
	/* clock gate */
	[MT8195_CLK_INFRA_AO_AUDIO_26M_B] = "infra_ao_audio_26m_b",
	[MT8195_CLK_SCP_ADSP_AUDIODSP] = "scp_adsp_audiodsp",
	/* afe clock gate */
	[MT8195_CLK_AUD_AFE] = "aud_afe",
	[MT8195_CLK_AUD_APLL] = "aud_apll",
	[MT8195_CLK_AUD_APLL2] = "aud_apll2",
	[MT8195_CLK_AUD_DAC] = "aud_dac",
	[MT8195_CLK_AUD_ADC] = "aud_adc",
	[MT8195_CLK_AUD_DAC_HIRES] = "aud_dac_hires",
	[MT8195_CLK_AUD_A1SYS_HP] = "aud_a1sys_hp",
	[MT8195_CLK_AUD_ADC_HIRES] = "aud_adc_hires",
	[MT8195_CLK_AUD_ADDA6_ADC] = "aud_adda6_adc",
	[MT8195_CLK_AUD_ADDA6_ADC_HIRES] = "aud_adda6_adc_hires",
	[MT8195_CLK_AUD_I2SIN] = "aud_i2sin",
	[MT8195_CLK_AUD_TDM_IN] = "aud_tdm_in",
	[MT8195_CLK_AUD_I2S_OUT] = "aud_i2s_out",
	[MT8195_CLK_AUD_TDM_OUT] = "aud_tdm_out",
	[MT8195_CLK_AUD_HDMI_OUT] = "aud_hdmi_out",
	[MT8195_CLK_AUD_ASRC11] = "aud_asrc11",
	[MT8195_CLK_AUD_ASRC12] = "aud_asrc12",
	[MT8195_CLK_AUD_A1SYS] = "aud_a1sys",
	[MT8195_CLK_AUD_A2SYS] = "aud_a2sys",
	[MT8195_CLK_AUD_PCMIF] = "aud_pcmif",
	[MT8195_CLK_AUD_MEMIF_UL1] = "aud_memif_ul1",
	[MT8195_CLK_AUD_MEMIF_UL2] = "aud_memif_ul2",
	[MT8195_CLK_AUD_MEMIF_UL3] = "aud_memif_ul3",
	[MT8195_CLK_AUD_MEMIF_UL4] = "aud_memif_ul4",
	[MT8195_CLK_AUD_MEMIF_UL5] = "aud_memif_ul5",
	[MT8195_CLK_AUD_MEMIF_UL6] = "aud_memif_ul6",
	[MT8195_CLK_AUD_MEMIF_UL8] = "aud_memif_ul8",
	[MT8195_CLK_AUD_MEMIF_UL9] = "aud_memif_ul9",
	[MT8195_CLK_AUD_MEMIF_UL10] = "aud_memif_ul10",
	[MT8195_CLK_AUD_MEMIF_DL2] = "aud_memif_dl2",
	[MT8195_CLK_AUD_MEMIF_DL3] = "aud_memif_dl3",
	[MT8195_CLK_AUD_MEMIF_DL6] = "aud_memif_dl6",
	[MT8195_CLK_AUD_MEMIF_DL7] = "aud_memif_dl7",
	[MT8195_CLK_AUD_MEMIF_DL8] = "aud_memif_dl8",
	[MT8195_CLK_AUD_MEMIF_DL10] = "aud_memif_dl10",
	[MT8195_CLK_AUD_MEMIF_DL11] = "aud_memif_dl11",
};

int mt8195_afe_get_mclk_source_clk_id(int sel)
{
	switch (sel) {
	case MT8195_MCK_SEL_26M:
		return MT8195_CLK_XTAL_26M;
	case MT8195_MCK_SEL_APLL1:
		return MT8195_CLK_TOP_APLL1;
	case MT8195_MCK_SEL_APLL2:
		return MT8195_CLK_TOP_APLL2;
	default:
		return -EINVAL;
	}
}

int mt8195_afe_get_mclk_source_rate(struct mtk_base_afe *afe, int apll)
{
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int clk_id = mt8195_afe_get_mclk_source_clk_id(apll);

	if (clk_id < 0) {
		dev_dbg(afe->dev, "invalid clk id\n");
		return 0;
	}

	return clk_get_rate(afe_priv->clk[clk_id]);
}

int mt8195_afe_get_default_mclk_source_by_rate(int rate)
{
	return ((rate % 8000) == 0) ?
		MT8195_MCK_SEL_APLL1 : MT8195_MCK_SEL_APLL2;
}

int mt8195_afe_init_clock(struct mtk_base_afe *afe)
{
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int i;

	mt8195_audsys_clk_register(afe);

	afe_priv->clk =
		devm_kcalloc(afe->dev, MT8195_CLK_NUM, sizeof(*afe_priv->clk),
			     GFP_KERNEL);
	if (!afe_priv->clk)
		return -ENOMEM;

	for (i = 0; i < MT8195_CLK_NUM; i++) {
		afe_priv->clk[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe_priv->clk[i])) {
			dev_dbg(afe->dev, "%s(), devm_clk_get %s fail, ret %ld\n",
				__func__, aud_clks[i],
				PTR_ERR(afe_priv->clk[i]));
			return PTR_ERR(afe_priv->clk[i]);
		}
	}

	return 0;
}

void mt8195_afe_deinit_clock(struct mtk_base_afe *afe)
{
	mt8195_audsys_clk_unregister(afe);
}

int mt8195_afe_enable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	int ret;

	if (clk) {
		ret = clk_prepare_enable(clk);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to enable clk\n",
				__func__);
			return ret;
		}
	} else {
		dev_dbg(afe->dev, "NULL clk\n");
	}
	return 0;
}
EXPORT_SYMBOL_GPL(mt8195_afe_enable_clk);

void mt8195_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	if (clk)
		clk_disable_unprepare(clk);
	else
		dev_dbg(afe->dev, "NULL clk\n");
}
EXPORT_SYMBOL_GPL(mt8195_afe_disable_clk);

int mt8195_afe_prepare_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	int ret;

	if (clk) {
		ret = clk_prepare(clk);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to prepare clk\n",
				__func__);
			return ret;
		}
	} else {
		dev_dbg(afe->dev, "NULL clk\n");
	}
	return 0;
}

void mt8195_afe_unprepare_clk(struct mtk_base_afe *afe, struct clk *clk)
{
	if (clk)
		clk_unprepare(clk);
	else
		dev_dbg(afe->dev, "NULL clk\n");
}

int mt8195_afe_enable_clk_atomic(struct mtk_base_afe *afe, struct clk *clk)
{
	int ret;

	if (clk) {
		ret = clk_enable(clk);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to clk enable\n",
				__func__);
			return ret;
		}
	} else {
		dev_dbg(afe->dev, "NULL clk\n");
	}
	return 0;
}

void mt8195_afe_disable_clk_atomic(struct mtk_base_afe *afe, struct clk *clk)
{
	if (clk)
		clk_disable(clk);
	else
		dev_dbg(afe->dev, "NULL clk\n");
}

int mt8195_afe_set_clk_rate(struct mtk_base_afe *afe, struct clk *clk,
			    unsigned int rate)
{
	int ret;

	if (clk) {
		ret = clk_set_rate(clk, rate);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to set clk rate\n",
				__func__);
			return ret;
		}
	}

	return 0;
}

int mt8195_afe_set_clk_parent(struct mtk_base_afe *afe, struct clk *clk,
			      struct clk *parent)
{
	int ret;

	if (clk && parent) {
		ret = clk_set_parent(clk, parent);
		if (ret) {
			dev_dbg(afe->dev, "%s(), failed to set clk parent\n",
				__func__);
			return ret;
		}
	}

	return 0;
}

static unsigned int get_top_cg_reg(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8195_TOP_CG_A1SYS_TIMING:
	case MT8195_TOP_CG_A2SYS_TIMING:
	case MT8195_TOP_CG_26M_TIMING:
		return ASYS_TOP_CON;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_mask(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8195_TOP_CG_A1SYS_TIMING:
		return ASYS_TOP_CON_A1SYS_TIMING_ON;
	case MT8195_TOP_CG_A2SYS_TIMING:
		return ASYS_TOP_CON_A2SYS_TIMING_ON;
	case MT8195_TOP_CG_26M_TIMING:
		return ASYS_TOP_CON_26M_TIMING_ON;
	default:
		return 0;
	}
}

static unsigned int get_top_cg_on_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8195_TOP_CG_A1SYS_TIMING:
	case MT8195_TOP_CG_A2SYS_TIMING:
	case MT8195_TOP_CG_26M_TIMING:
		return get_top_cg_mask(cg_type);
	default:
		return 0;
	}
}

static unsigned int get_top_cg_off_val(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8195_TOP_CG_A1SYS_TIMING:
	case MT8195_TOP_CG_A2SYS_TIMING:
	case MT8195_TOP_CG_26M_TIMING:
		return 0;
	default:
		return get_top_cg_mask(cg_type);
	}
}

static int mt8195_afe_enable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_on_val(cg_type);

	regmap_update_bits(afe->regmap, reg, mask, val);
	return 0;
}

static int mt8195_afe_disable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type)
{
	unsigned int reg = get_top_cg_reg(cg_type);
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned int val = get_top_cg_off_val(cg_type);

	regmap_update_bits(afe->regmap, reg, mask, val);
	return 0;
}

int mt8195_afe_enable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int i;
	unsigned int clk_array[] = {
		MT8195_CLK_SCP_ADSP_AUDIODSP, /* bus clock for infra */
		MT8195_CLK_TOP_AUDIO_H_SEL, /* clock for ADSP bus */
		MT8195_CLK_TOP_AUDIO_LOCAL_BUS_SEL, /* bus clock for DRAM access */
		MT8195_CLK_TOP_AUD_INTBUS_SEL, /* bus clock for AFE SRAM access */
		MT8195_CLK_INFRA_AO_AUDIO_26M_B, /* audio 26M clock */
		MT8195_CLK_AUD_AFE, /* AFE HW master switch */
		MT8195_CLK_AUD_A1SYS_HP, /* AFE HW clock*/
		MT8195_CLK_AUD_A1SYS, /* AFE HW clock */
	};

	for (i = 0; i < ARRAY_SIZE(clk_array); i++)
		mt8195_afe_enable_clk(afe, afe_priv->clk[clk_array[i]]);

	return 0;
}

int mt8195_afe_disable_reg_rw_clk(struct mtk_base_afe *afe)
{
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int i;
	unsigned int clk_array[] = {
		MT8195_CLK_AUD_A1SYS,
		MT8195_CLK_AUD_A1SYS_HP,
		MT8195_CLK_AUD_AFE,
		MT8195_CLK_INFRA_AO_AUDIO_26M_B,
		MT8195_CLK_TOP_AUD_INTBUS_SEL,
		MT8195_CLK_TOP_AUDIO_LOCAL_BUS_SEL,
		MT8195_CLK_TOP_AUDIO_H_SEL,
		MT8195_CLK_SCP_ADSP_AUDIODSP,
	};

	for (i = 0; i < ARRAY_SIZE(clk_array); i++)
		mt8195_afe_disable_clk(afe, afe_priv->clk[clk_array[i]]);

	return 0;
}

static int mt8195_afe_enable_afe_on(struct mtk_base_afe *afe)
{
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x1);
	return 0;
}

static int mt8195_afe_disable_afe_on(struct mtk_base_afe *afe)
{
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x0);
	return 0;
}

static int mt8195_afe_enable_timing_sys(struct mtk_base_afe *afe)
{
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int i;
	unsigned int clk_array[] = {
		MT8195_CLK_AUD_A1SYS,
		MT8195_CLK_AUD_A2SYS,
	};
	unsigned int cg_array[] = {
		MT8195_TOP_CG_A1SYS_TIMING,
		MT8195_TOP_CG_A2SYS_TIMING,
		MT8195_TOP_CG_26M_TIMING,
	};

	for (i = 0; i < ARRAY_SIZE(clk_array); i++)
		mt8195_afe_enable_clk(afe, afe_priv->clk[clk_array[i]]);

	for (i = 0; i < ARRAY_SIZE(cg_array); i++)
		mt8195_afe_enable_top_cg(afe, cg_array[i]);

	return 0;
}

static int mt8195_afe_disable_timing_sys(struct mtk_base_afe *afe)
{
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int i;
	unsigned int clk_array[] = {
		MT8195_CLK_AUD_A2SYS,
		MT8195_CLK_AUD_A1SYS,
	};
	unsigned int cg_array[] = {
		MT8195_TOP_CG_26M_TIMING,
		MT8195_TOP_CG_A2SYS_TIMING,
		MT8195_TOP_CG_A1SYS_TIMING,
	};

	for (i = 0; i < ARRAY_SIZE(cg_array); i++)
		mt8195_afe_disable_top_cg(afe, cg_array[i]);

	for (i = 0; i < ARRAY_SIZE(clk_array); i++)
		mt8195_afe_disable_clk(afe, afe_priv->clk[clk_array[i]]);

	return 0;
}

int mt8195_afe_enable_main_clock(struct mtk_base_afe *afe)
{
	mt8195_afe_enable_timing_sys(afe);

	mt8195_afe_enable_afe_on(afe);

	return 0;
}

int mt8195_afe_disable_main_clock(struct mtk_base_afe *afe)
{
	mt8195_afe_disable_afe_on(afe);

	mt8195_afe_disable_timing_sys(afe);

	return 0;
}
