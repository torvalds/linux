/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8183-afe-common.h  --  Mediatek 8183 audio driver definitions
 *
 * Copyright (c) 2018 MediaTek Inc.
 * Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>
 */

#ifndef _MT_8183_AFE_COMMON_H_
#define _MT_8183_AFE_COMMON_H_

#include <sound/soc.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include "../common/mtk-base-afe.h"

enum {
	MT8183_MEMIF_DL1,
	MT8183_MEMIF_DL2,
	MT8183_MEMIF_DL3,
	MT8183_MEMIF_VUL12,
	MT8183_MEMIF_VUL2,
	MT8183_MEMIF_AWB,
	MT8183_MEMIF_AWB2,
	MT8183_MEMIF_MOD_DAI,
	MT8183_MEMIF_HDMI,
	MT8183_MEMIF_NUM,
	MT8183_DAI_ADDA = MT8183_MEMIF_NUM,
	MT8183_DAI_PCM_1,
	MT8183_DAI_PCM_2,
	MT8183_DAI_I2S_0,
	MT8183_DAI_I2S_1,
	MT8183_DAI_I2S_2,
	MT8183_DAI_I2S_3,
	MT8183_DAI_I2S_5,
	MT8183_DAI_TDM,
	MT8183_DAI_HOSTLESS_LPBK,
	MT8183_DAI_HOSTLESS_SPEECH,
	MT8183_DAI_NUM,
};

enum {
	MT8183_IRQ_0,
	MT8183_IRQ_1,
	MT8183_IRQ_2,
	MT8183_IRQ_3,
	MT8183_IRQ_4,
	MT8183_IRQ_5,
	MT8183_IRQ_6,
	MT8183_IRQ_7,
	MT8183_IRQ_8,	/* hw bundle to TDM */
	MT8183_IRQ_11,
	MT8183_IRQ_12,
	MT8183_IRQ_NUM,
};

enum {
	MT8183_MTKAIF_PROTOCOL_1 = 0,
	MT8183_MTKAIF_PROTOCOL_2,
	MT8183_MTKAIF_PROTOCOL_2_CLK_P2,
};

/* MCLK */
enum {
	MT8183_I2S0_MCK = 0,
	MT8183_I2S1_MCK,
	MT8183_I2S2_MCK,
	MT8183_I2S3_MCK,
	MT8183_I2S4_MCK,
	MT8183_I2S4_BCK,
	MT8183_I2S5_MCK,
	MT8183_MCK_NUM,
};

struct clk;

struct mt8183_afe_private {
	struct clk **clk;

	int pm_runtime_bypass_reg_ctl;

	/* dai */
	void *dai_priv[MT8183_DAI_NUM];

	/* adda */
	int mtkaif_protocol;
	int mtkaif_calibration_ok;
	int mtkaif_chosen_phase[4];
	int mtkaif_phase_cycle[4];
	int mtkaif_calibration_num_phase;
	int mtkaif_dmic;

	/* mck */
	int mck_rate[MT8183_MCK_NUM];
};

unsigned int mt8183_general_rate_transform(struct device *dev,
					   unsigned int rate);
unsigned int mt8183_rate_transform(struct device *dev,
				   unsigned int rate, int aud_blk);

/* dai register */
int mt8183_dai_adda_register(struct mtk_base_afe *afe);
int mt8183_dai_pcm_register(struct mtk_base_afe *afe);
int mt8183_dai_i2s_register(struct mtk_base_afe *afe);
int mt8183_dai_tdm_register(struct mtk_base_afe *afe);
int mt8183_dai_hostless_register(struct mtk_base_afe *afe);
#endif
