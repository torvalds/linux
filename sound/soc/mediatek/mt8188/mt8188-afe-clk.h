/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8188-afe-clk.h  --  MediaTek 8188 afe clock ctrl definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 *         Chun-Chia Chiu <chun-chia.chiu@mediatek.com>
 */

#ifndef _MT8188_AFE_CLK_H_
#define _MT8188_AFE_CLK_H_

/* APLL */
#define APLL1_W_NAME "APLL1"
#define APLL2_W_NAME "APLL2"

enum {
	/* xtal */
	MT8188_CLK_XTAL_26M,
	/* pll */
	MT8188_CLK_APMIXED_APLL1,
	MT8188_CLK_APMIXED_APLL2,
	/* divider */
	MT8188_CLK_TOP_APLL1_D4,
	MT8188_CLK_TOP_APLL2_D4,
	MT8188_CLK_TOP_APLL12_DIV0,
	MT8188_CLK_TOP_APLL12_DIV1,
	MT8188_CLK_TOP_APLL12_DIV2,
	MT8188_CLK_TOP_APLL12_DIV3,
	MT8188_CLK_TOP_APLL12_DIV4,
	MT8188_CLK_TOP_APLL12_DIV9,
	/* mux */
	MT8188_CLK_TOP_A1SYS_HP_SEL,
	MT8188_CLK_TOP_A2SYS_SEL,
	MT8188_CLK_TOP_AUD_IEC_SEL,
	MT8188_CLK_TOP_AUD_INTBUS_SEL,
	MT8188_CLK_TOP_AUDIO_H_SEL,
	MT8188_CLK_TOP_AUDIO_LOCAL_BUS_SEL,
	MT8188_CLK_TOP_DPTX_M_SEL,
	MT8188_CLK_TOP_I2SO1_M_SEL,
	MT8188_CLK_TOP_I2SO2_M_SEL,
	MT8188_CLK_TOP_I2SI1_M_SEL,
	MT8188_CLK_TOP_I2SI2_M_SEL,
	/* clock gate */
	MT8188_CLK_ADSP_AUDIO_26M,
	MT8188_CLK_AUD_AFE,
	MT8188_CLK_AUD_APLL1_TUNER,
	MT8188_CLK_AUD_APLL2_TUNER,
	MT8188_CLK_AUD_TOP0_SPDF,
	MT8188_CLK_AUD_APLL,
	MT8188_CLK_AUD_APLL2,
	MT8188_CLK_AUD_DAC,
	MT8188_CLK_AUD_ADC,
	MT8188_CLK_AUD_DAC_HIRES,
	MT8188_CLK_AUD_A1SYS_HP,
	MT8188_CLK_AUD_ADC_HIRES,
	MT8188_CLK_AUD_I2SIN,
	MT8188_CLK_AUD_TDM_IN,
	MT8188_CLK_AUD_I2S_OUT,
	MT8188_CLK_AUD_TDM_OUT,
	MT8188_CLK_AUD_HDMI_OUT,
	MT8188_CLK_AUD_ASRC11,
	MT8188_CLK_AUD_ASRC12,
	MT8188_CLK_AUD_A1SYS,
	MT8188_CLK_AUD_A2SYS,
	MT8188_CLK_AUD_PCMIF,
	MT8188_CLK_AUD_MEMIF_UL1,
	MT8188_CLK_AUD_MEMIF_UL2,
	MT8188_CLK_AUD_MEMIF_UL3,
	MT8188_CLK_AUD_MEMIF_UL4,
	MT8188_CLK_AUD_MEMIF_UL5,
	MT8188_CLK_AUD_MEMIF_UL6,
	MT8188_CLK_AUD_MEMIF_UL8,
	MT8188_CLK_AUD_MEMIF_UL9,
	MT8188_CLK_AUD_MEMIF_UL10,
	MT8188_CLK_AUD_MEMIF_DL2,
	MT8188_CLK_AUD_MEMIF_DL3,
	MT8188_CLK_AUD_MEMIF_DL6,
	MT8188_CLK_AUD_MEMIF_DL7,
	MT8188_CLK_AUD_MEMIF_DL8,
	MT8188_CLK_AUD_MEMIF_DL10,
	MT8188_CLK_AUD_MEMIF_DL11,
	MT8188_CLK_NUM,
};

enum {
	MT8188_AUD_PLL1,
	MT8188_AUD_PLL2,
	MT8188_AUD_PLL3,
	MT8188_AUD_PLL4,
	MT8188_AUD_PLL5,
	MT8188_AUD_PLL_NUM,
};

enum {
	MT8188_MCK_SEL_26M,
	MT8188_MCK_SEL_APLL1,
	MT8188_MCK_SEL_APLL2,
	MT8188_MCK_SEL_APLL3,
	MT8188_MCK_SEL_APLL4,
	MT8188_MCK_SEL_APLL5,
	MT8188_MCK_SEL_NUM,
};

struct mtk_base_afe;

int mt8188_afe_get_mclk_source_clk_id(int sel);
int mt8188_afe_get_mclk_source_rate(struct mtk_base_afe *afe, int apll);
int mt8188_afe_get_default_mclk_source_by_rate(int rate);
int mt8188_get_apll_by_rate(struct mtk_base_afe *afe, int rate);
int mt8188_get_apll_by_name(struct mtk_base_afe *afe, const char *name);
int mt8188_afe_init_clock(struct mtk_base_afe *afe);
int mt8188_afe_enable_clk(struct mtk_base_afe *afe, struct clk *clk);
void mt8188_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk);
int mt8188_afe_set_clk_rate(struct mtk_base_afe *afe, struct clk *clk,
			    unsigned int rate);
int mt8188_afe_set_clk_parent(struct mtk_base_afe *afe, struct clk *clk,
			      struct clk *parent);
int mt8188_apll1_enable(struct mtk_base_afe *afe);
int mt8188_apll1_disable(struct mtk_base_afe *afe);
int mt8188_apll2_enable(struct mtk_base_afe *afe);
int mt8188_apll2_disable(struct mtk_base_afe *afe);
int mt8188_afe_enable_main_clock(struct mtk_base_afe *afe);
int mt8188_afe_disable_main_clock(struct mtk_base_afe *afe);
int mt8188_afe_enable_reg_rw_clk(struct mtk_base_afe *afe);
int mt8188_afe_disable_reg_rw_clk(struct mtk_base_afe *afe);

#endif
