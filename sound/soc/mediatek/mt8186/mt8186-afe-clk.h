/* SPDX-License-Identifier: GPL-2.0
 *
 * mt8186-afe-clk.h  --  Mediatek 8186 afe clock ctrl definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Jiaxin Yu <jiaxin.yu@mediatek.com>
 */

#ifndef _MT8186_AFE_CLOCK_CTRL_H_
#define _MT8186_AFE_CLOCK_CTRL_H_

#define PERI_BUS_DCM_CTRL	0x74

/* APLL */
#define APLL1_W_NAME "APLL1"
#define APLL2_W_NAME "APLL2"
enum {
	MT8186_APLL1 = 0,
	MT8186_APLL2,
};

enum {
	CLK_AFE = 0,
	CLK_DAC,
	CLK_DAC_PREDIS,
	CLK_ADC,
	CLK_TML,
	CLK_APLL22M,
	CLK_APLL24M,
	CLK_APLL1_TUNER,
	CLK_APLL2_TUNER,
	CLK_TDM,
	CLK_NLE,
	CLK_DAC_HIRES,
	CLK_ADC_HIRES,
	CLK_I2S1_BCLK,
	CLK_I2S2_BCLK,
	CLK_I2S3_BCLK,
	CLK_I2S4_BCLK,
	CLK_CONNSYS_I2S_ASRC,
	CLK_GENERAL1_ASRC,
	CLK_GENERAL2_ASRC,
	CLK_ADC_HIRES_TML,
	CLK_ADDA6_ADC,
	CLK_ADDA6_ADC_HIRES,
	CLK_3RD_DAC,
	CLK_3RD_DAC_PREDIS,
	CLK_3RD_DAC_TML,
	CLK_3RD_DAC_HIRES,
	CLK_ETDM_IN1_BCLK,
	CLK_ETDM_OUT1_BCLK,
	CLK_INFRA_SYS_AUDIO,
	CLK_INFRA_AUDIO_26M,
	CLK_MUX_AUDIO,
	CLK_MUX_AUDIOINTBUS,
	CLK_TOP_MAINPLL_D2_D4,
	/* apll related mux */
	CLK_TOP_MUX_AUD_1,
	CLK_TOP_APLL1_CK,
	CLK_TOP_MUX_AUD_2,
	CLK_TOP_APLL2_CK,
	CLK_TOP_MUX_AUD_ENG1,
	CLK_TOP_APLL1_D8,
	CLK_TOP_MUX_AUD_ENG2,
	CLK_TOP_APLL2_D8,
	CLK_TOP_MUX_AUDIO_H,
	CLK_TOP_I2S0_M_SEL,
	CLK_TOP_I2S1_M_SEL,
	CLK_TOP_I2S2_M_SEL,
	CLK_TOP_I2S4_M_SEL,
	CLK_TOP_TDM_M_SEL,
	CLK_TOP_APLL12_DIV0,
	CLK_TOP_APLL12_DIV1,
	CLK_TOP_APLL12_DIV2,
	CLK_TOP_APLL12_DIV4,
	CLK_TOP_APLL12_DIV_TDM,
	CLK_CLK26M,
	CLK_NUM
};

struct mtk_base_afe;
int mt8186_set_audio_int_bus_parent(struct mtk_base_afe *afe, int clk_id);
int mt8186_init_clock(struct mtk_base_afe *afe);
void mt8186_deinit_clock(void *priv);
int mt8186_afe_enable_cgs(struct mtk_base_afe *afe);
void mt8186_afe_disable_cgs(struct mtk_base_afe *afe);
int mt8186_afe_enable_clock(struct mtk_base_afe *afe);
void mt8186_afe_disable_clock(struct mtk_base_afe *afe);
int mt8186_afe_suspend_clock(struct mtk_base_afe *afe);
int mt8186_afe_resume_clock(struct mtk_base_afe *afe);

int mt8186_apll1_enable(struct mtk_base_afe *afe);
void mt8186_apll1_disable(struct mtk_base_afe *afe);

int mt8186_apll2_enable(struct mtk_base_afe *afe);
void mt8186_apll2_disable(struct mtk_base_afe *afe);

int mt8186_get_apll_rate(struct mtk_base_afe *afe, int apll);
int mt8186_get_apll_by_rate(struct mtk_base_afe *afe, int rate);
int mt8186_get_apll_by_name(struct mtk_base_afe *afe, const char *name);

/* these will be replaced by using CCF */
int mt8186_mck_enable(struct mtk_base_afe *afe, int mck_id, int rate);
void mt8186_mck_disable(struct mtk_base_afe *afe, int mck_id);

#endif
