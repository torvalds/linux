/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8189-afe-clk.h  --  Mediatek 8189 afe clock ctrl definition
 *
 * Copyright (c) 2025 MediaTek Inc.
 * Author: Darren Ye <darren.ye@mediatek.com>
 */

#ifndef _MT8189_AFE_CLOCK_CTRL_H_
#define _MT8189_AFE_CLOCK_CTRL_H_

/* APLL */
#define APLL1_W_NAME "APLL1"
#define APLL2_W_NAME "APLL2"

enum {
	MT8189_APLL1,
	MT8189_APLL2,
};

enum {
	MT8189_CLK_TOP_MUX_AUDIOINTBUS,
	MT8189_CLK_TOP_MUX_AUD_ENG1,
	MT8189_CLK_TOP_MUX_AUD_ENG2,
	MT8189_CLK_TOP_MUX_AUDIO_H,
	/* pll */
	MT8189_CLK_TOP_APLL1_CK,
	MT8189_CLK_TOP_APLL2_CK,
	/* divider */
	MT8189_CLK_TOP_APLL1_D4,
	MT8189_CLK_TOP_APLL2_D4,
	MT8189_CLK_TOP_APLL12_DIV_I2SIN0,
	MT8189_CLK_TOP_APLL12_DIV_I2SIN1,
	MT8189_CLK_TOP_APLL12_DIV_I2SOUT0,
	MT8189_CLK_TOP_APLL12_DIV_I2SOUT1,
	MT8189_CLK_TOP_APLL12_DIV_FMI2S,
	MT8189_CLK_TOP_APLL12_DIV_TDMOUT_M,
	MT8189_CLK_TOP_APLL12_DIV_TDMOUT_B,
	/* mux */
	MT8189_CLK_TOP_MUX_AUD_1,
	MT8189_CLK_TOP_MUX_AUD_2,
	MT8189_CLK_TOP_I2SIN0_M_SEL,
	MT8189_CLK_TOP_I2SIN1_M_SEL,
	MT8189_CLK_TOP_I2SOUT0_M_SEL,
	MT8189_CLK_TOP_I2SOUT1_M_SEL,
	MT8189_CLK_TOP_FMI2S_M_SEL,
	MT8189_CLK_TOP_TDMOUT_M_SEL,
	/* top 26m */
	MT8189_CLK_TOP_CLK26M,
	/* peri */
	MT8189_CLK_PERAO_AUDIO_SLV_CK_PERI,
	MT8189_CLK_PERAO_AUDIO_MST_CK_PERI,
	MT8189_CLK_PERAO_INTBUS_CK_PERI,
	MT8189_CLK_NUM,
};

struct mtk_base_afe;

int mt8189_mck_enable(struct mtk_base_afe *afe, int mck_id, int rate);
int mt8189_mck_disable(struct mtk_base_afe *afe, int mck_id);
int mt8189_get_apll_rate(struct mtk_base_afe *afe, int apll);
int mt8189_get_apll_by_rate(struct mtk_base_afe *afe, int rate);
int mt8189_get_apll_by_name(struct mtk_base_afe *afe, const char *name);
int mt8189_init_clock(struct mtk_base_afe *afe);
int mt8189_afe_enable_clk(struct mtk_base_afe *afe, struct clk *clk);
void mt8189_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk);
int mt8189_apll1_enable(struct mtk_base_afe *afe);
void mt8189_apll1_disable(struct mtk_base_afe *afe);
int mt8189_apll2_enable(struct mtk_base_afe *afe);
void mt8189_apll2_disable(struct mtk_base_afe *afe);
int mt8189_afe_enable_main_clock(struct mtk_base_afe *afe);
void mt8189_afe_disable_main_clock(struct mtk_base_afe *afe);
int mt8189_afe_enable_reg_rw_clk(struct mtk_base_afe *afe);
int mt8189_afe_disable_reg_rw_clk(struct mtk_base_afe *afe);

#endif
