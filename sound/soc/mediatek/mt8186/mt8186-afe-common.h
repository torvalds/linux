/* SPDX-License-Identifier: GPL-2.0
 *
 * mt8186-afe-common.h  --  Mediatek 8186 audio driver definitions
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Jiaxin Yu <jiaxin.yu@mediatek.com>
 */

#ifndef _MT_8186_AFE_COMMON_H_
#define _MT_8186_AFE_COMMON_H_
#include <sound/soc.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include "mt8186-reg.h"
#include "../common/mtk-base-afe.h"

enum {
	MT8186_MEMIF_DL1,
	MT8186_MEMIF_DL12,
	MT8186_MEMIF_DL2,
	MT8186_MEMIF_DL3,
	MT8186_MEMIF_DL4,
	MT8186_MEMIF_DL5,
	MT8186_MEMIF_DL6,
	MT8186_MEMIF_DL7,
	MT8186_MEMIF_DL8,
	MT8186_MEMIF_VUL12,
	MT8186_MEMIF_VUL2,
	MT8186_MEMIF_VUL3,
	MT8186_MEMIF_VUL4,
	MT8186_MEMIF_VUL5,
	MT8186_MEMIF_VUL6,
	MT8186_MEMIF_AWB,
	MT8186_MEMIF_AWB2,
	MT8186_MEMIF_NUM,
	MT8186_DAI_ADDA = MT8186_MEMIF_NUM,
	MT8186_DAI_AP_DMIC,
	MT8186_DAI_CONNSYS_I2S,
	MT8186_DAI_I2S_0,
	MT8186_DAI_I2S_1,
	MT8186_DAI_I2S_2,
	MT8186_DAI_I2S_3,
	MT8186_DAI_HW_GAIN_1,
	MT8186_DAI_HW_GAIN_2,
	MT8186_DAI_SRC_1,
	MT8186_DAI_SRC_2,
	MT8186_DAI_PCM,
	MT8186_DAI_TDM_IN,
	MT8186_DAI_HOSTLESS_LPBK,
	MT8186_DAI_HOSTLESS_FM,
	MT8186_DAI_HOSTLESS_HW_GAIN_AAUDIO,
	MT8186_DAI_HOSTLESS_SRC_AAUDIO,
	MT8186_DAI_HOSTLESS_SRC_1,
	MT8186_DAI_HOSTLESS_SRC_BARGEIN,
	MT8186_DAI_HOSTLESS_UL1,
	MT8186_DAI_HOSTLESS_UL2,
	MT8186_DAI_HOSTLESS_UL3,
	MT8186_DAI_HOSTLESS_UL5,
	MT8186_DAI_HOSTLESS_UL6,
	MT8186_DAI_NUM,
};

#define MT8186_RECORD_MEMIF MT8186_MEMIF_VUL12
#define MT8186_ECHO_REF_MEMIF MT8186_MEMIF_AWB
#define MT8186_PRIMARY_MEMIF MT8186_MEMIF_DL1
#define MT8186_FAST_MEMIF MT8186_MEMIF_DL2
#define MT8186_DEEP_MEMIF MT8186_MEMIF_DL3
#define MT8186_VOIP_MEMIF MT8186_MEMIF_DL12
#define MT8186_MMAP_DL_MEMIF MT8186_MEMIF_DL5
#define MT8186_MMAP_UL_MEMIF MT8186_MEMIF_VUL5
#define MT8186_BARGEIN_MEMIF MT8186_MEMIF_AWB

enum {
	MT8186_IRQ_0,
	MT8186_IRQ_1,
	MT8186_IRQ_2,
	MT8186_IRQ_3,
	MT8186_IRQ_4,
	MT8186_IRQ_5,
	MT8186_IRQ_6,
	MT8186_IRQ_7,
	MT8186_IRQ_8,
	MT8186_IRQ_9,
	MT8186_IRQ_10,
	MT8186_IRQ_11,
	MT8186_IRQ_12,
	MT8186_IRQ_13,
	MT8186_IRQ_14,
	MT8186_IRQ_15,
	MT8186_IRQ_16,
	MT8186_IRQ_17,
	MT8186_IRQ_18,
	MT8186_IRQ_19,
	MT8186_IRQ_20,
	MT8186_IRQ_21,
	MT8186_IRQ_22,
	MT8186_IRQ_23,
	MT8186_IRQ_24,
	MT8186_IRQ_25,
	MT8186_IRQ_26,
	MT8186_IRQ_NUM,
};

enum {
	MT8186_AFE_IRQ_DIR_MCU = 0,
	MT8186_AFE_IRQ_DIR_DSP,
	MT8186_AFE_IRQ_DIR_BOTH,
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

#define MTK_SPK_I2S_0_STR "MTK_SPK_I2S_0"
#define MTK_SPK_I2S_1_STR "MTK_SPK_I2S_1"
#define MTK_SPK_I2S_2_STR "MTK_SPK_I2S_2"
#define MTK_SPK_I2S_3_STR "MTK_SPK_I2S_3"

/* MCLK */
enum {
	MT8186_I2S0_MCK = 0,
	MT8186_I2S1_MCK,
	MT8186_I2S2_MCK,
	MT8186_I2S4_MCK,
	MT8186_TDM_MCK,
	MT8186_MCK_NUM,
};

struct snd_pcm_substream;
struct mtk_base_irq_data;
struct clk;

struct mt8186_afe_private {
	struct clk **clk;
	struct clk_lookup **lookup;
	struct regmap *topckgen;
	struct regmap *apmixedsys;
	struct regmap *infracfg;
	int irq_cnt[MT8186_MEMIF_NUM];
	int stf_positive_gain_db;
	int pm_runtime_bypass_reg_ctl;
	int sgen_mode;
	int sgen_rate;
	int sgen_amplitude;

	/* xrun assert */
	int xrun_assert[MT8186_MEMIF_NUM];

	/* dai */
	bool dai_on[MT8186_DAI_NUM];
	void *dai_priv[MT8186_DAI_NUM];

	/* adda */
	bool mtkaif_calibration_ok;
	int mtkaif_protocol;
	int mtkaif_chosen_phase[4];
	int mtkaif_phase_cycle[4];
	int mtkaif_calibration_num_phase;
	int mtkaif_dmic;
	int mtkaif_looback0;
	int mtkaif_looback1;

	/* mck */
	int mck_rate[MT8186_MCK_NUM];
};

int mt8186_dai_adda_register(struct mtk_base_afe *afe);
int mt8186_dai_i2s_register(struct mtk_base_afe *afe);
int mt8186_dai_tdm_register(struct mtk_base_afe *afe);
int mt8186_dai_hw_gain_register(struct mtk_base_afe *afe);
int mt8186_dai_src_register(struct mtk_base_afe *afe);
int mt8186_dai_pcm_register(struct mtk_base_afe *afe);
int mt8186_dai_hostless_register(struct mtk_base_afe *afe);

int mt8186_add_misc_control(struct snd_soc_component *component);

unsigned int mt8186_general_rate_transform(struct device *dev,
					   unsigned int rate);
unsigned int mt8186_rate_transform(struct device *dev,
				   unsigned int rate, int aud_blk);
unsigned int mt8186_tdm_relatch_rate_transform(struct device *dev,
					       unsigned int rate);

int mt8186_dai_i2s_set_share(struct mtk_base_afe *afe, const char *main_i2s_name,
			     const char *secondary_i2s_name);

int mt8186_dai_set_priv(struct mtk_base_afe *afe, int id,
			int priv_size, const void *priv_data);

#endif
