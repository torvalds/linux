/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8192-afe-common.h  --  Mediatek 8192 audio driver definitions
 *
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Shane Chien <shane.chien@mediatek.com>
 */

#ifndef _MT_8192_AFE_COMMON_H_
#define _MT_8192_AFE_COMMON_H_

#include <linux/list.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "../common/mtk-base-afe.h"
#include "mt8192-reg.h"

enum {
	MT8192_MEMIF_DL1,
	MT8192_MEMIF_DL12,
	MT8192_MEMIF_DL2,
	MT8192_MEMIF_DL3,
	MT8192_MEMIF_DL4,
	MT8192_MEMIF_DL5,
	MT8192_MEMIF_DL6,
	MT8192_MEMIF_DL7,
	MT8192_MEMIF_DL8,
	MT8192_MEMIF_DL9,
	MT8192_MEMIF_DAI,
	MT8192_MEMIF_DAI2,
	MT8192_MEMIF_MOD_DAI,
	MT8192_MEMIF_VUL12,
	MT8192_MEMIF_VUL2,
	MT8192_MEMIF_VUL3,
	MT8192_MEMIF_VUL4,
	MT8192_MEMIF_VUL5,
	MT8192_MEMIF_VUL6,
	MT8192_MEMIF_AWB,
	MT8192_MEMIF_AWB2,
	MT8192_MEMIF_HDMI,
	MT8192_MEMIF_NUM,
	MT8192_DAI_ADDA = MT8192_MEMIF_NUM,
	MT8192_DAI_ADDA_CH34,
	MT8192_DAI_AP_DMIC,
	MT8192_DAI_AP_DMIC_CH34,
	MT8192_DAI_VOW,
	MT8192_DAI_CONNSYS_I2S,
	MT8192_DAI_I2S_0,
	MT8192_DAI_I2S_1,
	MT8192_DAI_I2S_2,
	MT8192_DAI_I2S_3,
	MT8192_DAI_I2S_5,
	MT8192_DAI_I2S_6,
	MT8192_DAI_I2S_7,
	MT8192_DAI_I2S_8,
	MT8192_DAI_I2S_9,
	MT8192_DAI_HW_GAIN_1,
	MT8192_DAI_HW_GAIN_2,
	MT8192_DAI_SRC_1,
	MT8192_DAI_SRC_2,
	MT8192_DAI_PCM_1,
	MT8192_DAI_PCM_2,
	MT8192_DAI_TDM,
	MT8192_DAI_NUM,
};

enum {
	MT8192_IRQ_0,
	MT8192_IRQ_1,
	MT8192_IRQ_2,
	MT8192_IRQ_3,
	MT8192_IRQ_4,
	MT8192_IRQ_5,
	MT8192_IRQ_6,
	MT8192_IRQ_7,
	MT8192_IRQ_8,
	MT8192_IRQ_9,
	MT8192_IRQ_10,
	MT8192_IRQ_11,
	MT8192_IRQ_12,
	MT8192_IRQ_13,
	MT8192_IRQ_14,
	MT8192_IRQ_15,
	MT8192_IRQ_16,
	MT8192_IRQ_17,
	MT8192_IRQ_18,
	MT8192_IRQ_19,
	MT8192_IRQ_20,
	MT8192_IRQ_21,
	MT8192_IRQ_22,
	MT8192_IRQ_23,
	MT8192_IRQ_24,
	MT8192_IRQ_25,
	MT8192_IRQ_26,
	MT8192_IRQ_31,	/* used only for TDM */
	MT8192_IRQ_NUM,
};

enum {
	MTKAIF_PROTOCOL_1 = 0,
	MTKAIF_PROTOCOL_2,
	MTKAIF_PROTOCOL_2_CLK_P2,
};

enum {
	MTK_AFE_ADDA_DL_GAIN_MUTE = 0,
	MTK_AFE_ADDA_DL_GAIN_NORMAL = 0xf74f,
	/* SA suggest apply -0.3db to audio/speech path */
};

/* MCLK */
enum {
	MT8192_I2S0_MCK = 0,
	MT8192_I2S1_MCK,
	MT8192_I2S2_MCK,
	MT8192_I2S3_MCK,
	MT8192_I2S4_MCK,
	MT8192_I2S4_BCK,
	MT8192_I2S5_MCK,
	MT8192_I2S6_MCK,
	MT8192_I2S7_MCK,
	MT8192_I2S8_MCK,
	MT8192_I2S9_MCK,
	MT8192_MCK_NUM,
};

struct clk;

struct mt8192_afe_private {
	struct clk **clk;
	struct regmap *topckgen;
	struct regmap *apmixedsys;
	struct regmap *infracfg;
	int stf_positive_gain_db;
	int pm_runtime_bypass_reg_ctl;

	/* dai */
	bool dai_on[MT8192_DAI_NUM];
	void *dai_priv[MT8192_DAI_NUM];

	/* adda */
	int mtkaif_protocol;
	int mtkaif_chosen_phase[4];
	int mtkaif_phase_cycle[4];
	int mtkaif_calibration_num_phase;
	int mtkaif_dmic;
	int mtkaif_dmic_ch34;
	int mtkaif_adda6_only;

	/* mck */
	int mck_rate[MT8192_MCK_NUM];
};

int mt8192_dai_adda_register(struct mtk_base_afe *afe);
int mt8192_dai_i2s_register(struct mtk_base_afe *afe);
int mt8192_dai_hw_gain_register(struct mtk_base_afe *afe);
int mt8192_dai_src_register(struct mtk_base_afe *afe);
int mt8192_dai_pcm_register(struct mtk_base_afe *afe);
int mt8192_dai_tdm_register(struct mtk_base_afe *afe);

int mt8192_dai_i2s_set_share(struct mtk_base_afe *afe, const char *main_i2s_name,
			     const char *secondary_i2s_name);

unsigned int mt8192_general_rate_transform(struct device *dev,
					   unsigned int rate);
unsigned int mt8192_rate_transform(struct device *dev,
				   unsigned int rate, int aud_blk);

int mt8192_dai_set_priv(struct mtk_base_afe *afe, int id,
			int priv_size, const void *priv_data);

#endif
