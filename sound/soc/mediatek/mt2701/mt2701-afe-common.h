/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt2701-afe-common.h  --  Mediatek 2701 audio driver definitions
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 */

#ifndef _MT_2701_AFE_COMMON_H_
#define _MT_2701_AFE_COMMON_H_

#include <sound/soc.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include "mt2701-reg.h"
#include "../common/mtk-base-afe.h"

#define MT2701_PLL_DOMAIN_0_RATE	98304000
#define MT2701_PLL_DOMAIN_1_RATE	90316800

enum {
	MT2701_MEMIF_DL1,
	MT2701_MEMIF_DL2,
	MT2701_MEMIF_DL3,
	MT2701_MEMIF_DL4,
	MT2701_MEMIF_DL5,
	MT2701_MEMIF_DL_SINGLE_NUM,
	MT2701_MEMIF_DLM = MT2701_MEMIF_DL_SINGLE_NUM,
	MT2701_MEMIF_UL1,
	MT2701_MEMIF_UL2,
	MT2701_MEMIF_UL3,
	MT2701_MEMIF_UL4,
	MT2701_MEMIF_UL5,
	MT2701_MEMIF_DLBT,
	MT2701_MEMIF_ULBT,
	MT2701_MEMIF_NUM,
	MT2701_IO_I2S = MT2701_MEMIF_NUM,
	MT2701_IO_2ND_I2S,
	MT2701_IO_3RD_I2S,
	MT2701_IO_4TH_I2S,
	MT2701_IO_5TH_I2S,
	MT2701_IO_6TH_I2S,
	MT2701_IO_MRG,
};

enum {
	MT2701_IRQ_ASYS_IRQ1,
	MT2701_IRQ_ASYS_IRQ2,
	MT2701_IRQ_ASYS_IRQ3,
	MT2701_IRQ_ASYS_END,
};

enum audio_base_clock {
	MT2701_INFRA_SYS_AUDIO,
	MT2701_TOP_AUD_MCLK_SRC0,
	MT2701_TOP_AUD_MCLK_SRC1,
	MT2701_TOP_AUD_A1SYS,
	MT2701_TOP_AUD_A2SYS,
	MT2701_AUDSYS_AFE,
	MT2701_AUDSYS_AFE_CONN,
	MT2701_AUDSYS_A1SYS,
	MT2701_AUDSYS_A2SYS,
	MT2701_BASE_CLK_NUM,
};

struct mt2701_i2s_data {
	int i2s_ctrl_reg;
	int i2s_asrc_fs_shift;
	int i2s_asrc_fs_mask;
};

struct mt2701_i2s_path {
	int mclk_rate;
	int on[MTK_STREAM_NUM];
	int occupied[MTK_STREAM_NUM];
	const struct mt2701_i2s_data *i2s_data[MTK_STREAM_NUM];
	struct clk *hop_ck[MTK_STREAM_NUM];
	struct clk *sel_ck;
	struct clk *div_ck;
	struct clk *mclk_ck;
	struct clk *asrco_ck;
};

struct mt2701_soc_variants {
	bool has_one_heart_mode;
	int i2s_num;
};

struct mt2701_afe_private {
	struct mt2701_i2s_path *i2s_path;
	struct clk *base_ck[MT2701_BASE_CLK_NUM];
	struct clk *mrgif_ck;
	bool mrg_enable[MTK_STREAM_NUM];

	const struct mt2701_soc_variants *soc;
};

#endif
