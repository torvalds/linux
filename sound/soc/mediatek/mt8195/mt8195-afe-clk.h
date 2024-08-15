/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8195-afe-clk.h  --  Mediatek 8195 afe clock ctrl definition
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 */

#ifndef _MT8195_AFE_CLK_H_
#define _MT8195_AFE_CLK_H_

enum {
	/* xtal */
	MT8195_CLK_XTAL_26M,
	/* divider */
	MT8195_CLK_TOP_APLL1,
	MT8195_CLK_TOP_APLL2,
	MT8195_CLK_TOP_APLL12_DIV0,
	MT8195_CLK_TOP_APLL12_DIV1,
	MT8195_CLK_TOP_APLL12_DIV2,
	MT8195_CLK_TOP_APLL12_DIV3,
	MT8195_CLK_TOP_APLL12_DIV9,
	/* mux */
	MT8195_CLK_TOP_A1SYS_HP_SEL,
	MT8195_CLK_TOP_AUD_INTBUS_SEL,
	MT8195_CLK_TOP_AUDIO_H_SEL,
	MT8195_CLK_TOP_AUDIO_LOCAL_BUS_SEL,
	MT8195_CLK_TOP_DPTX_M_SEL,
	MT8195_CLK_TOP_I2SO1_M_SEL,
	MT8195_CLK_TOP_I2SO2_M_SEL,
	MT8195_CLK_TOP_I2SI1_M_SEL,
	MT8195_CLK_TOP_I2SI2_M_SEL,
	/* clock gate */
	MT8195_CLK_INFRA_AO_AUDIO_26M_B,
	MT8195_CLK_SCP_ADSP_AUDIODSP,
	MT8195_CLK_AUD_AFE,
	MT8195_CLK_AUD_APLL1_TUNER,
	MT8195_CLK_AUD_APLL2_TUNER,
	MT8195_CLK_AUD_APLL,
	MT8195_CLK_AUD_APLL2,
	MT8195_CLK_AUD_DAC,
	MT8195_CLK_AUD_ADC,
	MT8195_CLK_AUD_DAC_HIRES,
	MT8195_CLK_AUD_A1SYS_HP,
	MT8195_CLK_AUD_ADC_HIRES,
	MT8195_CLK_AUD_ADDA6_ADC,
	MT8195_CLK_AUD_ADDA6_ADC_HIRES,
	MT8195_CLK_AUD_I2SIN,
	MT8195_CLK_AUD_TDM_IN,
	MT8195_CLK_AUD_I2S_OUT,
	MT8195_CLK_AUD_TDM_OUT,
	MT8195_CLK_AUD_HDMI_OUT,
	MT8195_CLK_AUD_ASRC11,
	MT8195_CLK_AUD_ASRC12,
	MT8195_CLK_AUD_A1SYS,
	MT8195_CLK_AUD_A2SYS,
	MT8195_CLK_AUD_PCMIF,
	MT8195_CLK_AUD_MEMIF_UL1,
	MT8195_CLK_AUD_MEMIF_UL2,
	MT8195_CLK_AUD_MEMIF_UL3,
	MT8195_CLK_AUD_MEMIF_UL4,
	MT8195_CLK_AUD_MEMIF_UL5,
	MT8195_CLK_AUD_MEMIF_UL6,
	MT8195_CLK_AUD_MEMIF_UL8,
	MT8195_CLK_AUD_MEMIF_UL9,
	MT8195_CLK_AUD_MEMIF_UL10,
	MT8195_CLK_AUD_MEMIF_DL2,
	MT8195_CLK_AUD_MEMIF_DL3,
	MT8195_CLK_AUD_MEMIF_DL6,
	MT8195_CLK_AUD_MEMIF_DL7,
	MT8195_CLK_AUD_MEMIF_DL8,
	MT8195_CLK_AUD_MEMIF_DL10,
	MT8195_CLK_AUD_MEMIF_DL11,
	MT8195_CLK_NUM,
};

enum {
	MT8195_MCK_SEL_26M,
	MT8195_MCK_SEL_APLL1,
	MT8195_MCK_SEL_APLL2,
	MT8195_MCK_SEL_APLL3,
	MT8195_MCK_SEL_APLL4,
	MT8195_MCK_SEL_APLL5,
	MT8195_MCK_SEL_HDMIRX_APLL,
	MT8195_MCK_SEL_NUM,
};

enum {
	MT8195_AUD_PLL1,
	MT8195_AUD_PLL2,
	MT8195_AUD_PLL3,
	MT8195_AUD_PLL4,
	MT8195_AUD_PLL5,
	MT8195_AUD_PLL_NUM,
};

struct mtk_base_afe;

int mt8195_afe_get_mclk_source_clk_id(int sel);
int mt8195_afe_get_mclk_source_rate(struct mtk_base_afe *afe, int apll);
int mt8195_afe_get_default_mclk_source_by_rate(int rate);
int mt8195_afe_init_clock(struct mtk_base_afe *afe);
int mt8195_afe_enable_clk(struct mtk_base_afe *afe, struct clk *clk);
void mt8195_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk);
int mt8195_afe_prepare_clk(struct mtk_base_afe *afe, struct clk *clk);
void mt8195_afe_unprepare_clk(struct mtk_base_afe *afe, struct clk *clk);
int mt8195_afe_enable_clk_atomic(struct mtk_base_afe *afe, struct clk *clk);
void mt8195_afe_disable_clk_atomic(struct mtk_base_afe *afe, struct clk *clk);
int mt8195_afe_set_clk_rate(struct mtk_base_afe *afe, struct clk *clk,
			    unsigned int rate);
int mt8195_afe_set_clk_parent(struct mtk_base_afe *afe, struct clk *clk,
			      struct clk *parent);
int mt8195_afe_enable_main_clock(struct mtk_base_afe *afe);
int mt8195_afe_disable_main_clock(struct mtk_base_afe *afe);
int mt8195_afe_enable_reg_rw_clk(struct mtk_base_afe *afe);
int mt8195_afe_disable_reg_rw_clk(struct mtk_base_afe *afe);

#endif
